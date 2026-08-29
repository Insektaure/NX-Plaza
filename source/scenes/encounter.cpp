#include "app.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {

// One pass, opened. Mockup 1b docked, 3b handheld.
//
// Full-bleed, not a sheet: the left 860px is the portrait, on its own gradient,
// veiled into the canvas on its right edge so it reads as a photograph the
// text sits beside. Everything else is a column starting at x=860 with
// --space-9 padding: the header, the greeting in its own bg-2 card, the
// carrying chips, three stat cards, and the actions pinned to the bottom.
class EncounterScene final : public Scene {
public:
    enum Zone : int {
        Zone_Screen = Touch_SceneBase, // swallows taps; this screen is opaque
        Zone_Primary,
        Zone_Secondary,
        Zone_Block,
    };

    EncounterScene(std::string id, std::vector<std::string> siblings)
        : m_id(std::move(id))
        , m_siblings(std::move(siblings))
    {
        for (size_t i = 0; i < m_siblings.size(); i++) {
            if (m_siblings[i] == m_id) {
                m_index = static_cast<int>(i);
                break;
            }
        }
    }

    void onEnter(App& app) override { load(app); }

    // Reads the crossing this view is on, and marks it read.
    void load(App& app)
    {
        if (!app.store().findCrossing(m_id, m_crossing)) {
            m_missing = true;
            return;
        }
        m_missing = false;
        app.store().markOpened(m_id);
        m_crossing.opened = true;
        m_action = 0;
    }

    // Leaves, remembering which pass this ended on so the list behind can put
    // its cursor there. Every ordinary way out goes through here; blocking does
    // not, because the pass it was on is the one being thrown away.
    void close(App& app)
    {
        app.setLastViewedCrossing(m_id);
        app.popOverlay();
    }

    // Steps to the next or previous pass without going back to the grid.
    void step(App& app, int delta)
    {
        if (m_index < 0 || m_siblings.size() < 2)
            return;

        int count = static_cast<int>(m_siblings.size());
        for (int tries = 0; tries < count; tries++) {
            m_index = (m_index + delta + count) % count;
            Crossing probe;
            if (app.store().findCrossing(m_siblings[static_cast<size_t>(m_index)], probe)) {
                m_id = m_siblings[static_cast<size_t>(m_index)];
                load(app);
                return;
            }
        }
    }

    void update(App& app, const Input& input, float dt) override
    {
        m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

        if (m_missing || input.back()) {
            close(app);
            return;
        }

        TouchTarget tap;
        if (app.takeTap(tap)) {
            if (tap.is(Zone_Primary)) {
                m_action = 0;
                trade(app);
                return;
            }
            if (tap.is(Zone_Secondary)) {
                close(app);
                return;
            }
            if (tap.is(Zone_Block)) {
                askBlock(app);
                return;
            }
        }

        if (input.pressed(HidNpadButton_X)) {
            askBlock(app);
            return;
        }

        if (input.navLeft)
            m_action = std::max(m_action - 1, 0);
        if (input.navRight)
            m_action = std::min(m_action + 1, 2);

        if (input.accept()) {
            if (m_action == 2)
                askBlock(app);
            else if (m_action == 1)
                close(app);
            else
                trade(app);
            return;
        }

        // Y is the face button everywhere it appears - the Mii maker's save and
        // load is on Y, so keeping somebody else's face is too.
        if (input.pressed(HidNpadButton_R) || input.pressed(HidNpadButton_ZR)) {
            step(app, 1);
            return;
        }
        if (input.pressed(HidNpadButton_L) || input.pressed(HidNpadButton_ZL)) {
            step(app, -1);
            return;
        }

        if (input.pressed(HidNpadButton_Y)) {
            promptSaveMii(app, m_crossing.pass.face(), m_crossing.pass.handle);
            return;
        }
    }

    // The artboard is a whole screen, so it owns the chrome too.
    bool coversChrome() const override { return true; }

