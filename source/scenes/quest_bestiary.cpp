#include "app.h"
#include "core/i18n.h"
#include "core/quest_record.h"
#include "core/quest_rules.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <string>

namespace nxp {

namespace {

    // Everything the tower has put in front of you, and the next thing it
    // will.
    //
    // This screen stores nothing. A shadow's face and its name are both
    // worked out from its floor number, and the deepest floor reached is
    // already on the record - so "which ones have I beaten" is a comparison
    // rather than a list, and the whole thing is a pure function of one
    // number the app already keeps.
    //
    // What makes it worth looking at is that beating a floor unmasks it.
    // Everywhere else a shadow is drawn flat in one ink, because that is what
    // a shadow is; here, the ones behind you are drawn as the face underneath
    // - hair, eyes, colours - and the one ahead is still a silhouette.
    class BestiaryScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Cell = Touch_SceneBase,
            Zone_Back,
            Zone_Card,
        };

        bool coversChrome() const override { return true; }

        void onEnter(App&) override
        {
            m_pick = 0;
            m_scroll.stop();
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_scroll.update(dt);

            const Touch& touch = input.touch;
            if (touch.pressed)
                m_braked = m_scroll.absorbPress();
            if (touch.down && touch.dragged && m_grid.contains(touch.x, touch.y)) {
                m_scroll.drag(-touch.dy, dt);
                m_dragging = true;
            } else if (m_dragging && !touch.down) {
                m_scroll.release();
                m_dragging = false;
            }

            TouchTarget tap;
            bool tapped = app.takeTap(tap);
            if (m_card) {
                // A card is a modal: B and A both close it, and a tap
                // anywhere outside it does too.
                if (input.back() || input.accept()
                    || (tapped && !tap.is(Zone_Card)))
                    m_card = false;
                return;
            }

            if (tapped && !m_braked) {
                if (tap.is(Zone_Back)) {
                    app.popOverlay();
                    return;
                }
                if (tap.is(Zone_Cell) && tap.index >= 0 && tap.index < count()) {
                    // A second tap on the one already under the cursor opens
                    // it, which is the touch version of moving there and
                    // pressing A.
                    if (tap.index == m_pick)
                        m_card = true;
                    m_pick = tap.index;
                }
                return;
            }

            if (input.back()) {
                app.popOverlay();
                return;
            }
            if (input.accept() && count() > 0) {
                m_card = true;
                return;
            }

            if (input.navLeft)
                step(-1);
            if (input.navRight)
                step(1);
            if (input.navUp)
                step(-kColumns);
            if (input.navDown)
                step(kColumns);
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);
            app.touchZone(r.viewport(), Touch_None);

            if (m_card) {
                app.hint("B", "close");
            } else {
                if (count() > 0)
                    app.hint("A", "look closer");
                app.hint("B", "back");
            }

