#include "app.h"
#include "net/update.h"
#include "core/identity.h"
#include "core/log.h"
#include "core/place.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <string>
#include <cmath>

namespace nxp {

namespace {

// Settings and privacy.
//
// Two panes: a 360px section list on --bg-1 (the expanded sidebar),
// and the chosen section's rows to its right under a --text-2xl heading and a
// line of explanation. Rows are --bg-1 at rest and --bg-2 once focused, with
// the control on the right: a 96x52 toggle, or a row of separate pills.
class SettingsScene final : public Scene {
public:
    enum Zone : int {
        Zone_Section = Touch_SceneBase, // index = the section
        Zone_Row,                       // index = the setting id
        Zone_Segment,                   // index packs the id and the option
    };
    static constexpr int kSegmentShift = 8;

    void onEnter(App& app) override
    {
        // Coming back to the tab starts on the section list, not inside a
        // section, so A always means "open this one".
        m_inSidebar = true;
        m_scroll.stop();
    }

    void update(App& app, const Input& input, float dt) override
    {
        m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

        if (m_placeAge <= 0.0f) {
            m_place = currentPlace();
            m_placeAge = 2.0f;
        }
        m_placeAge -= dt;

        build(app);

        const Touch& touch = input.touch;
        if (touch.pressed)
            m_brakedTap = m_scroll.absorbPress();
        if (touch.down && touch.dragged && m_rowsArea.contains(touch.startX, touch.startY)) {
            m_scroll.drag(-touch.dy, dt);
            m_dragging = true;
        } else if (m_dragging && !touch.down) {
            m_scroll.release();
            m_dragging = false;
        }
        m_scroll.update(dt);

        TouchTarget tap;
        if (!m_brakedTap && app.takeTap(tap)) {
            if (tap.is(Zone_Section)) {
                enterSection(tap.index);
                return;
            }
            if (tap.is(Zone_Segment)) {
                Id id = static_cast<Id>(tap.index >> kSegmentShift);
                m_inSidebar = false;
                focusOn(id);
                applySegment(app, id, tap.index & ((1 << kSegmentShift) - 1));
                return;
            }
            if (tap.is(Zone_Row)) {
                Id id = static_cast<Id>(tap.index);
                m_inSidebar = false;
                focusOn(id);
                activate(app, id);
                return;
            }
        }

        if (m_inSidebar)
            updateSidebar(app, input);
        else
            updateRows(app, input);
    }

    void draw(App& app, Renderer& r) override
    {
        build(app);

        Rect area = app.contentArea();
        Rect sidebar { area.x, area.y, theme::railExpanded, area.h };
        drawSidebar(app, r, sidebar);

        Rect pane { sidebar.right() + theme::edge, area.y + theme::edgeTop,
            std::min(area.right() - sidebar.right() - theme::edge * 2.0f, 1280.0f),
            area.h - theme::edgeTop * 2.0f };
        drawPane(app, r, pane);
    }

private:
    // -------------------------------------------------------------- sections

    enum Section : int {
        Sec_Privacy = 0,
        Sec_Exchange,
        Sec_Notifications,
        Sec_Appearance,
        Sec_Console,
        Sec_Data,
        Sec_About,
        Sec_Count
    };

    struct SectionInfo {
        const char* label;
        ui::Icon icon;
        const char* title;
        // A string rather than a literal: one of these names the app, and the
        // app's name comes from the build.
        std::string blurb;
    };

    static const SectionInfo& sectionInfo(int index)
    {
        static const SectionInfo kSections[Sec_Count] = {
            { "Privacy", ui::Icon::Shield, "Privacy",
                format("%s never sends your name or an exact position. You choose how much ",
                    kAppName) +
                "of a pass leaves the console." },
            { "Exchange", ui::Icon::Radar, "Exchange", "How passes find their way to you." },
            { "Notifications", ui::Icon::Bell, "Notifications",
                "Nothing interrupts a game. The plaza waits." },
            { "Appearance", ui::Icon::Sun, "Appearance",
                "The app opens in daylight. Dark is for a dim room, or match whatever the "
                "console is set to." },
            { "This console", ui::Icon::Monitor, "This console",
                "Where your passes go, and how another console recognises this one." },
            { "Data", ui::Icon::Trash, "Data",
                "Everything the app keeps lives on the SD card, and all of it can go." },
            { "About", ui::Icon::Info, "About",
                format("What this build of %s is, who made it, and where it came from.",
                    kAppName) },
        };
        return kSections[std::min(std::max(index, 0), static_cast<int>(Sec_Count) - 1)];
    }

