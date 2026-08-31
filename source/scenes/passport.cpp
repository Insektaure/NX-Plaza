#include "app.h"
#include "core/identity.h"
#include "core/place.h"
#include "core/play_history.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {

// Your pass, and the editor for it.
//
// Two columns with --space-9 between them: on the left the pass itself, a
// 520x700 card at radius-5 with the portrait behind a bottom veil and the
// name, greeting and chips sitting on it; on the right the handful of things
// you can change, then two stat cards and the privacy note.
class PassportScene final : public Scene {
public:
    // The rows of the editor column. The Mii is not among them: the pass card
    // on the left *is* the Mii, so it is focused and opened directly rather
    // than through a row that describes it.
    enum Row : int {
        Row_Handle = 0,
        Row_Greeting,
        Row_Theme,
        Row_Carrying,
        Row_Playing,
        Row_Count
    };

    enum Zone : int {
        Zone_Row = Touch_SceneBase,
        Zone_Card,
        Zone_Swatch, // index = the theme
    };

    void onEnter(App& app) override
    {
        m_pass = app.store().myPass();

        // Asks for the play history rather than waiting for it. Reading it
        // means pulling a 128 KB icon per title out of storage, and doing that
        // here is what made this screen stall on the way in.
        beginPlayHistoryLoad();
        m_playingResolved = false;
    }

    // Called from update(), once the history has arrived.
    //
    // Defaults to the most recently played title the first time there is one to
    // default to. Deliberately not done at startup: this is the screen where the
    // choice is visible, and filling it in before the owner had ever seen it
    // would be publishing what they play without their having agreed to it.
    void resolvePlaying(App& app)
    {
        if (m_playingResolved || !playHistoryReady())
            return;
        m_playingResolved = true;

        std::vector<PlayedTitle> played = recentlyPlayed();
        if (played.empty())
            return;

        if (m_pass.playing.empty()) {
            m_playingIndex = 0;
            m_pass.playing = played.front().name;
            m_pass.hours = played.front().hours;
            commit(app);
            return;
        }

        // Find where the stored title sits in the list. It arrives clamped, so
        // the comparison is against the clamped form of each candidate rather
        // than the full name.
        m_playingIndex = 0;
        for (size_t i = 0; i < played.size(); i++) {
            Pass probe;
            probe.playing = played[i].name;
            probe.sanitize();
            if (probe.playing == m_pass.playing) {
                m_playingIndex = static_cast<int>(i);
                // Refreshed from the console rather than trusted from the card:
                // the number grows every time the game is opened.
                if (m_pass.hours != played[i].hours) {
                    m_pass.hours = played[i].hours;
                    commit(app);
                }
                break;
            }
        }
    }

    void update(App& app, const Input& input, float dt) override
    {
        m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
        resolvePlaying(app);

        TouchTarget tap;
        if (!m_brakedTap && app.takeTap(tap)) {
            if (tap.is(Zone_Swatch)) {
                m_onCard = false;
                m_selected = Row_Theme;
                m_pass.theme = static_cast<uint32_t>(tap.index) % 6;
                commit(app);
                return;
            }
            if (tap.is(Zone_Row) && tap.index < static_cast<int>(Row_Count)) {
                m_onCard = false;
                m_selected = tap.index;
                activate(app);
                return;
            }
            if (tap.is(Zone_Card)) {
                m_onCard = true;
                openEditor(app);
                return;
            }
        }

        const Touch& touch = input.touch;
        if (touch.pressed)
            m_brakedTap = m_scroll.absorbPress();
        if (touch.down && touch.dragged && m_listArea.contains(touch.startX, touch.startY)) {
            m_scroll.drag(-touch.dy, dt);
            m_dragging = true;
        } else if (m_dragging && !touch.down) {
            m_scroll.release();
            m_dragging = false;
        }
        m_scroll.update(dt);

        // The two columns sit side by side, so left and right are what moves
        // between them. Up and down stay inside the list.
        if (input.navLeft)
            m_onCard = true;
        if (input.navRight)
            m_onCard = false;

        if (!m_onCard && (input.navDown || input.navUp)) {
            m_selected = std::min(std::max(m_selected + (input.navDown ? 1 : -1), 0),
                static_cast<int>(Row_Count) - 1);
            m_scroll.centerOn(static_cast<float>(m_selected) * (kRowHeight + theme::s3)
                + kRowHeight * 0.5f);
        }

        if (input.accept()) {
            if (m_onCard)
                openEditor(app);
            else
                activate(app);
        }

        // X shuffles the face outright; the editor is for deciding.
        if (input.pressed(HidNpadButton_X)) {
            m_pass.setFace(Mii::random());
            commit(app);
        }
    }

