#include "app.h"
#include "core/backup.h"
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

        // A hint too long for its row scrolls, but only the one under the
        // cursor. The clock restarts whenever the cursor lands somewhere
        // new, so a hint always begins at its first word.
        // The section is part of the identity: row 2 of Privacy and row 2 of
        // Data are different rows, and a hint that carried on mid-sentence from
        // the last section would read as a glitch.
        int here = m_inSidebar ? -1 : (m_section * 1000 + m_focus);
        if (here != m_marqueeRow) {
            m_marqueeRow = here;
            m_marquee = 0.0f;
        } else {
            m_marquee += dt;
        }

        // Deliberately not currentPlace(): that is two nifm calls, and this
        // runs on the drawing thread. While the console is still negotiating
        // its network those calls block, and the sync worker is making the
        // same ones from its own thread, so the two queue up on one session -
        // which is how opening Settings during startup froze the whole app for
        // most of a second. The worker looks the network up every check-in
        // anyway, so this reads what it already found.

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
        Id_Backup,
        Id_GetArt,
        Id_CheckUpdates,
        Id_AutoCheckUpdates,

        // One row per blocked console, so their ids are a range rather than a
        // name. Last in the enum: everything above it is a fixed row, and
        // anything at or past it is the nth entry on the block list.
        Id_BlockedFirst = 1000,
    };

    enum class Kind { Toggle, Segmented, Value, Action, Danger };

    struct Row {
        Kind kind = Kind::Value;
        Id id = Id_None;
        // Drawn dim, no touch zone, and A does nothing. For a row whose work is
        // a request: pressing it with no network would change this console and
        // not the plaza, and the two disagreeing is the whole problem.
        bool enabled = true;
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

        if (input.accept()) {
            if (!row.enabled) {
                // Says why rather than doing nothing. A dead button that gives
                // no reason reads as a bug.
                app.toast("Not connected to the plaza",
                    "This one needs the server. The dot by the tabs turns green when "
                    "it can be reached.");
            } else {
                activate(app, row.id);
            }
        }
    }

    // Whether a request would go anywhere right now.
    //
    // Offline only, not Error: an error has already been retried and will be
    // again with backoff, so refusing on one is refusing on a wobble.
    static bool plazaReachable(const Sync::Status& status)
    {
        return status.state != Sync::State::Offline;
    }

    void build(App& app)
    {
        static const char* placeOptions[] = { "Off", "District", "City" };
        static const char* reachOptions[] = { "Same network", "Nearby", "Anywhere" };
        static const char* themeOptions[] = { "Light", "Dark", "Console" };

        Settings settings = app.store().settings();
        Sync::Status status = app.sync().status();
        const bool reachable = plazaReachable(status);
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
        // A std::string label, not a const char*: most rows are literals, but a
        // blocked console's row is named after whoever it was.
        auto value = [&](Id id, const std::string& label, const std::string& hint,
                         const std::string& shown, Kind kind = Kind::Value,
                         bool enabled = true) {
            Row row;
            row.kind = kind;
            row.id = id;
            row.label = label;
            row.hint = hint;
            row.value = shown;
            row.enabled = enabled;
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
                "The title, your hours in it, and how many games you have. Hidden "
                "titles stay hidden, always",
                settings.sharePlaying);
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
                status.placeToken.empty()
                    ? std::string("no Wi-Fi name to match on - wired, or not connected")
                    : format("consoles on \"%s\" share this", status.networkName.c_str()),
                status.placeToken.empty() ? std::string("(none)") : status.placeToken);
            value(Id_TestConnection, "Check in now", status.message,
                identity().shortCode(), Kind::Action);
            // The count was all this said, which told you nothing about who
            // you had blocked or whether one of them was a mis-tap. One row
            // each, A to let that one through.
            if (settings.blocked.empty()) {
                // An action, not a dead line of text. This is exactly the state
                // a restored profile.json leaves behind when the plaza is still
                // holding blocks the card has forgotten, and there is nothing
                // else that can ask for those to go.
                value(Id_Unblock, "Blocked consoles",
                    reachable
                        ? "Nobody is blocked here. A asks the plaza to drop any it "
                          "still has"
                        : "Nobody is blocked here. Needs the plaza, and this console "
                          "is offline",
                    "none", Kind::Action, reachable);
            } else {
                for (size_t b = 0; b < settings.blocked.size(); b++) {
                    const BlockedConsole& who = settings.blocked[b];
                    std::string name = who.name.empty()
                        ? std::string("Somebody")
                        : who.name;
                    // The code is there even when the name is: two people can
                    // pick the same handle, and it is what the list looked like
                    // before names were kept.
                    std::string hint = shortCodeFor(who.id);
                    if (who.when != 0)
                        hint += " - blocked " + relativeTime(who.when, nowUnix());
                    hint = reachable ? hint + ". A lets them cross you again"
                                     : hint + ". Offline - unblocking needs the plaza";
                    value(static_cast<Id>(Id_BlockedFirst + int(b)), name, hint,
                        "unblock", Kind::Action, reachable);
                }
                value(Id_Unblock, "Clear the whole list",
                    reachable
                        ? format("Clears all %zu at once, on this console and on the "
                                 "plaza", settings.blocked.size())
                        : std::string("Offline - clearing the list needs the plaza, or "
                                      "the two would disagree"),
                    "", Kind::Action, reachable);
            }
            toggle(Id_LogToFile, "Write a log file",
                settings.logToFile
                    ? "plaza.log, beside your pass on the SD card - it names this "
                      "console and the server"
                    : "Off. Turn it on before reporting a problem, then off again",
                settings.logToFile);
            break;

        case Sec_Data:
        default:
            value(Id_Backup, "Copy everything to a backup folder",
                "Your identity, your pass and your collection, into backup/ on this card",
                "", Kind::Action);
            {
                // Doubles as its own progress display, the same way the update
                // row in About does: build() runs every frame, so there is no
                // second place for the state to get out of step.
                Update& updater = Update::get();
                bool mine = updater.fetchingArt();
                bool working = mine && updater.state() == UpdateState::Downloading;
                bool done = mine && updater.state() == UpdateState::Installed;
                bool failed = mine && updater.state() == UpdateState::Failed;

                std::string hint = "If the puzzles show numbered squares, get the "
                                   "pictures here and restart";
                std::string shown;
                if (working) {
                    hint = updater.message();
                    shown = format("%.0f%%", updater.progress() * 100.0f);
                } else if (done || failed) {
                    hint = updater.message();
                    shown = done ? "done" : "failed";
                }
                value(Id_GetArt, "Download puzzle art", hint, shown, Kind::Action);
            }
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
                    // The artwork borrows this state and this worker; it has
                    // its own row in Data and should not narrate here.
                    if (updater.fetchingArt())
                        break;
                    hint = updater.message();
                    shown = format("%.0f%%", updater.progress() * 100.0f);
                    break;
                case UpdateState::Installed:
                    if (updater.fetchingArt())
                        break;
                    hint = "Press A to restart into it";
                    shown = updater.version();
                    break;
                case UpdateState::Failed:
                    if (updater.fetchingArt())
                        break; // the artwork's own row reports this
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

        // The block list is a range rather than a named row, so it is handled
        // before the switch. It also writes through the store rather than the
        // local copy below: the copy is written back wholesale at the end, and
        // a list that changed underneath it would be undone.
        if (id >= Id_BlockedFirst) {
            size_t index = size_t(int(id) - int(Id_BlockedFirst));
            if (index >= settings.blocked.size())
                return;
            const BlockedConsole& who = settings.blocked[index];
            std::string name = who.name.empty() ? shortCodeFor(who.id) : who.name;
            std::string id = who.id;
            app.store().unblock(id);
            app.sync().unblockPeer(id);
            // "Can" rather than "will": the plaza lifts our block and not
            // theirs, and it does not tell us whether theirs is still there -
            // which is the same discretion blocking gets in the other
            // direction.
            app.toast("Unblocked " + name,
                "They can cross you again, unless they blocked you as well.");
            return;
        }

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
                if (updater.fetchingArt())
                    return; // artwork, not a build: nothing to restart into
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
        case Id_GetArt:
            // Not gated on the artwork actually being missing: a pack can be
            // present and still be older than the puzzles this build has, and
            // the owner is better placed to know it looks wrong than we are.
            if (!Update::get().beginArtDownload()) {
                app.toast("Busy with another download",
                    "Wait for the update in About to finish, then try again.");
                return;
            }
            // What has happened, not what will: it can still fail, and this
            // row reports either way. The restart is asked for when there is
            // something to restart for.
            app.toast("Downloading puzzle art", "This row shows how it is going.");
            return;

        case Id_Backup: {
            std::string where;
            std::string why;
            if (!createBackup(where, why)) {
                app.toast("Could not back up", why);
                return;
            }
            // The folder name, not the whole path: the path is long, and the
            // only part the owner needs is which folder to look in.
            size_t slash = where.find_last_of('/');
            std::string folder = slash == std::string::npos ? where : where.substr(slash + 1);
            app.toast("Backed up to " + folder,
                "It is on the same card. Copy the backup folder to a computer to be safe "
                "from losing the card itself.");
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
        case Id_Unblock: {
            // No early return on an empty local list any more. An empty list is
            // precisely when the plaza might be holding something this console
            // cannot name, and refusing here is what made that unrecoverable.
            // Asked about, unlike before. It is not destructive - nothing is
            // lost by letting somebody through - but it undoes every one of
            // these decisions at once, and they were made one at a time.
            size_t count = settings.blocked.size();
            std::string question = count == 0
                ? std::string("Clear any block the plaza still has?")
                : format("Clear all %zu?", count);
            std::string detail = count == 0
                ? std::string("This console has none listed, but the plaza keeps its own "
                              "copy and a restored backup can leave the two disagreeing. "
                              "Nothing happens if it has none either.")
                : std::string("Every console you have blocked can cross you again, except "
                              "any that blocked you as well. Their old passes are not "
                              "coming back; only the block is lifted.");
            app.askConfirm(question, detail, "Clear them", [appPtr = &app]() {
                // Asked by owner, not by id: the list on this card may be
                // missing entries the plaza still holds, and those are the ones
                // this is for.
                appPtr->sync().unblockAllPeers();
                appPtr->store().unblockAll();
                appPtr->toast("Block list cleared",
                    "This console has none left, and the plaza has been asked to drop "
                    "the blocks it was holding for you.");
            });
            return;
        }
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

    // A row's hint, scrolled sideways when it does not fit and the cursor is
    // on it, ellipsized when it is not.
    //
    // Ellipsis on an unfocused row is the right default.
    // The focused row is the one being read, and it is the only one whose whole
    // sentence matters.
    void drawHint(Renderer& r, const Rect& box, const std::string& text,
        const TextStyle& style, bool focused)
    {
        // Measured only for the row that could scroll. ellipsize() already
        // walks the string, and doing both for every row on screen would be
        // two passes over text nobody is reading.
        if (!focused) {
            r.text(box.x, box.y, r.ellipsize(text, style, box.w), style);
            return;
        }
        float width = r.measure(text, style);
        if (width <= box.w) {
            r.text(box.x, box.y, text, style);
            return;
        }

        // Out and back rather than wrapping round: a marquee that jumps to the
        // start is read as a glitch, and the return trip costs nothing because
        // the eye is already following the text.
        const float speed = 90.0f;   // design pixels a second
        const float rest = 1.4f;     // held still at each end, to be read
        float over = width - box.w;
        float travel = over / speed;
        float cycle = (rest + travel) * 2.0f;
        float t = std::fmod(m_marquee, cycle);

        float shift;
        if (t < rest)
            shift = 0.0f;
        else if (t < rest + travel)
            shift = (t - rest) * speed;
        else if (t < rest + travel + rest)
            shift = over;
        else
            shift = over - (t - rest - travel - rest) * speed;

        // Clipped to the room the hint has, so it slides under the value on the
        // right rather than across it.
        r.pushClipHorizontal(box);
        r.text(box.x - shift, box.y, text, style);
        r.popClip();
    }

    void drawRow(App& app, Renderer& r, const Rect& box, const Row& row, int index)
    {
        // No zone at all when disabled, so a tap falls through to nothing
        // rather than landing on a row that will not answer.
        if (row.enabled)
            app.touchZone(box, Zone_Row, row.id);

        bool focused = !m_inSidebar && index == m_focus;
        float focus = !row.enabled
            ? 0.0f
            : (app.touchHeld(Zone_Row, row.id) ? 1.0f
                                               : (focused ? 0.7f + 0.3f * m_pulse : 0.0f));

        bool danger = row.kind == Kind::Danger;
        Color fill = danger ? theme::dangerTint : (focused ? theme::bg2 : theme::bg1);
        ui::card(r, box, focus, fill, theme::r3);

        Rect inner = box.inset(theme::s6, theme::s5);

        TextStyle label;
        label.size = theme::textBase;
        label.weight = FontWeight::Bold;
        // Still focusable while disabled: the cursor has to be able to reach it
        // to read the hint saying why it will not work.
        label.color = !row.enabled ? theme::fg3
                                   : (danger ? theme::danger : theme::fg1);
        r.text(inner.x, inner.y, r.ellipsize(row.label, label, inner.w * 0.6f), label);

        if (!row.hint.empty()) {
            TextStyle hint;
            hint.size = theme::textSm;
            hint.color = theme::fg3;
            float hintY = inner.y + label.size * theme::leadingSnug + 6.0f;
            float room = inner.w * 0.6f;
            drawHint(r, Rect { inner.x, hintY, room, r.lineHeight(hint) }, row.hint, hint,
                focused);
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
    float m_marquee = 0.0f;   // seconds the cursor has sat on its row
    int m_marqueeRow = -2;    // -1 is the sidebar; -2 is "nothing yet"
    bool m_inSidebar = true; // the section list has the cursor on arrival
    float m_pulse = 0.0f;


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