    // ------------------------------------------------------------------ rows

    enum Id : int {
        Id_None = 0,
        Id_PlaceSharing,
        Id_District,
        Id_City,
        Id_SharePlaying,
        Id_AutoExchange,
        Id_Reach,
        Id_DailyLimit,
        Id_Notify,
        Id_Theme,
        Id_ServerUrl,
        Id_PlaceToken,
        Id_TestConnection,
        Id_LogToFile,
        Id_Unblock,
        Id_DeleteAll,
        Id_NewIdentity,
        Id_About,
        Id_Source,
        Id_ConsoleId,
        Id_CheckUpdates,
        Id_AutoCheckUpdates,
    };

    enum class Kind { Toggle, Segmented, Value, Action, Danger };

    struct Row {
        Kind kind = Kind::Value;
        Id id = Id_None;
        std::string label;
        std::string hint;
        std::string value;
        bool toggleState = false;
        int selected = 0;
        const char* const* options = nullptr;
        int optionCount = 0;
    };

    // Moves the highlight in the section list. Deliberately does *not* enter
    // the section: walking the list with the d-pad should never drop the cursor
    // into a section's rows, or a pass down the list changes six settings'
    // worth of focus on the way past.
    void showSection(int index)
    {
        int clamped = std::min(std::max(index, 0), static_cast<int>(Sec_Count) - 1);
        if (clamped == m_section)
            return;

        m_section = clamped;
        m_focus = 0;
        m_scroll.stop();
    }

    // A on a section, or a tap on one: now the rows take the cursor.
    void enterSection(int index)
    {
        showSection(index);
        m_inSidebar = false;
    }

    void focusOn(Id id)
    {
        for (size_t i = 0; i < m_rows.size(); i++) {
            if (m_rows[i].id == id) {
                m_focus = static_cast<int>(i);
                return;
            }
        }
    }

    void updateSidebar(App& app, const Input& input)
    {
        if (input.navDown)
            showSection(m_section + 1);
        if (input.navUp)
            showSection(m_section - 1);

        // A is the only way in. Right is a navigation key, so it moves the
        // highlight or does nothing - it does not commit.
        if (input.accept())
            m_inSidebar = false;
    }

    void updateRows(App& app, const Input& input)
    {
        if (m_rows.empty())
            return;

        // B is the only way out, as A is the only way in. Left and Right belong
        // entirely to the row's own control, so a segmented setting can be
        // stepped back down without the cursor escaping the pane.
        if (input.back()) {
            m_inSidebar = true;
            return;
        }

        if (input.navDown || input.navUp) {
            m_focus = std::min(std::max(m_focus + (input.navDown ? 1 : -1), 0),
                static_cast<int>(m_rows.size()) - 1);
        }

        const Row& row = m_rows[static_cast<size_t>(m_focus)];

        int delta = 0;
        if (input.navRight)
            delta = 1;
        else if (input.navLeft)
            delta = -1;
        if (delta != 0)
            adjust(app, row.id, delta);

        if (input.accept())
            activate(app, row.id);
    }