    void draw(App& app, Renderer& r) override
    {
        Rect area = app.contentArea();
        // padding: 48px 64px; gap --space-9
        Rect content { area.x + theme::edge, area.y + theme::edgeTop,
            area.w - theme::edge * 2.0f, area.h - theme::edgeTop * 2.0f };

        // In the artboard this column has no width of its own - the title sets
        // it and the 520px card sits inside. Measuring it is what stops
        // "This is what they see" from running under the editor.
        TextStyle titleStyle = passTitleStyle();
        float leftWidth = std::max(kCardWidth, r.measure(kPassTitle, titleStyle));

        Rect left { content.x, content.y, leftWidth, content.h };
        Rect right { left.right() + theme::s9, content.y,
            content.right() - left.right() - theme::s9, content.h };

        drawPassColumn(app, r, left);
        drawEditor(app, r, right);
    }

private:
    static constexpr const char* kPassTitle = "This is what they see";
    static constexpr float kCardWidth = 520.0f;
    static constexpr float kCardHeight = 700.0f;
    static constexpr float kRowHeight = 108.0f; // --space-4 padding, two lines
    static constexpr float kSwatch = 44.0f;

    static TextStyle passTitleStyle()
    {
        TextStyle title;
        title.size = theme::text2xl;
        title.weight = FontWeight::Bold;
        title.color = theme::fg1;
        title.tracking = theme::trackingTight;
        return title;
    }

    void openEditor(App& app)
    {
        Pass* pass = &m_pass;
        App* appPtr = &app;
        app.pushOverlay(makeMiiEditorScene(m_pass.face(), [pass, appPtr](const Mii& face) {
            pass->setFace(face);
            pass->sanitize();
            appPtr->store().setMyPass(*pass);
            appPtr->store().flush();
            appPtr->sync().publishPass();
        }));
    }

    void commit(App& app)
    {
        m_pass.sanitize();
        app.store().setMyPass(m_pass);
        app.store().flush();
        app.sync().publishPass();
    }

    void activate(App& app)
    {
        std::string value;
        switch (m_selected) {
        case Row_Handle:
            if (app.textInput("The name on your pass", m_pass.handle, 16, value, false)) {
                m_pass.handle = value;
                commit(app);
            }
            break;
        case Row_Greeting:
            if (app.textInput("Up to 60 characters", m_pass.greeting, 60, value)) {
                m_pass.greeting = value;
                commit(app);
            }
            break;
        case Row_Theme:
            m_pass.theme = (m_pass.theme + 1) % 6;
            commit(app);
            break;
        case Row_Carrying: {
            // Picked from a list, not typed. The list is the same as the one in the plaza, so it is
            // not repeated here. The pass is the source of truth for what is carried, so it is
            // passed in and updated on return.
            PassportScene* self = this;
            App* appPtr = &app;
            app.pushOverlay(makeCarryScene(m_pass.carrying,
                [self, appPtr](std::vector<std::string> chosen) {
                    self->m_pass.carrying = std::move(chosen);
                    self->commit(*appPtr);
                }));
            break;
        }
        case Row_Playing: {
            // Cycles, the way the card theme does: five titles is a short list
            // and a picker for it would be a screen to open and close for one
            // choice.
            //
            // The position is held here rather than found by matching the
            // stored name against the list. A pass clamps `playing` to 32
            // characters on the way in, and plenty of real titles are longer
            // than that - so the stored string matched nothing, every press
            // fell through to the first entry, and any long title was a dead
            // end you could not cycle past.
            std::vector<PlayedTitle> played = recentlyPlayed();
            if (played.empty())
                break;

            m_playingIndex = (m_playingIndex + 1) % static_cast<int>(played.size());
            m_pass.playing = played[static_cast<size_t>(m_playingIndex)].name;
            // The hours belong to the title, so they move with it.
            m_pass.hours = played[static_cast<size_t>(m_playingIndex)].hours;
            commit(app);
            break;
        }
        default:
            break;
        }
    }

