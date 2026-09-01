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

    // Two views, one tab.
    //
    // The list answers "how am I doing" at a glance and nothing else: three
    // rows shaped like the rows inside a Settings section, each carrying the
    // tags that matter - which puzzle the pieces are going into, and how much
    // of it is filled. Opening one gives it the whole screen, which is what a
    // 5x3 cut of a 1280x720 picture needs.
    class PuzzlesScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Row = Touch_SceneBase, // index = the puzzle
            Zone_Tile,                  // index = set * 64 + piece
            Zone_Back,
        };

        void onEnter(App& app) override
        {
            m_inventory = app.store().pieces();
            m_row = m_inventory.active;
            m_open = -1;
        }

        void update(App& app, const Input& input, float dt) override
        {
            (void)dt;
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            m_inventory = app.store().pieces();

            const std::vector<PieceSet>& sets = pieceSets();
            if (sets.empty())
                return;

            int count = static_cast<int>(sets.size());
            m_row = std::min(std::max(m_row, 0), count - 1);
            if (m_open >= count)
                m_open = -1;

            if (m_open < 0)
                updateList(app, input, count);
            else
                updateDetail(app, input, sets);
        }

        void draw(App& app, Renderer& r) override
        {
            const std::vector<PieceSet>& sets = pieceSets();
            Rect area = app.contentArea();
            Rect content { area.x + theme::edge, area.y + theme::s8,
                area.w - theme::edge * 2.0f, area.h - theme::s8 - theme::s7 };

            if (m_open >= 0 && size_t(m_open) < sets.size())
                drawDetail(app, r, content);
            else
                drawList(app, r, content, sets);
        }

    private:
        static constexpr float kRowHeight = 132.0f;
        // Fifteen cuts as five across, which is the shape a picture wants.
        static constexpr int kPerRow = 5;
        static constexpr int kRows = 3;
        // The artwork is 1280x720, so a 5x3 cut is not square: one tile is
        // 256x240 of the picture. The grid is sized from the tile rather than
        // the other way round - fitting a 16:9 box and then dividing it leaves
        // the gaps inside the ratio, and every tile ends up a fraction of a
        // percent wrong for the crop it will one day hold.
        //
        // Nothing here is a fixed size: a puzzle wants to be as big as it can be.
        static constexpr float kCropW = 256.0f;
        static constexpr float kCropH = 240.0f;
        static constexpr float kTileGap = 8.0f;
        // The provenance sits beside the grid, not under it. Under it, it took
        // height the picture wanted and still left the width unused.
        static constexpr float kAsideWidth = 400.0f;

        // ------------------------------------------------------------- input

        void updateList(App& app, const Input& input, int count)
        {
            TouchTarget tap;
            if (app.takeTap(tap) && tap.is(Zone_Row) && tap.index >= 0
                && tap.index < count) {
                m_row = tap.index;
                open(tap.index);
                return;
            }

            if (input.navDown)
                m_row = (m_row + 1) % count;
            if (input.navUp)
                m_row = (m_row - 1 + count) % count;

            if (input.accept())
                open(m_row);
            // Choosing what to fill without opening it: the list is where you
            // compare the three, so it is where the choice wants to be made.
            if (input.pressed(HidNpadButton_Y))
                fill(app, m_row);
        }

        void updateDetail(App& app, const Input& input, const std::vector<PieceSet>& sets)
        {
            int count = int(sets[size_t(m_open)].count);

            TouchTarget tap;
            if (app.takeTap(tap)) {
                if (tap.is(Zone_Back)) {
                    m_open = -1;
                    return;
                }
                if (tap.is(Zone_Tile) && tap.index >= 0) {
                    int piece = tap.index % 64;
                    if (tap.index / 64 == m_open && piece < count)
                        m_piece = piece;
                    return;
                }
            }

            if (input.back()) {
                m_open = -1;
                return;
            }

            // Clamped rather than wrapped: the grid is a picture, and walking
            // off the right edge onto the row below is only sensible when the
            // tiles are a list, which these are not.
            int col = m_piece % kPerRow;
            int row = m_piece / kPerRow;
            int lastRow = (count - 1) / kPerRow;
            if (input.navRight)
                col = std::min(col + 1, kPerRow - 1);
            if (input.navLeft)
                col = std::max(col - 1, 0);
            if (input.navDown)
                row = std::min(row + 1, lastRow);
            if (input.navUp)
                row = std::max(row - 1, 0);
            m_piece = std::min(row * kPerRow + col, count - 1);

            if (input.accept())
                fill(app, m_open);
        }

        void open(int set)
        {
            m_open = set;
            m_piece = 0;
        }

        // What pressing the fill button would do, worded as the three things
        // fill() actually does.
        const char* fillHintFor(int set) const
        {
            if (set < 0 || size_t(set) >= pieceSets().size())
                return "-";
            if (m_inventory.complete(set))
                return "already finished";
            if (set == m_inventory.active)
                return "already filling";
            return "fill this one next";
        }

        // Makes a puzzle the one crossings go into.
        void fill(App& app, int set)
        {
            const std::vector<PieceSet>& sets = pieceSets();
            if (set < 0 || size_t(set) >= sets.size())
                return;
            // Picking a finished one cannot do what the toast would promise:
            // the store moves off a full puzzle on the next crossing anyway, so
            // saying "crossings go into this one" would be false before the day
            // is out. Say what will actually happen instead.
            if (m_inventory.complete(set)) {
                app.toast(std::string(sets[size_t(set)].name) + " is finished",
                    "Pieces go into the first puzzle that is not.");
                return;
            }
            if (set == m_inventory.active) {
                app.toast(std::string("Already filling ") + sets[size_t(set)].name,
                    "Every crossing brings a piece of this one.");
                return;
            }
            app.store().setActivePieceSet(set);
            m_inventory = app.store().pieces();
            app.toast(std::string("Filling ") + sets[size_t(set)].name,
                "Crossings go into this one until you pick another.");
        }

        // -------------------------------------------------------------- list

        void drawList(App& app, Renderer& r, const Rect& content,
            const std::vector<PieceSet>& sets)
        {
            app.hint("A", sets.empty() ? "-" : "open");
            app.hint("Y", sets.empty() ? "-" : fillHintFor(m_row));

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

            for (size_t i = 0; i < sets.size(); i++) {
                drawRow(app, r, Rect { content.x, y, content.w, kRowHeight },
                    static_cast<int>(i));
                y += kRowHeight + theme::s3;
            }
        }

        void drawRow(App& app, Renderer& r, const Rect& box, int set)
        {
            const PieceSet& spec = pieceSets()[size_t(set)];
            bool focused = set == m_row;
            bool active = set == m_inventory.active;
            bool complete = m_inventory.complete(set);
            int held = m_inventory.countHeld(set);

            app.touchZone(box, Zone_Row, set);
            float focus = app.touchHeld(Zone_Row, set)
                ? 1.0f
                : (focused ? 0.7f + 0.3f * m_pulse : 0.0f);
            ui::card(r, box, focus, focused ? theme::bg2 : theme::bg1, theme::r3);

            Rect inner = box.inset(theme::s6, theme::s5);

            TextStyle label;
            label.size = theme::textBase;
            label.weight = FontWeight::Bold;
            label.color = theme::fg1;
            float nameWidth = r.text(inner.x, inner.y,
                r.ellipsize(spec.name, label, inner.w * 0.6f), label);

            // The tags, on the title's own line so the eye reads name and state
            // together: which puzzle is being filled, and how far along it is.
            if (active && !complete) {
                TextStyle badgeText;
                badgeText.size = theme::textXs;
                badgeText.weight = FontWeight::Medium;   // as pill() draws it
                float badgeWidth = r.measure("filling", badgeText) + theme::s6;
                ui::pill(r,
                    Rect { inner.x + nameWidth + theme::s4,
                        inner.y + (label.size * theme::leadingSnug - 34.0f) * 0.5f,
                        badgeWidth, 34.0f },
                    "filling", theme::bg0, theme::accent, theme::textXs);
            }

            TextStyle progress;
            progress.size = theme::textSm;
            progress.color = complete ? theme::teal : theme::fg3;
            r.text(inner.x, inner.y + label.size * theme::leadingSnug + 6.0f,
                complete ? std::string("finished")
                         : format("%d of %u pieces", held, unsigned(spec.count)),
                progress);

            // A chevron rather than a value: the row opens something.
            Rect chevron { inner.right() - 32.0f, inner.centerY() - 16.0f, 32.0f, 32.0f };
            ui::icon(r, chevron, ui::Icon::Chevron, theme::fg3, 3.0f);

            drawBar(r,
                Rect { inner.x, inner.bottom() - 8.0f, inner.w - 56.0f, 8.0f },
                float(held) / float(spec.count), complete);
        }

        // How full the puzzle is, as a line under the row. Cheaper to read than
        // counting filled squares, and it is the whole reason to glance here.
        void drawBar(Renderer& r, const Rect& box, float fraction, bool complete)
        {
            r.roundRect(box, box.h * 0.5f, theme::bg3);
            float w = std::max(0.0f, std::min(1.0f, fraction)) * box.w;
            if (w > 0.0f) {
                r.roundRect(Rect { box.x, box.y, std::max(w, box.h), box.h },
                    box.h * 0.5f, complete ? theme::teal : theme::accent);
            }
        }

        // ------------------------------------------------------------ detail

        void drawDetail(App& app, Renderer& r, const Rect& content)
        {
            const PieceSet& spec = pieceSets()[size_t(m_open)];
            bool complete = m_inventory.complete(m_open);
            bool active = m_open == m_inventory.active;
            int held = m_inventory.countHeld(m_open);

            app.hint("A", fillHintFor(m_open));
            app.hint("B", "back");

            float y = content.y;

            Rect back { content.x, y, 44.0f, 44.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back, 0);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            float nameWidth = r.text(back.right() + theme::s4,
                y + (back.h - title.size * theme::leadingTight) * 0.5f, spec.name, title);

            if (active && !complete) {
                TextStyle badgeText;
                badgeText.size = theme::textXs;
                badgeText.weight = FontWeight::Medium;
                float badgeWidth = r.measure("filling", badgeText) + theme::s6;
                ui::pill(r,
                    Rect { back.right() + theme::s4 + nameWidth + theme::s4,
                        y + (back.h - 34.0f) * 0.5f, badgeWidth, 34.0f },
                    "filling", theme::bg0, theme::accent, theme::textXs);
            }

            TextStyle count;
            count.size = theme::textSm;
            count.color = complete ? theme::teal : theme::fg3;
            r.text(Rect { content.x, y, content.w, back.h },
                complete ? std::string("finished")
                         : format("%d of %u pieces", held, unsigned(spec.count)),
                count, Align::Right, VAlign::Middle);

            y += back.h + theme::s6;

            // As large as both dimensions allow. Whichever runs out first
            // decides the tile, and the grid follows from it.
            float roomW = content.w - kAsideWidth - theme::s6;
            float roomH = content.bottom() - y;
            float tileW = std::min((roomW - float(kPerRow - 1) * kTileGap) / float(kPerRow),
                ((roomH - float(kRows - 1) * kTileGap) / float(kRows)) * (kCropW / kCropH));
            float tileH = tileW * (kCropH / kCropW);
            float gridW = float(kPerRow) * tileW + float(kPerRow - 1) * kTileGap;
            float gridH = float(kRows) * tileH + float(kRows - 1) * kTileGap;

            drawGrid(app, r, Rect { content.x, y, gridW, gridH });
            drawProvenance(r,
                Rect { content.x + gridW + theme::s6, y, kAsideWidth, roomH });
        }

        void drawGrid(App& app, Renderer& r, const Rect& box)
        {
            const PieceSet& spec = pieceSets()[size_t(m_open)];
            float tileW = (box.w - float(kPerRow - 1) * kTileGap) / float(kPerRow);
            float tileH = (box.h - float(kRows - 1) * kTileGap) / float(kRows);

            for (int i = 0; i < int(spec.count); i++) {
                Rect tile { box.x + float(i % kPerRow) * (tileW + kTileGap),
                    box.y + float(i / kPerRow) * (tileH + kTileGap), tileW, tileH };

                if (m_inventory.has(m_open, uint8_t(i))) {
                    // Numbered and tinted by index until the artwork can be
                    // drawn: enough to tell two pieces apart, and obviously not
                    // the finished thing.
                    r.roundRect(tile, theme::r2, theme::cardTheme(uint32_t(i) % 6).tint);
                    r.strokeRect(tile, theme::r2, theme::stroke, theme::stroke2);

                    TextStyle number;
                    number.size = theme::text2xl;   // the tile is 243px now
                    number.weight = FontWeight::Bold;
                    number.color = theme::fg1;
                    r.text(tile, format("%d", i + 1), number, Align::Center, VAlign::Middle);
                } else {
                    // A gap, not a blank tile: what is missing should read as
                    // missing rather than as another kind of piece.
                    r.strokeRect(tile, theme::r2, theme::stroke, theme::stroke3);
                }

                app.touchZone(tile, Zone_Tile, m_open * 64 + i);
                if (i == m_piece)
                    ui::focusRing(r, tile.inset(-theme::s2, -theme::s2), theme::r2,
                        0.6f + 0.4f * m_pulse);
            }
        }

        // Who brought the piece the cursor is on.
        //
        // This is the whole point of collecting them. A grid of numbered tiles
        // is a progress bar; a grid where every tile remembers the person who
        // handed it over is a record of who you have crossed, which is what the
        // app is for. The name is kept next to the piece rather than looked up
        // through the crossing, so it survives the collection pruning.
        void drawProvenance(Renderer& r, const Rect& box)
        {
            const PieceSet& spec = pieceSets()[size_t(m_open)];
            if (m_piece < 0 || m_piece >= int(spec.count) || box.w < 120.0f)
                return;
            uint8_t piece = uint8_t(m_piece);

            bool held = m_inventory.has(m_open, piece);
            const PieceSource* src = m_inventory.sourceFor(m_open, piece);

            std::string who;
            if (!held)
                who = "Not found yet";
            else if (src == nullptr || src->who.empty())
                who = "someone";   // collected before this was kept
            else
                who = src->who;

            std::string tail;
            if (!held)
                tail = "Cross someone while this puzzle is the one being filled.";
            else if (src != nullptr && src->when != 0)
                tail = relativeTime(src->when, nowUnix());
            else
                tail = "Collected before the app started keeping track.";

            TextStyle label;
            label.size = theme::textSm;
            label.color = theme::fg3;

            TextStyle name;
            name.size = theme::textLg;
            name.weight = FontWeight::Bold;
            name.color = held ? theme::fg1 : theme::fg3;
            name.tracking = theme::trackingTight;

            TextStyle note;
            note.size = theme::textSm;
            note.color = theme::fg3;
            note.leading = theme::leadingNormal;

            // Measured before the card is drawn, so the surface is the size of
            // what goes on it.
            float inner = box.w - theme::s6 * 2.0f;
            float lineLabel = r.lineHeight(label);
            float lineName = r.lineHeight(name);
            float noteHeight = r.measureWrapped(inner, tail, note, 4);
            float needed = theme::s5 * 2.0f + lineLabel + (held ? lineLabel : 0.0f)
                + lineName + theme::s2 + noteHeight;

            Rect panel { box.x, box.y, box.w, std::min(box.h, needed) };
            ui::card(r, panel);

            float x = panel.x + theme::s6;
            float y = panel.y + theme::s5;

            r.text(x, y, format("Piece %d", piece + 1), label);
            y += lineLabel;

            if (held) {
                r.text(x, y, "Brought by", label);
                y += lineLabel;
            }

            r.text(x, y, r.ellipsize(who, name, inner), name);
            y += lineName + theme::s2;

            r.textWrapped(Rect { x, y, inner, noteHeight }, tail, note, 4);
        }

        PieceInventory m_inventory;
        int m_row = 0;    // highlighted row in the list
        int m_open = -1;  // the puzzle filling the screen, or -1 for the list
        int m_piece = 0;  // cursor within the open puzzle
        float m_pulse = 0.0f;
    };
}

std::unique_ptr<Scene> makePuzzlesScene() { return std::make_unique<PuzzlesScene>(); }

} // namespace nxp