    void build(App& app)
    {
        static const char* placeOptions[] = { "Off", "District", "City" };
        static const char* reachOptions[] = { "Same network", "Nearby", "Anywhere" };
        static const char* themeOptions[] = { "Light", "Dark", "Console" };

        Settings settings = app.store().settings();
        Sync::Status status = app.sync().status();
        m_rows.clear();

        auto toggle = [&](Id id, const char* label, const char* hint, bool state) {
            Row row;
            row.kind = Kind::Toggle;
            row.id = id;
            row.label = label;
            row.hint = hint;
            row.toggleState = state;
            m_rows.push_back(row);
        };
        auto segmented = [&](Id id, const char* label, const std::string& hint,
                             const char* const* options, int count, int selected) {
            Row row;
            row.kind = Kind::Segmented;
            row.id = id;
            row.label = label;
            row.hint = hint;
            row.options = options;
            row.optionCount = count;
            row.selected = selected;
            m_rows.push_back(row);
        };
        auto value = [&](Id id, const char* label, const std::string& hint,
                         const std::string& shown, Kind kind = Kind::Value) {
            Row row;
            row.kind = kind;
            row.id = id;
            row.label = label;
            row.hint = hint;
            row.value = shown;
            m_rows.push_back(row);
        };

        switch (m_section) {
        case Sec_Privacy:
            segmented(Id_PlaceSharing, "Share where we crossed",
                "District only - never a street or a venue you did not name", placeOptions, 3,
                static_cast<int>(settings.placeSharing));
            value(Id_District, "District label", "e.g. Namba Station",
                settings.districtLabel.empty() ? "-" : settings.districtLabel);
            value(Id_City, "City label", "used when sharing is set to City",
                settings.cityLabel.empty() ? "-" : settings.cityLabel);
            toggle(Id_SharePlaying, "Show what I am playing",
                "Hidden titles stay hidden, always", settings.sharePlaying);
            break;

        case Sec_Exchange:
            toggle(Id_AutoExchange, "Exchange passes automatically",
                "Trades happen while the app is open", settings.autoExchange);
            segmented(Id_Reach, "How far a crossing reaches",
                "Same network is the closest thing to walking past someone", reachOptions, 3,
                static_cast<int>(settings.reach));
            value(Id_DailyLimit, "Crossings per day", "left and right to change",
                format("%d", settings.dailyLimit));
            break;

        case Sec_Notifications:
            toggle(Id_Notify, "Tell me when passes arrive",
                "A card in the corner, never an interruption", settings.notify);
            break;

        case Sec_Appearance:
            segmented(Id_Theme, "Theme",
                theme::mode() == theme::Mode::System
                    ? format("following the console, currently %s",
                          theme::resolvedMode() == theme::Mode::Dark ? "dark" : "light")
                    : std::string("light by default"),
                themeOptions, 3, settings.themeMode);
            break;

        case Sec_Console:
            value(Id_ServerUrl, "Plaza server",
                kServerIsEditable ? "http://host:port of your own server"
                                  : "where this build trades passes",
                settings.serverUrl.empty() ? "-" : settings.serverUrl);
            value(Id_PlaceToken, "Wi-Fi match token",
                m_place.token.empty()
                    ? std::string("no Wi-Fi name to match on - wired, or not connected")
                    : format("consoles on \"%s\" share this", m_place.networkName.c_str()),
                m_place.token.empty() ? std::string("(none)") : m_place.token);
            value(Id_TestConnection, "Check in now", status.message,
                identity().shortCode(), Kind::Action);
            value(Id_Unblock, "Blocked consoles", "A to clear the whole list",
                format("%zu blocked", settings.blocked.size()), Kind::Action);
            toggle(Id_LogToFile, "Write a log file",
                settings.logToFile
                    ? "plaza.log, beside your pass on the SD card - it names this "
                      "console and the server"
                    : "Off. Turn it on before reporting a problem, then off again",
                settings.logToFile);
            break;

        case Sec_Data:
        default:
            value(Id_DeleteAll, "Delete every pass you have collected",
                "This cannot be undone", "", Kind::Danger);
            value(Id_NewIdentity, "Start over as someone new",
                "New id, new pass; everything you handed out goes unlinkable", "",
                Kind::Danger);
            break;

        case Sec_About:
            {
                // build() runs every frame, so this row doubles as the progress
                // display: there is no separate dialog to keep in step.
                Update& updater = Update::get();
                std::string hint;
                std::string shown;
                switch (updater.state()) {
                case UpdateState::Checking:
                    hint = "Asking GitHub for the latest release";
                    shown = "...";
                    break;
                case UpdateState::Available:
                    hint = "Press A to download and install it";
                    shown = updater.version();
                    break;
                case UpdateState::Downloading:
                    hint = updater.message();
                    shown = format("%.0f%%", updater.progress() * 100.0f);
                    break;
                case UpdateState::Installed:
                    hint = "Press A to restart into it";
                    shown = updater.version();
                    break;
                case UpdateState::Failed:
                    hint = updater.message();
                    shown = "-";
                    break;
                case UpdateState::UpToDate:
                    hint = "You are on the latest release";
                    shown = "up to date";
                    break;
                default:
                    hint = "Looks at the releases on GitHub";
                    shown = "";
                    break;
                }
                value(Id_CheckUpdates, "Check for updates", hint, shown, Kind::Action);
            }
            toggle(Id_AutoCheckUpdates, "Check on every launch",
                "One request when the app opens; it never installs on its own",
                settings.checkUpdates);

            // What this is, who made it, and where it lives. The id below them
            // is on a row of its own because it identifies the console rather
            // than the build: it is the thing to quote when reporting a fault,
            // and nothing to do with the rows above it.
            value(Id_About, "NX Plaza - Online StreetPass | " APP_VERSION, "Developed by Insektaure", "",
                Kind::Value);
            value(Id_Source, "Find us on GitHub !", "https://github.com/Insektaure/NX-Plaza", "", Kind::Value);
            value(Id_ConsoleId, "This console's id", identity().id, "", Kind::Value);
            break;
        }

        m_focus = m_rows.empty()
            ? 0
            : std::min(std::max(m_focus, 0), static_cast<int>(m_rows.size()) - 1);
    }