    static std::string joinList(const std::vector<std::string>& items)
    {
        std::string out;
        for (size_t i = 0; i < items.size(); i++) {
            if (i > 0)
                out += "; ";
            out += items[i];
        }
        return out;
    }

    static std::vector<std::string> splitList(const std::string& text)
    {
        std::vector<std::string> out;
        std::string current;
        for (char c : text) {
            if (c == ';') {
                std::string piece = trim(current);
                if (!piece.empty())
                    out.push_back(piece);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        std::string piece = trim(current);
        if (!piece.empty())
            out.push_back(piece);
        if (out.size() > 4)
            out.resize(4);
        return out;
    }

    // Eyebrow, title, then the pass card itself.
    void drawPassColumn(App& app, Renderer& r, const Rect& box)
    {
        TextStyle eyebrow;
        eyebrow.size = theme::textSm;
        eyebrow.weight = FontWeight::Bold;
        eyebrow.color = theme::accent;
        eyebrow.tracking = theme::trackingWider;
        eyebrow.uppercase = true;
        r.text(box.x, box.y, "your pass", eyebrow);

        TextStyle title = passTitleStyle();
        float titleY = box.y + theme::textSm * theme::leadingNormal + theme::s2;
        r.text(box.x, titleY, kPassTitle, title);

        Rect card { box.x, titleY + title.size * theme::leadingTight + theme::s6,
            kCardWidth, kCardHeight };
        app.touchZone(card, Zone_Card);
        drawPassCard(app, r, card);
    }

    void drawPassCard(App& app, Renderer& r, const Rect& box)
    {
        const theme::CardTheme& cardTheme = theme::cardTheme(m_pass.theme);

        // transform: scale(1.03) + focus-ring in the artboard: the pass is what
        // this screen is about, so it is always the lit element.
        Rect card = box.inset(-box.w * 0.015f, -box.h * 0.015f);

        r.pushClip(card);
        // 44% x 44% shoulders standing 230px off a 700px floor, head 24% wide
        // with its base at 56%. Without the offset the head
        // floated clear of the shoulders.
        ui::miiStage(r, card, m_pass.face(), m_pass.theme,
            ui::StageFigure { 0.44f, 0.44f, 0.24f, 0.56f, 230.0f / 700.0f, 0.12f },
            theme::r5, theme::r5);

        // A 300px veil, so the text below stays legible over the portrait.
        ui::veilBottom(r, Rect { card.x, card.bottom() - 300.0f, card.w, 300.0f }, theme::bg0,
            theme::r5);

        {
            TextStyle pillText;
            pillText.size = theme::textXs;
            pillText.weight = FontWeight::Bold;
            pillText.color = theme::fg2;
            pillText.tracking = theme::trackingWide;
            pillText.uppercase = true;

            std::string label = std::string(cardTheme.name) + " theme";
            float width = r.measure(label, pillText) + 32.0f;
            Rect pill { card.x + theme::s6, card.y + theme::s6, width,
                pillText.size * theme::leadingNormal + 12.0f };
            // rgba(8,8,10,.5) in the artboard. Taken literally that is a pale
            // plate on a pale stage in the light theme, so this uses the
            // surface colour, which separates in both.
            r.roundRect(pill, pill.h * 0.5f, theme::bg1.withAlpha(0.85f));
            r.text(pill, label, pillText, Align::Center, VAlign::Middle);
        }

        Rect body { card.x + theme::s6, card.y, card.w - theme::s6 * 2.0f, card.h };

        TextStyle name;
        name.size = theme::textXl;
        name.weight = FontWeight::Bold;
        name.color = theme::fg1;
        name.tracking = theme::trackingTight;

        TextStyle greeting;
        greeting.size = theme::textBase;
        greeting.color = theme::fg2;
        greeting.leading = theme::leadingSnug;

        std::string quote = m_pass.greeting.empty()
            ? std::string("(no greeting yet)")
            : "\xE2\x80\x9C" + m_pass.greeting + "\xE2\x80\x9D";
        float greetingHeight = r.measureWrapped(body.w, quote, greeting, 3);

        Settings settings = app.store().settings();
        std::vector<std::pair<std::string, bool>> chips; // text, is the activity
        if (settings.sharePlaying && !m_pass.playing.empty())
            chips.emplace_back(m_pass.playing, false);
        std::string place = settings.sharedPlaceLabel();
        if (!place.empty())
            chips.emplace_back(place, false);
        if (!m_pass.activity.empty())
            chips.emplace_back(m_pass.activity, true);

        TextStyle chipStyle;
        chipStyle.size = theme::textXs;
        chipStyle.color = theme::fg2;
        float chipHeight = chipStyle.size * theme::leadingNormal + 12.0f;

        float blockHeight = name.size * theme::leadingTight + theme::s3 + greetingHeight
            + (chips.empty() ? 0.0f : theme::s2 + chipHeight);
        float blockTop = card.bottom() - theme::s6 - blockHeight;

        r.text(body.x, blockTop, r.ellipsize(m_pass.handle, name, body.w), name);
        float greetingY = blockTop + name.size * theme::leadingTight + theme::s3;
        r.textWrapped(Rect { body.x, greetingY, body.w, greetingHeight }, quote, greeting, 3);

        float chipX = body.x;
        float chipY = greetingY + greetingHeight + theme::s2;
        for (const auto& entry : chips) {
            float width = r.measure(entry.first, chipStyle) + 28.0f;
            if (chipX > body.x && chipX + width > body.right()) {
                chipX = body.x;
                chipY += chipHeight + 10.0f;
            }
            Color background = entry.second ? theme::tealTint : theme::fg1.withAlpha(0.08f);
            Color text = entry.second ? theme::teal : theme::fg2;
            ui::pill(r, Rect { chipX, chipY, width, chipHeight }, entry.first, text, background,
                theme::textXs);
            chipX += width + 10.0f;
        }

        r.popClip();

        // The artboard keeps the pass lit at all times - it is what the screen
        // is about - but the card is now also a target you move onto and press
        // A on, so the ring has to say which of those it is doing.
        float focus = app.touchHeld(Zone_Card)
            ? 1.0f
            : (m_onCard ? 0.75f + 0.25f * m_pulse : 0.4f);
        ui::focusRing(r, card, theme::r5, focus);
    }

    // The editor: rows, then two stat cards, then the privacy note.
    void drawEditor(App& app, Renderer& r, const Rect& box)
    {
        // The two rows that cycle say so. "Edit" on a row that used to open a
        // keyboard and now steps through a list is a button that does something
        // other than what it says.
        const char* accept = "edit";
        if (m_onCard)
            accept = "edit your mii";
        else if (m_selected == Row_Theme)
            accept = "next theme";
        else if (m_selected == Row_Playing)
            accept = recentlyPlayed().empty() ? "-" : "next title";

        app.hint("A", accept);
        // Crossing between the card and this column is the one thing on the
        // screen that is not obvious from looking at it, so it is always named.
        app.hint(m_onCard ? "Right" : "Left", m_onCard ? "your details" : "your mii");
        app.hint("X", "shuffle face");

        struct RowSpec {
            const char* label;
            std::string hint;
        };

        Settings settings = app.store().settings();
        const theme::CardTheme& cardTheme = theme::cardTheme(m_pass.theme);

        RowSpec rows[Row_Count];
        rows[Row_Handle] = { "Name", m_pass.handle.empty() ? "not set yet" : m_pass.handle };
        rows[Row_Greeting] = { "Greeting",
            format("%zu of 60 characters", utf8Length(m_pass.greeting)) };
        rows[Row_Theme] = { "Card theme", format("%s - 6 unlocked", cardTheme.name) };
        rows[Row_Carrying] = { "What you carry",
            m_pass.carrying.empty() ? std::string("nothing yet") : joinList(m_pass.carrying) };
        std::vector<PlayedTitle> played = recentlyPlayed();
        std::string playingValue;
        if (!settings.sharePlaying)
            playingValue = "hidden by privacy settings";
        else if (!playHistoryReady())
            playingValue = "reading the play history...";
        else if (played.empty())
            playingValue = "nothing played on this console yet";
        else if (m_pass.playing.empty())
            playingValue = "hidden";
        else if (m_pass.hours > 0)
            // The hours travel with the title, so the row shows both: this is
            // what the other console's card will read.
            playingValue = format("%s - %uh", m_pass.playing.c_str(), m_pass.hours);
        else
            playingValue = m_pass.playing;
        rows[Row_Playing] = { "Title on your pass", playingValue };

        // The artboard offsets this column by 150px so its three rows line up
        // under the pass card's title block. This editor has five - the name
        // and the advertised title are editable here too - so instead it starts
        // level with the left column and scrolls, the same way the settings pane
        // does.
        m_listArea = box;
        float total = static_cast<float>(Row_Count) * (kRowHeight + theme::s3)
            + theme::s3 + 140.0f + theme::s5 + 110.0f;
        m_scroll.setBounds(box.h, std::max(0.0f, total));

        r.pushClipVertical(box.inset(0.0f, -theme::focusRoom));
        float y = box.y - m_scroll.offset();

        for (int i = 0; i < Row_Count; i++) {
            Rect row { box.x, y, box.w, kRowHeight };
            app.touchZone(row, Zone_Row, i);

            // The cursor is either on the card or in this list, never both, so a
            // row goes quiet the moment focus crosses to the pass.
            bool focused = !m_onCard && i == m_selected;
            float focus = app.touchHeld(Zone_Row, i)
                ? 1.0f
                : (focused ? 0.7f + 0.3f * m_pulse : 0.0f);

            // bg-1 at rest, bg-2 once it is the focused row.
            ui::card(r, row, focus, focused ? theme::bg2 : theme::bg1, theme::r3);

            Rect inner = row.inset(theme::s6, theme::s4);

            TextStyle label;
            label.size = theme::textBase;
            label.weight = FontWeight::Bold;
            label.color = theme::fg1;
            r.text(inner.x, inner.y, rows[i].label, label);

            TextStyle hint;
            hint.size = theme::textSm;
            hint.color = theme::fg3;
            r.text(inner.x, inner.y + label.size * theme::leadingSnug + 6.0f,
                r.ellipsize(rows[i].hint, hint, inner.w * 0.62f), hint);

            if (i == Row_Theme)
                drawSwatches(app, r, inner);
            else
                ui::icon(r,
                    Rect { inner.right() - 40.0f, inner.centerY() - 20.0f, 40.0f, 40.0f },
                    ui::Icon::Chevron, theme::fg2, 3.0f);

            y += kRowHeight + theme::s3;
        }

        float statY = y + theme::s3;
        float statWidth = (box.w - theme::s4) * 0.5f;

        // How many times this console's pass has gone to somebody, whether we
        // pushed it or they pulled it.
        //
        // Read from the store rather than from Sync::Status. The status is
        // memory only: zero until a check-in answers, and zero for the whole
        // session with no network - so the tile would tell somebody who has
        // traded for weeks that their pass had never been sent. The store
        // remembers the last figure the server gave, and the sync worker
        // updates it whenever a reply carries one.
        //
        // The label used to read "people have your pass", which this number
        // never measured: it counts hand-offs and never comes down, so crossing
        // one friend ten times read as ten people. How many people you have met
        // is the collection's business, and the collection screen says it.
        ui::statCard(r, Rect { box.x, statY, statWidth, 140.0f },
            ui::groupedNumber(app.store().passesSent()), "times your pass was sent",
            theme::text2xl);
        ui::statCard(r, Rect { box.x + statWidth + theme::s4, statY, statWidth, 140.0f },
            identity().shortCode(), "your pairing code", theme::text2xl);

        Rect note { box.x, statY + 140.0f + theme::s5, box.w, 110.0f };
        r.roundRect(note, theme::r3, theme::bg0);
        r.strokeRect(note, theme::r3, theme::stroke, theme::stroke2);

        Rect noteInner = note.inset(theme::s6, theme::s5);
        ui::icon(r, Rect { noteInner.x, noteInner.y, 36.0f, 36.0f }, ui::Icon::Check,
            theme::accent, 3.0f);

        TextStyle noteText;
        noteText.size = theme::textSm;
        noteText.color = theme::fg2;
        noteText.leading = theme::leadingSnug;
        r.textWrapped(Rect { noteInner.x + 36.0f + theme::s4, noteInner.y,
                          noteInner.w - 36.0f - theme::s4, noteInner.h },
            "Your pass never carries your account name, your friend code, or a precise "
            "position - only what you typed and, if you allow it, the place you named.",
            noteText, 3);

        r.popClip();

        if (m_scroll.scrollable())
            ui::scrollbar(r, Rect { box.right() + theme::s3, box.y, 8.0f, box.h },
                m_scroll.progress(), m_scroll.visibleFraction());


    }

    // Three 44px theme swatches, the current one ringed in accent.
    void drawSwatches(App& app, Renderer& r, const Rect& inner)
    {
        float gap = 10.0f;
        int shown = 3;
        float total = shown * kSwatch + (shown - 1) * gap;
        float x = inner.right() - total;

        // Centred on the current theme so it is always one of the three shown.
        int first = static_cast<int>(m_pass.theme) - 1;
        for (int i = 0; i < shown; i++) {
            uint32_t index = static_cast<uint32_t>((first + i + 6) % 6);
            Rect swatch { x, inner.centerY() - kSwatch * 0.5f, kSwatch, kSwatch };
            app.touchZone(swatch, Zone_Swatch, static_cast<int>(index));

            Color top, bottom;
            ui::stageGradient(index, top, bottom);
            r.gradientRect(swatch, theme::cardTheme(index).tint.mix(bottom, 0.45f), bottom,
                10.0f);

            if (index == m_pass.theme)
                r.strokeRect(swatch.inset(-4.0f), 14.0f, 3.0f, theme::accent);

            x += kSwatch + gap;
        }
    }

    Pass m_pass;
    int m_selected = 0;
    // Which of the recently-played titles is chosen. Held here because the
    // stored name is clamped and so cannot be matched back to the list.
    int m_playingIndex = 0;
    bool m_playingResolved = false;
    // Whether the pass card, rather than a row of the editor, has the focus.
    bool m_onCard = true;
    float m_pulse = 0.0f;

    ui::ScrollView m_scroll;
    Rect m_listArea;
    bool m_dragging = false;
    bool m_brakedTap = false;
};

} // namespace

std::unique_ptr<Scene> makePassportScene()
{
    return std::unique_ptr<Scene>(new PassportScene());
}

} // namespace nxp