    void draw(App& app, Renderer& r) override
    {
        if (m_missing)
            return;

        r.clear(theme::bg0);
        app.touchZone(r.viewport(), Touch_None);

        app.hint("A", m_action == 2 ? "block" : (m_action == 1 ? "keep" : "trade back"));
        app.hint("B", "back");
        if (m_siblings.size() > 1)
            app.hint("L/R", "next pass");
        app.hint("X", "block");
        app.hint("Y", "save face");

        // Full bleed, minus the control strip the chrome owns.
        Rect screen = app.contentArea();
        screen.x = 0.0f;
        screen.w = Renderer::DesignWidth;

        // left:0 top:0 bottom:0 width:860px
        constexpr float portraitWidth = 860.0f;
        drawPortrait(r, Rect { screen.x, screen.y, portraitWidth, screen.h });

        // The artboard pads this column --space-9 top and --space-8 bottom. The
        // control strip now sits under it and does the work that bottom padding
        // was doing, so both come in one step: a full pass needs 843 of the 848
        // that leaves.
        Rect body { portraitWidth + theme::s9, theme::s8,
            screen.w - portraitWidth - theme::s9 - theme::edge,
            screen.h - theme::s8 - theme::s7 };

        drawHeader(r, body);
        float y = body.y + headerHeight() + theme::s6;

        if (!m_crossing.pass.greeting.empty()) {
            y += drawGreetingCard(r, Rect { body.x, y, body.w, 0.0f }) + theme::s6;
        }
        if (!m_crossing.pass.carrying.empty()) {
            y += drawCarrying(r, Rect { body.x, y, body.w, 0.0f }) + theme::s6;
        }

        drawStats(r, Rect { body.x, y, body.w, kStatsHeight });
        drawActions(app, r, Rect { body.x, body.bottom() - kActionHeight, body.w,
                               kActionHeight });
    }

private:
    static constexpr float kActionHeight = 76.0f;  // padding 20px + --text-base
    static constexpr float kStatCardHeight = 132.0f;
    // Two rows: three numbers, then the title with the whole width to itself.
    static constexpr float kStatsHeight = kStatCardHeight * 2.0f + theme::s4;
    static constexpr float kChipHeight = 56.0f;    // padding 10px 18px

    void trade(App& app)
    {
        if (!m_crossing.tradedBack) {
            app.store().markTradedBack(m_id);
            m_crossing.tradedBack = true;
            app.sync().publishPass();
            app.toast(format("Sent something back to %s", m_crossing.pass.handle.c_str()),
                "It travels with your pass the next time you cross.");
        } else {
            close(app);
        }
    }

    void askBlock(App& app)
    {
        std::string handle = m_crossing.pass.handle;
        std::string id = m_id;
        app.askConfirm(format("Block %s?", handle.c_str()),
            "Their pass is deleted and this console can never cross you again. They are "
            "not told.",
            "Block and forget",
            [appPtr = &app, id]() {
                appPtr->store().block(id);
                appPtr->sync().blockPeer(id);
                appPtr->popOverlay();
            });
    }

    static constexpr float kNameSize = theme::text2xl;

    static float headerHeight()
    {
        return theme::textSm * theme::leadingNormal + theme::s3
            + kNameSize * theme::leadingTight + theme::s3
            + theme::textSm * theme::leadingNormal;
    }

    // The portrait panel: its own gradient, a big silhouette, and a veil on
    // the right edge that dissolves it into the canvas.
    void drawPortrait(Renderer& r, const Rect& pane)
    {
        ui::miiStage(r, pane, m_crossing.pass.face(), m_crossing.pass.theme,
            ui::StageFigure { 0.46f, 0.60f, 0.26f, 0.54f, 0.0f, 0.17f });

        // linear-gradient(90deg, transparent 55%, rgba(8,8,10,.9) 92%, #08080A)
        ui::veilRight(r, pane, theme::bg0);

        TextStyle label;
        label.size = theme::textXs;
        label.color = theme::fg4;
        label.tracking = theme::trackingWide;
        label.uppercase = true;
        r.text(pane.x + theme::edge, pane.bottom() - theme::edgeTop - label.size,
            theme::cardTheme(m_crossing.pass.theme).name, label);
    }