    // ---------------------------------------------------------------- change

    void applySegment(App& app, Id id, int value)
    {
        Settings settings = app.store().settings();

        switch (id) {
        case Id_PlaceSharing:
            settings.placeSharing = static_cast<PlaceSharing>(
                std::min(std::max(value, 0), Place_Count - 1));
            break;
        case Id_Reach:
            settings.reach = static_cast<Reach>(std::min(std::max(value, 0), Reach_Count - 1));
            break;
        case Id_Theme:
            settings.themeMode = std::min(std::max(value, 0),
                static_cast<int>(theme::Mode::Count) - 1);
            theme::setMode(static_cast<theme::Mode>(settings.themeMode));
            break;
        default:
            return;
        }

        app.store().setSettings(settings);
        app.store().flush();
        if (id != Id_Theme)
            app.sync().publishPass();
    }

    void adjust(App& app, Id id, int delta)
    {
        Settings settings = app.store().settings();

        switch (id) {
        case Id_PlaceSharing:
            applySegment(app, id,
                (static_cast<int>(settings.placeSharing) + delta + Place_Count) % Place_Count);
            return;
        case Id_Reach:
            applySegment(app, id,
                (static_cast<int>(settings.reach) + delta + Reach_Count) % Reach_Count);
            return;
        case Id_Theme:
            applySegment(app, id,
                (settings.themeMode + delta + static_cast<int>(theme::Mode::Count))
                    % static_cast<int>(theme::Mode::Count));
            return;
        case Id_DailyLimit:
            settings.dailyLimit = std::min(48, std::max(1, settings.dailyLimit + delta));
            break;
        default:
            return;
        }

        app.store().setSettings(settings);
        app.store().flush();
        app.sync().publishPass();
    }

    void activate(App& app, Id id)
    {
        Settings settings = app.store().settings();
        std::string value;

        switch (id) {
        case Id_SharePlaying:
            settings.sharePlaying = !settings.sharePlaying;
            break;
        case Id_AutoExchange:
            settings.autoExchange = !settings.autoExchange;
            break;
        case Id_Notify:
            settings.notify = !settings.notify;
            break;
        case Id_AutoCheckUpdates:
            settings.checkUpdates = !settings.checkUpdates;
            break;
        case Id_CheckUpdates: {
            Update& updater = Update::get();
            switch (updater.state()) {
            case UpdateState::Available:
                app.askConfirm("Install version " + updater.version() + "?",
                    "The new version is downloaded, checked, and only then written over "
                    "this one. The copy you are running now is kept until the new one has "
                    "been verified.",
                    "Install", [] { Update::get().beginInstall(); });
                break;
            case UpdateState::Installed:
                app.askConfirm("Restart into version " + updater.version() + "?",
                    "The app closes and opens again on the new version. Your passes and "
                    "your collection are untouched.",
                    "Restart", [&app] {
                        Update::restartIntoUpdate();
                        app.requestExit();
                    });
                break;
            case UpdateState::Checking:
            case UpdateState::Downloading:
                // Already working. Pressing A again should not start a second one.
                break;
            default:
                updater.beginCheck(true);
                break;
            }
            return;
        }
        case Id_LogToFile:
            settings.logToFile = !settings.logToFile;
            // Immediately, rather than on the next launch: someone turning this
            // on is about to reproduce something, and someone turning it off
            // means stop writing now.
            logSetFileEnabled(settings.logToFile);
            break;
        case Id_PlaceSharing:
        case Id_Reach:
        case Id_Theme:
            adjust(app, id, 1);
            return;
        case Id_District:
            if (app.textInput("Where are you? A district, a station, a shop",
                    settings.districtLabel, 24, value))
                settings.districtLabel = value;
            break;
        case Id_City:
            if (app.textInput("Which city", settings.cityLabel, 24, value))
                settings.cityLabel = value;
            break;
        case Id_ServerUrl:
            // Nothing to edit in a released build: one plaza, compiled in.
            if (!kServerIsEditable)
                break;
            if (app.textInput("Plaza server address", settings.serverUrl, 120, value, false)) {
                settings.serverUrl = value;
                app.sync().publishPass();
                app.sync().kick();
            }
            break;
        case Id_TestConnection:
            app.sync().publishPass();
            app.sync().kick();
            app.toast("Checking in", settings.serverUrl);
            return;
        case Id_Unblock:
            if (settings.blocked.empty()) {
                app.toast("Nothing blocked", "No console is on your block list.");
                return;
            }
            settings.blocked.clear();
            app.toast("Block list cleared", "Those consoles can cross you again.");
            break;
        case Id_DeleteAll:
            app.askConfirm("Delete every pass?",
                "Every pass you have collected is removed from this console. Your own pass "
                "and your identity stay.",
                "Delete them all",
                [appPtr = &app]() {
                    appPtr->store().deleteAllCrossings();
                    appPtr->toast("Collection cleared", "The plaza is empty again.");
                });
            return;
        case Id_NewIdentity:
            app.askConfirm("Start over as someone new?",
                "This console gets a brand new id. Passes you already handed out can no "
                "longer be linked to you, and your collection is deleted.",
                "Start over",
                [appPtr = &app]() {
                    appPtr->sync().forgetMe();
                    identityRotate();
                    appPtr->store().deleteAllCrossings();
                    appPtr->store().setMyPass(Pass::makeDefault(suggestedHandle()));
                    appPtr->store().flush();
                    appPtr->sync().publishPass();
                    appPtr->toast("You are someone new",
                        format("Your code is now %s", identity().shortCode().c_str()));
                });
            return;
        default:
            return;
        }

        app.store().setSettings(settings);
        app.store().flush();
        app.sync().publishPass();
    }

