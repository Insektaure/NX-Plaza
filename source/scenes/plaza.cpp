#include "app.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
#include "ui/mii_render.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {

// The inbox.
//
// Geometry is transcribed from those artboards rather than approximated: the
// hero band is 520px with a six-figure crowd standing 88px off its floor, and
// the passes below are 300x404 vertical tiles - a 250px "photo" stage with the
// silhouette in it, then name, title and place.
class PlazaScene final : public Scene {
public:
    enum Zone : int {
        Zone_Card = Touch_SceneBase,
        Zone_More,
    };

    void onEnter(App& app) override
    {
        m_crossings = app.store().crossings();
        clampSelection();
    }

    void update(App& app, const Input& input, float dt) override
    {
        m_crossings = app.store().crossings();
        clampSelection();

        int count = static_cast<int>(m_crossings.size());
        const Touch& touch = input.touch;

        if (touch.pressed)
            m_brakedTap = m_scroll.absorbPress();

        if (touch.down && touch.dragged && m_stripRect.contains(touch.startX, touch.startY)) {
            m_scroll.drag(-touch.dx, dt);
            m_dragging = true;
        } else if (m_dragging && !touch.down) {
            m_scroll.release();
            clampSelectionToView();
            m_dragging = false;
        }
        m_scroll.update(dt);

        TouchTarget tap;
        if (!m_brakedTap && app.takeTap(tap) && tap.is(Zone_Card) && tap.index < count) {
            m_selected = tap.index;
            app.openEncounter(m_crossings[static_cast<size_t>(m_selected)].id);
            return;
        }

        if (count > 0) {
            int before = m_selected;
            if (input.navRight)
                m_selected = std::min(m_selected + 1, count - 1);
            if (input.navLeft)
                m_selected = std::max(m_selected - 1, 0);
            if (m_selected != before)
                revealSelection();

            if (input.accept())
                app.openEncounter(m_crossings[static_cast<size_t>(m_selected)].id);
        }

        if (input.pressed(HidNpadButton_X)) {
            app.sync().kick();
            app.toast("Looking for passes", "Asking the plaza who else has been around.");
        }

        if (input.pressed(HidNpadButton_Y) && count > 0) {
            app.store().markAllOpened();
            m_crossings = app.store().crossings();
        }

        m_focusPulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
    }

    void draw(App& app, Renderer& r) override
    {
        Rect area = app.contentArea();

        // Band is 520 of 1080. The control strip takes 88 from
        // the bottom, and the 404px tiles are data rather than decoration, so
        // the band is what gives up the difference.
        Rect band { area.x, area.y, area.w, theme::plazaHeight - 40.0f };
        drawPlazaBand(app, r, band);

        drawPasses(app, r, Rect { area.x, band.bottom(), area.w, area.h - band.h });
    }

private:
    // --- the artboards' own numbers -----------------------------------------
    static constexpr float kTileWidth = 300.0f;
    static constexpr float kTileStage = 250.0f;  // the "photo" area
    static constexpr float kTileHeight = 404.0f; // stage plus the text block
    static constexpr float kTileGap = theme::s5;
    static constexpr float kMoreWidth = 170.0f;

    void clampSelection()
    {
        int count = static_cast<int>(m_crossings.size());
        m_selected = count == 0 ? 0 : std::min(std::max(m_selected, 0), count - 1);
    }

    void revealSelection()
    {
        m_scroll.centerOn(static_cast<float>(m_selected) * (kTileWidth + kTileGap)
            + kTileWidth * 0.5f);
    }

    void clampSelectionToView()
    {
        if (m_crossings.empty())
            return;
        float middle = m_scroll.offset() + m_scroll.viewport() * 0.5f;
        int index = static_cast<int>(std::floor((middle - kTileWidth * 0.5f)
            / (kTileWidth + kTileGap) + 0.5f));
        m_selected = std::min(std::max(index, 0), static_cast<int>(m_crossings.size()) - 1);
    }