    void drawHeader(Renderer& r, const Rect& body)
    {
        TextStyle eyebrow;
        eyebrow.size = theme::textSm;
        eyebrow.weight = FontWeight::Bold;
        eyebrow.color = theme::accent;
        eyebrow.tracking = theme::trackingWider;
        eyebrow.uppercase = true;
        r.text(body.x, body.y,
            format("crossed paths - %s", relativeTime(m_crossing.lastSeen, nowUnix()).c_str()),
            eyebrow);

        TextStyle name;
        name.size = kNameSize;
        name.weight = FontWeight::Bold;
        name.color = theme::fg1;
        name.tracking = theme::trackingTight;
        name.leading = theme::leadingTight;

        float nameY = body.y + theme::textSm * theme::leadingNormal + theme::s3;
        r.text(body.x, nameY, r.ellipsize(m_crossing.pass.handle, name, body.w), name);

        // "Namba Station, Osaka · 4th crossing · playing Turnip Grove"
        std::string meta;
        auto add = [&meta](const std::string& part) {
            if (part.empty())
                return;
            if (!meta.empty())
                meta += " - ";
            meta += part;
        };
        add(m_crossing.place);
        // How many times is a stat, not a caption: it sits with the other
        // numbers below rather than in a line about where they were.
        if (!m_crossing.pass.playing.empty())
            add("playing " + m_crossing.pass.playing);
        add(m_crossing.pass.activity);
        if (meta.empty())
            meta = "somewhere on the network";

        TextStyle metaStyle;
        metaStyle.size = theme::textSm;
        metaStyle.weight = FontWeight::Medium;
        metaStyle.color = theme::fg3;
        r.text(body.x, nameY + name.size * theme::leadingTight + theme::s3,
            r.ellipsize(meta, metaStyle, body.w), metaStyle);
    }

    // The greeting is a card in the artboard: bg-2, radius-4, --space-6
    // padding, display type at --text-lg.
    float drawGreetingCard(Renderer& r, const Rect& box)
    {
        TextStyle quote;
        quote.size = theme::textLg;
        quote.color = theme::fg1;
        quote.leading = theme::leadingSnug;

        std::string text = "\xE2\x80\x9C" + m_crossing.pass.greeting + "\xE2\x80\x9D";
        float inner = box.w - theme::s6 * 2.0f;
        float textHeight = r.measureWrapped(inner, text, quote, 3);
        float height = textHeight + theme::s6 * 2.0f;

        r.roundRect(Rect { box.x, box.y, box.w, height }, theme::r4, theme::bg2);
        r.textWrapped(Rect { box.x + theme::s6, box.y + theme::s6, inner, textHeight },
            text, quote, 3);
        return height;
    }

    // "carrying" plus wrapping chips. The artboard tints the first two by what
    // they are; here the pass's card theme colours the first, and the rest are
    // neutral, so the row stays legible whatever a pass carries.
    float drawCarrying(Renderer& r, const Rect& box)
    {
        TextStyle label;
        label.size = theme::textSm;
        label.weight = FontWeight::Bold;
        label.color = theme::fg3;
        label.tracking = theme::trackingWide;
        label.uppercase = true;
        r.text(box.x, box.y, "carrying", label);

        float top = box.y + theme::textSm * theme::leadingNormal + theme::s4;
        float x = box.x;
        float y = top;
        const Color tints[3] = { theme::cardTheme(m_crossing.pass.theme).tint, theme::teal,
            theme::fg2 };

        for (size_t i = 0; i < m_crossing.pass.carrying.size(); i++) {
            const std::string& item = m_crossing.pass.carrying[i];
            float width = std::min(ui::chipWidth(r, item), box.w);
            if (x > box.x && x + width > box.right()) {
                x = box.x;
                y += kChipHeight + theme::s3;
            }
            ui::chip(r, Rect { x, y, width, kChipHeight }, item, tints[std::min(i, size_t(2))]);
            x += width + theme::s3;
        }

        return (y + kChipHeight) - box.y;
    }