    // ------------------------------------------------------------------ draw

    // The section list: --bg-1, padding 48px 32px, items at 18px --space-5.
    void drawSidebar(App& app, Renderer& r, const Rect& box)
    {
        r.rect(box, theme::bg1);
        r.rect(Rect { box.right() - theme::stroke, box.y, theme::stroke, box.h },
            theme::stroke1);

        float y = box.y + theme::edgeTop;
        float itemHeight = theme::textBase * theme::leadingNormal + 36.0f;

        for (int i = 0; i < Sec_Count; i++) {
            const SectionInfo& info = sectionInfo(i);
            Rect item { box.x + theme::s6, y, box.w - theme::s6 * 2.0f, itemHeight };
            app.touchZone(item, Zone_Section, i);

            bool current = i == m_section;
            bool held = app.touchHeld(Zone_Section, i);

            // The tint says which section is open; the ring says where the
            // cursor is. They are different questions, and while the cursor is
            // in the rows the open section keeps its tint but loses the ring.
            if (current)
                r.roundRect(item, theme::r2, theme::accentTint);
            else if (held)
                r.roundRect(item, theme::r2, theme::bg3);

            if (m_inSidebar && current)
                ui::focusRing(r, item, theme::r2, 0.7f + 0.3f * m_pulse);

            Rect iconBox { item.x + theme::s5, item.centerY() - 18.0f, 36.0f, 36.0f };
            ui::icon(r, iconBox, info.icon, current ? theme::accent : theme::fg3, 3.0f);

            TextStyle label;
            label.size = theme::textBase;
            label.weight = current ? FontWeight::Bold : FontWeight::Regular;
            label.color = current ? theme::accent : theme::fg2;
            r.text(Rect { iconBox.right() + theme::s4, item.y, item.w, item.h }, info.label,
                label, Align::Left, VAlign::Middle);

            y += itemHeight + theme::s2;
        }
    }