    // The hero band: a three-stop gradient, an amber pool on the floor, and
    // the crowd standing in it.
    void drawPlazaBand(App& app, Renderer& r, const Rect& band)
    {
        // linear-gradient(180deg, #1A1520 0%, #121016 60%, #08080A 100%)
        r.gradientRect(Rect { band.x, band.y, band.w, band.h * 0.6f },
            theme::plazaTop, theme::plazaMid);
        r.gradientRect(Rect { band.x, band.y + band.h * 0.6f, band.w, band.h * 0.4f },
            theme::plazaMid, theme::plazaBottom);

        // radial-gradient(60% 130% at 50% 100%) over the bottom 190px.
        float poolHeight = band.h * (190.0f / 520.0f);
        float poolRx = band.w * 0.6f;
        float poolRy = poolHeight * 1.3f;
        r.glow(Rect { band.centerX() - poolRx, band.bottom() - poolRy, poolRx * 2.0f,
                   poolRy * 2.0f },
            theme::accentGlow.scaleAlpha(0.4f), 1.5f);

        drawCrowd(r, band);

        // left:64px, top:48px, gap:--space-3
        // 0.55 rather than the artboard's auto width: it has to stop short of
        // the crowd, which now stands on the right.
        Rect text { band.x + theme::edge, band.y + theme::edgeTop, band.w * 0.55f, band.h };

        TextStyle eyebrow;
        eyebrow.size = theme::textSm;
        eyebrow.weight = FontWeight::Bold;
        eyebrow.color = theme::accent;
        eyebrow.tracking = theme::trackingWider;
        eyebrow.uppercase = true;
        r.text(text.x, text.y, "the plaza - today", eyebrow);

        TextStyle title;
        title.size = theme::text3xl;
        title.weight = FontWeight::Bold;
        title.color = theme::fg1;
        title.tracking = theme::trackingTight;
        title.leading = theme::leadingTight;

        float titleY = text.y + theme::textSm * theme::leadingNormal + theme::s3;
        Stats stats = app.store().stats();

        std::string headline;
        if (stats.uniquePeople == 0)
            headline = "Nobody has crossed you yet";
        else if (stats.today == 1)
            headline = "1 person crossed your path";
        else if (stats.today > 0)
            headline = format("%u people crossed your path", stats.today);
        else
            headline = format("%u passes waiting for you", stats.unopened);

        float titleUsed = r.textWrapped(Rect { text.x, titleY, text.w, title.size * 2.2f },
            headline, title, 2);

        TextStyle sub;
        sub.size = theme::textBase;
        sub.color = theme::fg2;
        r.textWrapped(Rect { text.x, titleY + titleUsed + theme::s3, text.w,
                          sub.size * theme::leadingNormal * 2.0f },
            subtitleText(app), sub, 2);
    }