    // grid-template-columns: repeat(3, 1fr); gap --space-4
    void drawStats(Renderer& r, const Rect& box)
    {
        float gap = theme::s4;

        // Three across, then the title on a row of its own.
        float width = (std::min(box.w, 1000.0f) - gap * 2.0f) / 3.0f;

        // The one number here about the two of you rather than about them.
        std::string crossed = format("%u", m_crossing.count);
        const char* crossedCaption = m_crossing.count == 1 ? "time crossed" : "times crossed";

        ui::statCard(r, Rect { box.x, box.y, width, kStatCardHeight },
            crossed, crossedCaption);
        ui::statCard(r, Rect { box.x + width + gap, box.y, width, kStatCardHeight },
            format("%zu", m_crossing.pass.games.size()), "games they carry");
        ui::statCard(r, Rect { box.x + (width + gap) * 2.0f, box.y, width, kStatCardHeight },
            ui::groupedNumber(m_crossing.pass.met), "people met");

        std::string hours = m_crossing.pass.hours > 0
            ? format("%uh", m_crossing.pass.hours)
            : std::string("-");
        std::string caption = m_crossing.pass.playing.empty()
            ? std::string("hours played")
            : format("in %s", m_crossing.pass.playing.c_str());

        ui::statCard(r, Rect { box.x, box.y + kStatCardHeight + gap,
                         std::min(box.w, 1000.0f), kStatCardHeight },
            hours, caption);
    }

    // Primary, outline, and a square icon button, gap --space-4.
    void drawActions(App& app, Renderer& r, const Rect& box)
    {
        std::string primaryLabel = m_crossing.tradedBack
            ? "Already traded back"
            : (m_crossing.pass.carrying.empty()
                    ? "Trade something back"
                    : format("Take it, send something back"));

        float primaryWidth = std::min(ui::actionButtonWidth(r, primaryLabel), box.w * 0.5f);
        float secondaryWidth = ui::actionButtonWidth(r, "Keep the pass");

        Rect primary { box.x, box.y, primaryWidth, box.h };
        Rect secondary { primary.right() + theme::s4, box.y, secondaryWidth, box.h };
        Rect block { secondary.right() + theme::s4, box.y, box.h, box.h };

        app.touchZone(primary, Zone_Primary);
        app.touchZone(secondary, Zone_Secondary);
        app.touchZone(block, Zone_Block);

        float pulse = 0.7f + 0.3f * m_pulse;
        bool primaryHeld = app.touchHeld(Zone_Primary);
        bool secondaryHeld = app.touchHeld(Zone_Secondary);
        bool blockHeld = app.touchHeld(Zone_Block);

        ui::actionButton(r, primary, primaryLabel, !m_crossing.tradedBack,
            (m_action == 0 || primaryHeld) ? pulse : 0.0f);
        ui::actionButton(r, secondary, "Keep the pass", false,
            (m_action == 1 || secondaryHeld) ? pulse : 0.0f);

        r.roundRect(block, theme::r2, theme::bg1);
        r.strokeRect(block, theme::r2, theme::stroke, theme::stroke3);
        ui::icon(r, block.inset(theme::s4), ui::Icon::Block, theme::fg2, 3.0f);
        ui::focusRing(r, block, theme::r2, (m_action == 2 || blockHeld) ? pulse : 0.0f);


    }


    std::string m_id;
    // The list this view was opened from, in the order it was on screen, and
    // where we are in it. Empty when there is nothing to step through.
    std::vector<std::string> m_siblings;
    int m_index = -1;
    Crossing m_crossing;
    bool m_missing = false;
    int m_action = 0;
    float m_pulse = 0.0f;
};

} // namespace

std::unique_ptr<Scene> makeEncounterScene(const std::string& crossingId,
    std::vector<std::string> siblings)
{
    return std::unique_ptr<Scene>(new EncounterScene(crossingId, std::move(siblings)));
}

} // namespace nxp
