#include "app.h"
#include "core/store.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>

namespace nxp {

namespace {

    // Things to play with.
    class GamesScene final : public Scene {
    public:
        void update(App& app, const Input& input, float dt) override
        {
            (void)app;
            (void)input;
            (void)dt;
        }

        void draw(App& app, Renderer& r) override
        {
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
            r.text(content.x, y, "Nothing to play yet", title);
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle sub;
            sub.size = theme::textBase;
            sub.color = theme::fg3;
            sub.leading = theme::leadingNormal;
            r.textWrapped(Rect { content.x, y, std::min(content.w, 1100.0f), content.h },
                "This is where the games will be. They will be played with the "
                "faces, names and hours in your collection, so the more people "
                "you have crossed, the more there is to play with.",
                sub, 3);
        }
    };
}

std::unique_ptr<Scene> makeGamesScene() { return std::make_unique<GamesScene>(); }

} // namespace nxp