    // Six figures, flex-end aligned with a --space-5 gap.
    // They are fractions of the 300px hero here so the
    // crowd scales with the band.
    void drawCrowd(Renderer& r, const Rect& band)
    {
        if (m_crossings.empty())
            return;

        struct Slot {
            float height; // fraction of the hero's height
        };
        // Depth comes from size alone.
        static constexpr Slot kSlots[4] = {
            { 0.667f }, { 1.000f }, { 0.720f }, { 0.573f },
        };
        // Which crossing each slot shows: the newest is the hero.
        static constexpr int kOrder[4] = { 1, 0, 2, 3 };
        // The space between figures, as a fraction of the hero's height.
        //
        // They do not overlap. Hair is far wider than the face it sits on -
        // the median style reaches 1.37 times the face's half-width and 75 of
        // the 131 styles reach past 1.35 - so figures whose boxes merely touch
        // already bury a third of the neighbour's face. This gap is sized so a
        // typical hairstyle leans into the space rather than onto a face.
        static constexpr float kGapOfHero = 0.107f;

        // Back to front: the fourth behind everyone, the third in front of it,
        // the hero in front of the lot with the first tucked behind it. The
        // figures are spaced rather than stacked, so this rarely decides a
        // pixel - but a wide hairstyle does lean into the gap, and when it
        // does, the nearer figure should be the one that wins.
        static constexpr int kDepth[4] = { 3, 2, 0, 1 };

        float scale = band.h / theme::plazaHeight;
        float heroHeight = 300.0f * scale;
        float floor = band.bottom() - 88.0f * scale;

        int count = std::min<int>(static_cast<int>(m_crossings.size()), 4);

        auto widthOf = [&](int slot) { return heroHeight * kSlots[slot].height * 0.52f; };

        float gap = heroHeight * kGapOfHero;

        float total = 0.0f;
        for (int i = 0; i < count; i++)
            total += widthOf(i) + (i ? gap : 0.0f);

        float xs[4] = {};
        float cursor = band.right() - theme::s9 - total;
        for (int slot = 0; slot < count; slot++) {
            xs[slot] = cursor;
            cursor += widthOf(slot) + gap;
        }

        for (int d = 0; d < count; d++) {
            int slot = kDepth[d];
            if (slot >= count)
                continue;

            float height = heroHeight * kSlots[slot].height;
            float width = widthOf(slot);

            int which = kOrder[slot] < count ? kOrder[slot] : slot;
            const Crossing& crossing = m_crossings[static_cast<size_t>(which)];

            ui::miiFigure(r, Rect { xs[slot], floor - height, width, height },
                crossing.pass.face(), 1.0f, slot == 1);
        }
    }

    std::string subtitleText(App& app)
    {
        if (m_crossings.empty())
            return "Take the console outside, or join a busier network. Passes arrive on "
                   "their own.";

        std::vector<std::string> places;
        for (const Crossing& crossing : m_crossings) {
            if (crossing.place.empty())
                continue;
            if (std::find(places.begin(), places.end(), crossing.place) == places.end())
                places.push_back(crossing.place);
            if (places.size() == 3)
                break;
        }

        if (places.empty()) {
            Sync::Status status = app.sync().status();
            return status.message.empty() ? "Somewhere out there." : status.message;
        }

        std::string out;
        for (size_t i = 0; i < places.size(); i++) {
            if (i > 0)
                out += i + 1 == places.size() ? ", and " : ", ";
            out += places[i];
        }
        return out + ".";
    }

    // Everything below the band: padding --space-6 top, 64 left, gap --space-5.
    void drawPasses(App& app, Renderer& r, const Rect& area)
    {
        // --safe-edge on both sides: the button hints are right-aligned in here
        // and were landing 40px past the title-safe area.
        app.hint("A", "open");
        app.hint("X", "look now");
        app.hint("Y", "mark read");

        // --safe-edge on both sides.
        // The strip is deliberately not clipped vertically - that is
        // what keeps the first tile's ring intact - so the room has to be real.
        Rect content { area.x + theme::edge, area.y + theme::s5,
            area.w - theme::edge * 2.0f, area.h - theme::s5 };

        float headerHeight = theme::textLg * theme::leadingSnug;
        drawSectionHeader(app, r, Rect { content.x, content.y, content.w, headerHeight },
            "Unopened passes", unopenedCaption(app));

        Rect strip { content.x, content.y + headerHeight + theme::s4, content.w, kTileHeight };

        if (m_crossings.empty()) {
            drawEmptyState(r, Rect { content.x, strip.y, std::min(content.w, 1000.0f), 200.0f });
            return;
        }

        int count = static_cast<int>(m_crossings.size());
        m_stripRect = strip;
        m_scroll.setBounds(strip.w,
            static_cast<float>(count) * (kTileWidth + kTileGap) - kTileGap
                + kMoreWidth + kTileGap);
        float offset = m_scroll.offset();

        // Tiles slide out sideways, so the cut belongs on the left and right -
        // but set back by the room a focused tile needs, or the first one loses
        // its ring to the very edge it sits against. Vertically nothing is
        // clipped, so the grow has all the room it wants there.
        r.pushClipHorizontal(strip.inset(-theme::focusRoom, 0.0f));
        for (int i = 0; i < count; i++) {
            Rect tile { strip.x - offset + static_cast<float>(i) * (kTileWidth + kTileGap),
                strip.y, kTileWidth, kTileHeight };
            if (tile.right() < strip.x - 60.0f || tile.x > strip.right() + 60.0f)
                continue;

            app.touchZone(tile, Zone_Card, i);
            drawPassTile(app, r, tile, m_crossings[static_cast<size_t>(i)], i);
        }

        // The artboard's trailing "2 more" tile.
        int hidden = countBeyond(strip, offset, count);
        if (hidden > 0) {
            Rect more { strip.x - offset + static_cast<float>(count) * (kTileWidth + kTileGap),
                strip.y, kMoreWidth, kTileHeight };
            drawMoreTile(r, more, hidden);
        }
        r.popClip();
    }

