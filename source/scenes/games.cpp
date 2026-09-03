#include "app.h"
#include "core/store.h"
#include "core/util.h"
#include "core/wallet.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nxp {

namespace {

    // Things to play with.
    class GamesScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Row = Touch_SceneBase,
        };

        void onEnter(App& app) override
        {
            (void)app;
            m_row = 0;
        }

        void update(App& app, const Input& input, float dt) override
        {
            (void)dt;
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

            TouchTarget tap;
            if (app.takeTap(tap)) {
                if (tap.is(Zone_Row) && tap.index >= 0 && tap.index < kGames) {
                    m_row = tap.index;
                    open(app, tap.index);
                }
                return;
            }

            if (input.navDown)
                m_row = (m_row + 1) % kGames;
            if (input.navUp)
                m_row = (m_row - 1 + kGames) % kGames;
            if (input.accept())
                open(app, m_row);
        }

        void draw(App& app, Renderer& r) override
        {
            app.hint("A", "play");

            Rect area = app.contentArea();
            Rect content { area.x + theme::edge, area.y + theme::s8,
                area.w - theme::edge * 2.0f, area.h - theme::s8 - theme::s7 };

            float y = content.y;
            ui::eyebrow(r, Rect { content.x, y, content.w, 34.0f }, "games");
            y += 40.0f;

            TextStyle title;
            title.size = theme::text3xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            r.text(content.x, y, "Something to do", title);

            // The balance, because one of these can be played for a coin.
            TextStyle meta;
            meta.size = theme::textSm;
            meta.color = theme::fg3;
            uint32_t coins = Wallet::get().balance();
            r.text(Rect { content.x, y, content.w, title.size * theme::leadingTight },
                format("%u %s", unsigned(coins), coins == 1 ? "coin" : "coins"), meta,
                Align::Right, VAlign::Middle);
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle sub;
            sub.size = theme::textBase;
            sub.color = theme::fg3;
            r.text(content.x, y,
                "Played with the faces and names in your collection, so the more "
                "people you have crossed, the more there is here.",
                sub);
            y += sub.size * theme::leadingNormal + theme::s6;

            for (int i = 0; i < kGames; i++) {
                drawRow(app, r, Rect { content.x, y, content.w, kRowHeight }, i);
                y += kRowHeight + theme::s3;
            }
        }

    private:
        static constexpr int kGames = 1;
        static constexpr float kRowHeight = 148.0f;
        static constexpr float kIconBox = 76.0f;

        void open(App& app, int index)
        {
            if (index == 0)
                app.pushOverlay(makeMiiRaceScene());
        }

        void drawRow(App& app, Renderer& r, const Rect& box, int index)
        {
            bool focused = index == m_row;
            app.touchZone(box, Zone_Row, index);
            float focus = focused
                ? (app.touchHeld(Zone_Row, index) ? 1.0f : 0.7f + 0.3f * m_pulse)
                : 0.0f;
            ui::card(r, box, focus, focused ? theme::bg2 : theme::bg1, theme::r3);

            Rect inner = box.inset(theme::s6, theme::s5);
            Rect icon { inner.x, inner.centerY() - kIconBox * 0.5f, kIconBox, kIconBox };
            r.circle(icon.centerX(), icon.centerY(), kIconBox * 0.5f,
                theme::accent.scaleAlpha(0.18f));
            ui::icon(r, icon.inset(kIconBox * 0.18f), ui::Icon::Flag, theme::accent,
                2.5f);

            float textX = icon.right() + theme::s5;
            float textW = inner.right() - textX;

            TextStyle name;
            name.size = theme::textBase;
            name.weight = FontWeight::Bold;
            name.color = theme::fg1;
            name.leading = theme::leadingSnug;
            r.text(textX, inner.y, "The Mii race", name);

            TextStyle body;
            body.size = theme::textSm;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            r.textWrapped(Rect { textX, inner.y + name.size * theme::leadingSnug + 6.0f,
                              textW, inner.h },
                "Three of the people you have crossed, against your own Mii. Nobody "
                "is faster than anybody. Watch for nothing, bet a coin on your own "
                "Mii for three back, or call first and second for eleven.",
                body, 2);
        }

        int m_row = 0;
        float m_pulse = 0.0f;
    };
}

std::unique_ptr<Scene> makeGamesScene() { return std::make_unique<GamesScene>(); }

} // namespace nxp