            drawHeader(app, r);
            drawGrid(app, r);
            drawFooter(r);
            if (m_card)
                drawCard(app, r);
        }

    private:
        static constexpr int kColumns = 6;
        static constexpr float kTop = 150.0f;
        static constexpr float kCellW = 268.0f;
        static constexpr float kCellH = 268.0f;
        // Wide enough for a focused card to grow into. ui::card lifts a
        // focused one by up to 14px an edge and then draws its ring three
        // outside that, so anything less than about thirty here puts the ring
        // on top of the neighbours.
        static constexpr float kGap = 28.0f;
        static constexpr float kHead = 128.0f;
        // The head's well is taller than the head: miiHead stands the chin on
        // the bottom of what it is given and lets a beard hang below it, so
        // the floor number needs clearing under the box as well as hair room
        // above it.
        static constexpr float kHeadY = 14.0f;
        static constexpr float kNumberY = 172.0f;
        static constexpr float kNameY = 200.0f;
        // Clear of the hint strip, which owns the bottom 88 pixels.
        static constexpr float kFooter = 160.0f;

        // Floors 1 to the deepest, plus the one after it: the next shadow is
        // the only unbeaten one worth drawing, and a wall of nine hundred
        // silhouettes nobody has met is not a wall, it is a spoiler with no
        // secret in it.
        static int count()
        {
            int deepest = int(std::min<uint32_t>(QuestRecord::get().deepest(), 998));
            return deepest + 1;
        }

        static int floorAt(int index) { return index + 1; }

        static bool beaten(int floor)
        {
            return uint32_t(floor) <= QuestRecord::get().deepest();
        }

        void step(int by)
        {
            int n = count();
            if (n <= 0)
                return;
            m_pick = std::min(n - 1, std::max(0, m_pick + by));
            m_scroll.centerOn(float(m_pick / kColumns) * (kCellH + kGap)
                + kCellH * 0.5f);
        }

        void drawHeader(App& app, Renderer& r) const
        {
            TextStyle label;
            label.size = theme::textSm;
            label.weight = FontWeight::Bold;
            label.color = theme::accent;
            label.tracking = theme::trackingWider;
            label.uppercase = true;
            r.text(theme::edge, 44.0f, tr("the quest"), label);

            TextStyle title;
            title.size = theme::textXl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            uint32_t deepest = QuestRecord::get().deepest();
            r.text(theme::edge, 74.0f,
                deepest == 0 ? std::string(tr("No shadow has fallen yet"))
                             : format(tr("%u shadows behind you"), deepest),
                title);

            Rect back { Renderer::DesignWidth - theme::edge - 42.0f, 44.0f, 42.0f,
                42.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);
        }

        void drawGrid(App& app, Renderer& r)
        {
            Rect grid { theme::edge, kTop, Renderer::DesignWidth - theme::edge * 2.0f,
                Renderer::DesignHeight - kTop - kFooter };
            m_grid = grid;

            int n = count();
            int rows = (n + kColumns - 1) / kColumns;
            m_scroll.setBounds(grid.h, float(rows) * (kCellH + kGap));

            r.pushClipVertical(grid.inset(0.0f, -theme::focusRoom));
            // The highlighted one last, so its ring is drawn over its
            // neighbours rather than under the next card painted.
            for (int pass = 0; pass < 2; pass++) {
                for (int i = 0; i < n; i++) {
                    if ((i == m_pick) != (pass == 1))
                        continue;
                    float x = grid.x + float(i % kColumns) * (kCellW + kGap);
                    float y = grid.y + float(i / kColumns) * (kCellH + kGap)
                        - m_scroll.offset();
                    Rect cell { x, y, kCellW, kCellH };
                    if (cell.bottom() < grid.y - theme::focusRoom
                        || cell.y > grid.bottom() + theme::focusRoom)
                        continue;
                    drawCell(app, r, cell, i);
                }
            }
            r.popClip();

            // The grid is six wide and the tower is deep.
            if (m_scroll.scrollable()) {
                ui::scrollbar(r, Rect { grid.right() - 8.0f, grid.y, 8.0f, grid.h },
                    m_scroll.progress(), m_scroll.visibleFraction());
            }
        }

        void drawCell(App& app, Renderer& r, const Rect& cell, int index)
        {
            int floor = floorAt(index);
            bool here = index == m_pick;
            bool known = beaten(floor);

            app.touchZone(cell, Zone_Cell, index);
            ui::card(r, cell, here ? 1.0f : 0.0f, here ? theme::bg2 : theme::bg1,
                theme::r3);

            // The reveal. Everywhere else in the app a shadow is one flat ink,
            // which is what makes it a shadow; the ones you have put down are
            // drawn as whoever was under it.
            Rect head { cell.x + (cell.w - kHead) * 0.5f, cell.y + kHeadY, kHead,
                kHead };
            Mii face = shadowFace(floor);
            if (known) {
                ui::miiHead(r, ui::headroom(head), face);
            } else {
                Color ink = kShadowInk;
                ui::miiHead(r, ui::headroom(head), face, 1.0f, &ink);
            }

            TextStyle number;
            number.size = theme::textXs;
            number.color = theme::fg4;
            number.tracking = theme::trackingWide;
            number.uppercase = true;
            r.text(Rect { cell.x, cell.y + kNumberY, cell.w, 26.0f },
                format(tr("Floor %d"), floor), number, Align::Center, VAlign::Top);

            TextStyle name;
            name.size = theme::textBase;
            name.weight = FontWeight::Bold;
            name.color = known ? theme::fg1 : theme::fg4;
            r.text(Rect { cell.x + theme::s3, cell.y + kNameY, cell.w - theme::s5,
                       34.0f },
                r.ellipsize(known ? shadowName(floor) : std::string("???"), name,
                    cell.w - theme::s5),
                name, Align::Center, VAlign::Top);

            if (!known) {
                TextStyle next;
                next.size = theme::textXs;
                next.color = theme::accent;
                next.tracking = theme::trackingWide;
                next.uppercase = true;
                r.text(Rect { cell.x, cell.bottom() - 32.0f, cell.w, 26.0f },
                    tr("next"), next, Align::Center, VAlign::Top);
            }
        }

        // What the highlighted one is worth, along the foot. One line rather
        // than a panel per cell: the numbers are the same shape for every
        // floor and only differ in size, so they belong somewhere the eye can
        // watch them climb.
        void drawFooter(Renderer& r) const
        {
            if (count() <= 0)
                return;
            int floor = floorAt(std::min(m_pick, count() - 1));
            Boss boss = bossFor(floor);

            // Its own band, painted after the grid and running to the foot of
            // the screen.
            float top = Renderer::DesignHeight - kFooter;
            r.rect(Rect { 0.0f, top, Renderer::DesignWidth, kFooter }, theme::bg0);
            r.rect(Rect { 0.0f, top, Renderer::DesignWidth, theme::stroke },
                theme::stroke1);

            // Both halves on one line, centred in the band rather than sitting
            // on its top edge, so the 26px name and the 22px numbers line up
            // through their middles instead of through their tops.
            Rect foot { theme::edge, top + theme::s4,
                Renderer::DesignWidth - theme::edge * 2.0f, 44.0f };

            TextStyle who;
            who.size = theme::textBase;
            who.weight = FontWeight::Bold;
            who.color = beaten(floor) ? theme::fg1 : theme::fg3;
            r.text(foot,
                beaten(floor) ? shadowName(floor) : std::string(tr("Not met yet")),
                who, Align::Left, VAlign::Middle);

            TextStyle stats;
            stats.size = theme::textSm;
            stats.color = theme::fg3;
            stats.tracking = theme::trackingWide;
            r.text(foot,
                format("HP %u   ATK %u   DEF %u   SPD %u", unsigned(boss.hp),
                    unsigned(boss.atk), unsigned(boss.def), unsigned(boss.spd)),
                stats, Align::Right, VAlign::Middle);
        }

        // One shadow, close up. Everything on it is worked out rather than
        // stored: the face and the name from the floor number, the numbers
        // from bossFor(), and what it can leave from the same weight table
        // the roll itself reads - so the odds printed here cannot drift away
        // from the odds you actually get.
        void drawCard(App& app, Renderer& r) const
        {
            int floor = floorAt(std::min(m_pick, count() - 1));
            bool known = beaten(floor);
            Boss boss = bossFor(floor);

            r.rect(r.viewport(), theme::scrim);

            // 940 x 600 at y 200. The column beside the head is 584 wide,
            // which is one line of English and not one line of anything else,
            // so the sentence in it wraps to two and the numbers under it
            // start low enough to leave room.
            constexpr float kW = 940.0f;
            constexpr float kH = 600.0f;
            Rect panel { (Renderer::DesignWidth - kW) * 0.5f, 200.0f, kW, kH };
            app.touchZone(panel, Zone_Card);
            r.roundRect(panel, theme::r5, theme::bg1);
            r.strokeRect(panel, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = panel.inset(theme::s7, theme::s6);

            // ---- the face, and who it is beside it
            Rect head { inner.x, inner.y, 220.0f, 220.0f };
            Mii face = shadowFace(floor);
            if (known) {
                ui::miiHead(r, ui::headroom(head), face);
            } else {
                Color ink = kShadowInk;
                ui::miiHead(r, ui::headroom(head), face, 1.0f, &ink);
            }

            float x = head.right() + theme::s7;
            TextStyle eyebrow;
            eyebrow.size = theme::textXs;
            eyebrow.color = theme::accent;
            eyebrow.tracking = theme::trackingWider;
            eyebrow.uppercase = true;
            r.text(x, inner.y + 6.0f, format(tr("Floor %d"), floor), eyebrow);

            TextStyle name;
            name.size = theme::textXl;
            name.weight = FontWeight::Bold;
            name.color = known ? theme::fg1 : theme::fg3;
            name.tracking = theme::trackingTight;
            r.text(x, inner.y + 40.0f,
                r.ellipsize(known ? shadowName(floor) : std::string(tr("Not met yet")),
                    name, inner.right() - x),
                name);

            // Measured against the floor below rather than written out: the
            // slope lives in bossFor() and a copy of it here would be a
            // second place to change it and a first place to be wrong.
            if (floor > 1) {
                Boss below = bossFor(floor - 1);
                TextStyle note;
                note.size = theme::textSm;
                note.color = theme::fg3;
                // Wrapped rather than drawn on one line. At 22px this sentence is
                // 583 pixels of English in a 584-pixel column, which is to say it
                // fitted by one pixel in one language and ran off the card in the
                // other eleven.
                r.textWrapped(Rect { x, inner.y + 104.0f, inner.right() - x, 70.0f },
                    format(tr("%u more health and %u more attack than the floor below"),
                        unsigned(boss.hp - below.hp), unsigned(boss.atk - below.atk)),
                    note, 2);
            }

            // ---- its numbers
            const char* labels[4] = { "HP", "ATK", "DEF", "SPD" };
            unsigned values[4] = { unsigned(boss.hp), unsigned(boss.atk),
                unsigned(boss.def), unsigned(boss.spd) };
            float statY = inner.y + 186.0f;
            float statW = (inner.right() - x) / 4.0f;
            for (int i = 0; i < 4; i++) {
                Rect box { x + float(i) * statW, statY, statW, 62.0f };
                TextStyle cap;
                cap.size = theme::textXs;
                cap.color = theme::fg4;
                cap.tracking = theme::trackingWide;
                r.text(box.x, box.y, labels[i], cap);

                TextStyle value;
                value.size = theme::textMd;
                value.weight = FontWeight::Bold;
                value.color = theme::fg1;
                r.text(box.x, box.y + 26.0f, format("%u", values[i]), value);
            }

            // ---- what it can leave
            //
            // Under whichever column is longer: the head is 220 tall and the
            // numbers beside it now end lower than that, so a divider hung
            // off the head alone would be drawn through them.
            float y = std::max(head.bottom(), statY + 62.0f) + theme::s6;
            ui::divider(r, inner.x, y, inner.w);
            y += theme::s5;
            r.text(inner.x, y, tr("what falls here"), eyebrow);
            y += 34.0f;

            TextStyle body;
            body.size = theme::textSm;
            body.color = theme::fg3;
            r.text(inner.x, y, tr("One floor in two leaves something behind."), body);
            y += 40.0f;

            uint32_t weights[Quality_Count] = {};
            dropWeights(floor, weights);
            uint32_t total = 0;
            for (uint32_t w : weights)
                total += w;
            if (total == 0)
                return;

            // Two columns of three, so the six tiers fit on one card without
            // a scroll and the eye can compare the top of one column with the
            // bottom of the other.
            float column = inner.w * 0.5f;
            for (uint8_t q = 0; q < Quality_Count; q++) {
                float cx = inner.x + float(q / 3) * column;
                float cy = y + float(q % 3) * 44.0f;

                TextStyle tier;
                tier.size = theme::textBase;
                tier.weight = FontWeight::Bold;
                tier.color = weights[q] > 0 ? ui::qualityColour(q) : theme::fg4;
                r.text(cx, cy, tr(qualityName(q)), tier);

                // Rounded, and never rounded up to a share that cannot
                // happen: a tier with no weight at this depth says so in
                // words rather than as "0%", which reads like bad luck.
                TextStyle share;
                share.size = theme::textBase;
                share.color = weights[q] > 0 ? theme::fg2 : theme::fg4;
                r.text(Rect { cx, cy, column - theme::s7, 32.0f },
                    weights[q] > 0
                        ? format("%u%%", unsigned((weights[q] * 100 + total / 2) / total))
                        : std::string(tr("never")),
                    share, Align::Right, VAlign::Top);
            }
        }

        // The same ink the fight draws a shadow in, so an unbeaten one on this
        // wall is the same colour as the thing standing in front of you.
        static const Color kShadowInk;

        bool m_card = false;
        ui::ScrollView m_scroll;
        Rect m_grid {};
        int m_pick = 0;
        bool m_dragging = false;
        bool m_braked = false;
    };

    const Color BestiaryScene::kShadowInk = Color::hex(0x2E2733);
}

std::unique_ptr<Scene> makeQuestBestiaryScene()
{
    return std::make_unique<BestiaryScene>();
}

} // namespace nxp