    // The heading, its line of explanation, then the rows.
    void drawPane(App& app, Renderer& r, const Rect& box)
    {
        if (m_inSidebar) {
            app.hint("A", "open section");
        } else {
            app.hint("A", "change");
            app.hint("B", "sections");
        }

        const SectionInfo& info = sectionInfo(m_section);

        TextStyle title;
        title.size = theme::text2xl;
        title.weight = FontWeight::Bold;
        title.color = theme::fg1;
        title.tracking = theme::trackingTight;
        r.text(box.x, box.y, info.title, title);

        TextStyle blurb;
        blurb.size = theme::textBase;
        blurb.color = theme::fg2;
        blurb.leading = theme::leadingNormal;
        float blurbY = box.y + title.size * theme::leadingTight + theme::s3;
        float blurbHeight = r.textWrapped(Rect { box.x, blurbY, std::min(box.w, 900.0f), 120.0f },
            info.blurb, blurb, 2);

        Rect rows { box.x, blurbY + blurbHeight + theme::s6, box.w,
            box.bottom() - (blurbY + blurbHeight + theme::s6) };
        m_rowsArea = rows;

        float rowHeight = kRowHeight;
        float total = static_cast<float>(m_rows.size()) * (rowHeight + theme::s3);
        m_scroll.setBounds(rows.h, std::max(0.0f, total - theme::s3));

        r.pushClipVertical(rows.inset(0.0f, -theme::focusRoom));
        float y = rows.y - m_scroll.offset();
        for (size_t i = 0; i < m_rows.size(); i++) {
            Rect row { rows.x, y, rows.w, rowHeight };
            if (row.bottom() > rows.y - rowHeight && row.y < rows.bottom() + rowHeight)
                drawRow(app, r, row, m_rows[i], static_cast<int>(i));
            y += rowHeight + theme::s3;
        }
        r.popClip();

        if (m_scroll.scrollable())
            ui::scrollbar(r, Rect { rows.right() + theme::s4, rows.y, 8.0f, rows.h },
                m_scroll.progress(), m_scroll.visibleFraction());


    }

    void drawRow(App& app, Renderer& r, const Rect& box, const Row& row, int index)
    {
        app.touchZone(box, Zone_Row, row.id);

        bool focused = !m_inSidebar && index == m_focus;
        float focus = app.touchHeld(Zone_Row, row.id)
            ? 1.0f
            : (focused ? 0.7f + 0.3f * m_pulse : 0.0f);

        bool danger = row.kind == Kind::Danger;
        Color fill = danger ? theme::dangerTint : (focused ? theme::bg2 : theme::bg1);
        ui::card(r, box, focus, fill, theme::r3);

        Rect inner = box.inset(theme::s6, theme::s5);

        TextStyle label;
        label.size = theme::textBase;
        label.weight = FontWeight::Bold;
        label.color = danger ? theme::danger : theme::fg1;
        r.text(inner.x, inner.y, r.ellipsize(row.label, label, inner.w * 0.6f), label);

        if (!row.hint.empty()) {
            TextStyle hint;
            hint.size = theme::textSm;
            hint.color = theme::fg3;
            r.text(inner.x, inner.y + label.size * theme::leadingSnug + 6.0f,
                r.ellipsize(row.hint, hint, inner.w * 0.6f), hint);
        }

        switch (row.kind) {
        case Kind::Toggle:
            ui::toggle(r, inner, row.toggleState, focus);
            break;

        case Kind::Segmented: {
            // A zone per pill, so touch can pick an option instead of stepping.
            float gap = 10.0f;
            float total = 0.0f;
            for (int i = 0; i < row.optionCount; i++)
                total += ui::segmentWidth(r, row.options[i]) + (i ? gap : 0.0f);

            float x = inner.right() - total;
            float height = theme::textSm * theme::leadingNormal + 24.0f;
            for (int i = 0; i < row.optionCount; i++) {
                float width = ui::segmentWidth(r, row.options[i]);
                app.touchZone(Rect { x, inner.centerY() - height * 0.5f, width, height },
                    Zone_Segment, (row.id << kSegmentShift) | i);
                x += width + gap;
            }

            ui::segmented(r, inner, row.options, row.optionCount, row.selected, focus);
            break;
        }

        default: {
            if (row.value.empty())
                break;
            TextStyle value;
            value.size = theme::textBase;
            value.weight = FontWeight::Medium;
            value.color = theme::fg2;
            float width = std::min(r.measure(row.value, value), inner.w * 0.38f);
            r.text(Rect { inner.right() - width, inner.y, width, inner.h },
                r.ellipsize(row.value, value, width), value, Align::Left, VAlign::Middle);
            break;
        }
        }
    }

    static constexpr float kRowHeight = 124.0f;

    std::vector<Row> m_rows;
    int m_section = Sec_Privacy;
    int m_focus = 0;
    bool m_inSidebar = true; // the section list has the cursor on arrival
    float m_pulse = 0.0f;

    PlaceInfo m_place;
    float m_placeAge = 0.0f;

    ui::ScrollView m_scroll;
    Rect m_rowsArea;
    bool m_dragging = false;
    bool m_brakedTap = false;
};

} // namespace

std::unique_ptr<Scene> makeSettingsScene()
{
    return std::unique_ptr<Scene>(new SettingsScene());
}

} // namespace nxp
