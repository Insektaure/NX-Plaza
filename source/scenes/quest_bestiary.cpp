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
            if (app.takeTap(tap) && !m_braked) {
                if (tap.is(Zone_Back)) {
                    app.popOverlay();
                    return;
                }
                if (tap.is(Zone_Cell) && tap.index >= 0 && tap.index < count())
                    m_pick = tap.index;
                return;
            }

            if (input.back()) {
                app.popOverlay();
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

            app.hint("B", "back");

            drawHeader(app, r);
            drawGrid(app, r);
            drawFooter(r);
        }

    private:
        static constexpr int kColumns = 6;
        static constexpr float kTop = 150.0f;
        static constexpr float kCellW = 288.0f;
        static constexpr float kCellH = 268.0f;
        static constexpr float kGap = 12.0f;
        static constexpr float kHead = 132.0f;
        static constexpr float kFooter = 104.0f;

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
            for (int i = 0; i < n; i++) {
                float x = grid.x + float(i % kColumns) * (kCellW + kGap);
                float y = grid.y + float(i / kColumns) * (kCellH + kGap)
                    - m_scroll.offset();
                Rect cell { x, y, kCellW, kCellH };
                if (cell.bottom() < grid.y - theme::focusRoom
                    || cell.y > grid.bottom() + theme::focusRoom)
                    continue;
                drawCell(app, r, cell, i);
            }
            r.popClip();
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
            Rect head { cell.x + (cell.w - kHead) * 0.5f, cell.y + theme::s4, kHead,
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
            r.text(Rect { cell.x, head.bottom() + theme::s3, cell.w, 26.0f },
                format(tr("Floor %d"), floor), number, Align::Center, VAlign::Top);

            TextStyle name;
            name.size = theme::textBase;
            name.weight = FontWeight::Bold;
            name.color = known ? theme::fg1 : theme::fg4;
            r.text(Rect { cell.x + theme::s3, head.bottom() + theme::s3 + 30.0f,
                       cell.w - theme::s5, 34.0f },
                known ? shadowName(floor) : std::string("???"), name, Align::Center,
                VAlign::Top);

            if (!known) {
                TextStyle next;
                next.size = theme::textXs;
                next.color = theme::accent;
                next.tracking = theme::trackingWide;
                next.uppercase = true;
                r.text(Rect { cell.x, cell.bottom() - 34.0f, cell.w, 26.0f },
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

            Rect foot { theme::edge, Renderer::DesignHeight - kFooter + theme::s3,
                Renderer::DesignWidth - theme::edge * 2.0f, 48.0f };

            TextStyle who;
            who.size = theme::textBase;
            who.weight = FontWeight::Bold;
            who.color = beaten(floor) ? theme::fg1 : theme::fg3;
            r.text(foot.x, foot.y,
                beaten(floor) ? shadowName(floor) : std::string(tr("Not met yet")),
                who);

            TextStyle stats;
            stats.size = theme::textSm;
            stats.color = theme::fg3;
            stats.tracking = theme::trackingWide;
            r.text(Rect { foot.x, foot.y, foot.w, 34.0f },
                format("HP %u   ATK %u   DEF %u   SPD %u", unsigned(boss.hp),
                    unsigned(boss.atk), unsigned(boss.def), unsigned(boss.spd)),
                stats, Align::Right, VAlign::Top);
        }

        // The same ink the fight draws a shadow in, so an unbeaten one on this
        // wall is the same colour as the thing standing in front of you.
        static const Color kShadowInk;

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