    int countBeyond(const Rect& strip, float offset, int count) const
    {
        int visible = 0;
        for (int i = 0; i < count; i++) {
            float x = strip.x - offset + static_cast<float>(i) * (kTileWidth + kTileGap);
            if (x + kTileWidth <= strip.right() + 1.0f)
                visible++;
        }
        return std::max(0, count - visible);
    }

    std::string unopenedCaption(App& app)
    {
        Stats stats = app.store().stats();

        std::string detail;
        if (stats.unopened == 0) {
            detail = m_crossings.empty() ? "nothing yet" : "all caught up";
        } else {
            detail = format("%u waiting", stats.unopened);
            if (stats.oldestUnopened != 0) {
                detail += " - oldest ";
                detail += relativeTime(stats.oldestUnopened, nowUnix());
            }
        }

        if (stats.totalCrossings > 0)
            detail += format(" - %u crossings, %u place%s", stats.totalCrossings, stats.places,
                stats.places == 1 ? "" : "s");
        return detail;
    }

    // h3 plus a --text-sm caption on the same baseline, gap --space-4.
    void drawSectionHeader(App& app, Renderer& r, const Rect& row, const char* heading,
        const std::string& caption)
    {
        TextStyle title;
        title.size = theme::textLg;
        title.weight = FontWeight::Bold;
        title.color = theme::fg1;
        float width = r.text(row.x, row.y, heading, title);

        TextStyle meta;
        meta.size = theme::textSm;
        meta.weight = FontWeight::Medium;
        meta.color = theme::fg3;
        r.text(Rect { row.x + width + theme::s4, row.y, 700.0f, row.h }, caption, meta,
            Align::Left, VAlign::Bottom);


    }

