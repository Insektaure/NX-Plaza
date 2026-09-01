#include "app.h"
#include "core/pieces.h"
#include "core/store.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {
    class PuzzlesScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Puzzle = Touch_SceneBase,
        };

        void onEnter(App& app) override
        {
            m_inventory = app.store().pieces();
            m_focus = m_inventory.active;
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            m_inventory = app.store().pieces();

            const std::vector<PieceSet>& sets = pieceSets();
            if (sets.empty())
                return;

            int count = static_cast<int>(sets.size());
            m_focus = std::min(std::max(m_focus, 0), count - 1);

            TouchTarget tap;
            if (app.takeTap(tap) && tap.is(Zone_Puzzle) && tap.index >= 0
                && tap.index < count) {
                m_focus = tap.index;
                choose(app);
                return;
            }

            if (input.navDown)
                m_focus = (m_focus + 1) % count;
            if (input.navUp)
                m_focus = (m_focus - 1 + count) % count;

            if (input.accept())
                choose(app);
        }

        void draw(App& app, Renderer& r) override
        {
            const std::vector<PieceSet>& sets = pieceSets();

            app.hint("A", sets.empty() ? "-" : "fill this one next");

            Rect area = app.contentArea();
            Rect content { area.x + theme::edge, area.y + theme::s8,
                area.w - theme::edge * 2.0f, area.h - theme::s8 - theme::s7 };

            float y = content.y;

            ui::eyebrow(r, Rect { content.x, y, content.w, 34.0f }, "puzzles");
            y += 40.0f;

            TextStyle title;
            title.size = theme::text3xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;

            int done = 0;
            for (size_t i = 0; i < sets.size(); i++) {
                if (m_inventory.complete(static_cast<int>(i)))
                    done++;
            }
            std::string headline = sets.empty()
                ? std::string("Nothing to collect yet")
                : (done == static_cast<int>(sets.size())
                        ? std::string("Every puzzle is finished")
                        : format("%d of %zu finished", done, sets.size()));
            r.text(content.x, y, headline, title);
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle sub;
            sub.size = theme::textBase;
            sub.color = theme::fg3;
            sub.leading = theme::leadingNormal;
            r.textWrapped(Rect { content.x, y, std::min(content.w, 900.0f), 70.0f },
                "Every console you cross brings one piece. Pick which puzzle they go into.",
                sub, 2);
            y += sub.size * theme::leadingNormal + theme::s6;

            if (sets.empty())
                return;

            for (size_t i = 0; i < sets.size(); i++) {
                float used = drawPuzzle(app, r, Rect { content.x, y, content.w, 0.0f },
                    static_cast<int>(i));
                y += used + theme::s6;
            }
        }

    private:
        static constexpr float kTile = 74.0f;
        static constexpr float kTileGap = 10.0f;
        // Fifteen cuts as five across, which is the shape a picture wants when
        // there is one to cut up.
        static constexpr int kPerRow = 5;

        void choose(App& app)
        {
            const std::vector<PieceSet>& sets = pieceSets();
            if (m_focus < 0 || size_t(m_focus) >= sets.size())
                return;
            app.store().setActivePieceSet(m_focus);
            m_inventory = app.store().pieces();
            app.toast(std::string("Filling ") + sets[size_t(m_focus)].name,
                "Crossings go into this one until you pick another.");
        }

        // Returns the height it used, so the caller can stack the puzzles
        // without the row height being written down in two places.
        float drawPuzzle(App& app, Renderer& r, const Rect& box, int set)
        {
            const PieceSet& spec = pieceSets()[size_t(set)];
            bool focused = set == m_focus;
            bool active = set == m_inventory.active;
            int held = m_inventory.countHeld(set);
            bool complete = m_inventory.complete(set);

            int rows = (int(spec.count) + kPerRow - 1) / kPerRow;
            float gridWidth = float(kPerRow) * kTile + float(kPerRow - 1) * kTileGap;
            float gridHeight = float(rows) * kTile + float(rows - 1) * kTileGap;

            TextStyle name;
            name.size = theme::textLg;
            name.weight = FontWeight::Bold;
            name.color = focused ? theme::accent : theme::fg1;
            float headHeight = name.size * theme::leadingNormal;

            Rect whole { box.x, box.y, gridWidth, headHeight + theme::s3 + gridHeight };
            float nameWidth = r.text(box.x, box.y, spec.name, name);

            // Which puzzle the pieces are going into, as a badge next to its
            // name rather than a word at the end of the line.
            //
            // Once they are all done there is nothing to point at - every row
            // says "finished", and a badge on a full board would be a lie.
            if (active && !complete) {
                TextStyle badgeText;
                badgeText.size = theme::textXs;
                badgeText.weight = FontWeight::Medium;   // as pill() draws it
                float badgeWidth = r.measure("filling", badgeText) + theme::s6;
                Rect badge { box.x + nameWidth + theme::s4,
                    box.y + (headHeight - 34.0f) * 0.5f, badgeWidth, 34.0f };
                ui::pill(r, badge, "filling", theme::bg0, theme::accent, theme::textXs);
            }

            TextStyle count;
            count.size = theme::textSm;
            count.color = complete ? theme::teal : theme::fg3;
            std::string right = complete ? std::string("finished")
                                         : format("%d of %u", held, unsigned(spec.count));
            r.text(Rect { box.x, box.y, gridWidth, headHeight }, right, count, Align::Right,
                VAlign::Middle);

            float gridY = box.y + headHeight + theme::s3;
            for (int i = 0; i < int(spec.count); i++) {
                Rect tile { box.x + float(i % kPerRow) * (kTile + kTileGap),
                    gridY + float(i / kPerRow) * (kTile + kTileGap), kTile, kTile };

                if (m_inventory.has(set, uint8_t(i))) {
                    // Numbered and tinted by index: enough to tell two pieces
                    // apart, and obviously not the finished thing.
                    r.roundRect(tile, theme::r2, theme::cardTheme(uint32_t(i) % 6).tint);
                    r.strokeRect(tile, theme::r2, theme::stroke, theme::stroke2);

                    TextStyle number;
                    number.size = theme::textSm;
                    number.weight = FontWeight::Bold;
                    number.color = theme::fg1;
                    r.text(tile, format("%d", i + 1), number, Align::Center, VAlign::Middle);
                } else {
                    // A gap, not a blank tile: what is missing should read as
                    // missing rather than as another kind of piece.
                    r.strokeRect(tile, theme::r2, theme::stroke, theme::stroke3);
                }
            }

            app.touchZone(whole, Zone_Puzzle, set);
            if (focused)
                ui::focusRing(r, whole.inset(-theme::s4, -theme::s4), theme::r3,
                    0.6f + 0.4f * m_pulse);

            return whole.h;
        }

        PieceInventory m_inventory;
        int m_focus = 0;
        float m_pulse = 0.0f;
    };
}

std::unique_ptr<Scene> makePuzzlesScene() { return std::make_unique<PuzzlesScene>(); }

} // namespace nxp