    // A pass tile: bg-2, radius-3, a 250px stage with the silhouette and the
    // "new" flag, then name / title / place on --space-4 --space-5 padding.
    void drawPassTile(App& app, Renderer& r, const Rect& box, const Crossing& crossing,
        int index)
    {
        bool selected = index == m_selected;
        bool held = app.touchHeld(Zone_Card, index);
        float focus = held ? 1.0f : (selected ? 0.7f + 0.3f * m_focusPulse : 0.0f);

        // transform: scale(1.06) on the focused tile.
        Rect tile = box;
        if (focus > 0.001f) {
            float grow = theme::focusGrow * focus;
            tile = box.inset(-box.w * grow, -box.h * grow);
        }

        r.roundRect(tile, theme::r3, theme::bg2);
        r.strokeRect(tile, theme::r3, theme::stroke, theme::stroke1);

        float stageHeight = tile.h * (kTileStage / kTileHeight);
        Rect stage { tile.x, tile.y, tile.w, stageHeight };

        r.pushClip(stage);
        ui::miiStage(r, stage, crossing.pass.face(), crossing.pass.theme,
            ui::StageFigure { 0.52f, 0.56f, 0.30f, 0.50f, 0.0f, 0.17f }, theme::r3, 0.0f);
        r.popClip();

        if (!crossing.opened) {
            // top/left --space-4, padding 4px 12px, radius-pill, teal on tint.
            TextStyle flag;
            flag.size = theme::textXs;
            flag.weight = FontWeight::Bold;
            flag.color = theme::teal;
            flag.tracking = theme::trackingWide;
            flag.uppercase = true;

            float width = r.measure("new", flag) + 24.0f;
            Rect pill { stage.x + theme::s4, stage.y + theme::s4, width,
                theme::textXs * theme::leadingNormal + 8.0f };
            r.roundRect(pill, pill.h * 0.5f, theme::tealTint);
            r.text(pill, "new", flag, Align::Center, VAlign::Middle);
        }

        Rect text { tile.x + theme::s5, stage.bottom() + theme::s4,
            tile.w - theme::s5 * 2.0f, tile.bottom() - stage.bottom() - theme::s4 };

        TextStyle name;
        name.size = theme::textMd;
        name.weight = FontWeight::Bold;
        name.color = theme::fg1;
        r.text(text.x, text.y, r.ellipsize(crossing.pass.handle, name, text.w), name);

        TextStyle game;
        game.size = theme::textSm;
        game.color = theme::fg2;
        float gameY = text.y + name.size * theme::leadingSnug + 6.0f;
        r.text(text.x, gameY,
            r.ellipsize(crossing.pass.playing.empty() ? "hidden title" : crossing.pass.playing,
                game, text.w),
            game);

        TextStyle when;
        when.size = theme::textXs;
        when.color = theme::fg3;
        std::string stamp = relativeTime(crossing.lastSeen, nowUnix());
        if (!crossing.place.empty())
            stamp = crossing.place + " - " + stamp;
        r.text(text.x, gameY + game.size * theme::leadingNormal + 6.0f,
            r.ellipsize(stamp, when, text.w), when);

        ui::focusRing(r, tile, theme::r3, focus);
    }

    // "2 more": 170px wide, full tile height, bg-1, a chevron over a caption.
    void drawMoreTile(Renderer& r, const Rect& box, int hidden)
    {
        r.roundRect(box, theme::r3, theme::bg1);

        Rect icon { box.centerX() - 24.0f, box.centerY() - 44.0f, 48.0f, 48.0f };
        ui::icon(r, icon, ui::Icon::Chevron, theme::fg3, 3.0f);

        TextStyle label;
        label.size = theme::textSm;
        label.color = theme::fg3;
        r.text(Rect { box.x, box.centerY() + theme::s3, box.w, 40.0f },
            format("%d more", hidden), label, Align::Center, VAlign::Top);
    }

    void drawEmptyState(Renderer& r, const Rect& box)
    {
        TextStyle title;
        title.size = theme::textMd;
        title.weight = FontWeight::Bold;
        title.color = theme::fg2;
        r.text(box.x, box.y, "The plaza is empty", title);

        TextStyle body;
        body.size = theme::textBase;
        body.color = theme::fg3;
        r.textWrapped(Rect { box.x, box.y + theme::textMd * theme::leadingSnug + theme::s4,
                          box.w, 160.0f },
            "Your console trades a pass with anyone whose Switch is on the same network, "
            "or nearby on the internet. Leave it running and come back.",
            body, 3);
    }

    std::vector<Crossing> m_crossings;
    int m_selected = 0;
    float m_focusPulse = 0.0f;

    ui::ScrollView m_scroll;
    Rect m_stripRect;
    bool m_dragging = false;
    bool m_brakedTap = false;
};

} // namespace

std::unique_ptr<Scene> makePlazaScene()
{
    return std::unique_ptr<Scene>(new PlazaScene());
}

} // namespace nxp
