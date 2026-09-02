#include "app.h"
#include "core/pieces.h"
#include "core/store.h"
#include "core/util.h"
#include "gfx/picture.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
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

        // While a picture is being looked at bare, the tab rail and the status
        // dot go: the artwork is 16:9 and so is the screen, so anything beside
        // it is a border the picture did not ask for. The hint bar stays,
        // because B is the way out and a picture with no way out is a trap.
        bool coversChrome() const override { return m_showcase >= 0 && m_bare; }

        // The hint strip too: the picture is 16:9 and the panel is 16:9, so
        // with nothing along the bottom it fits exactly, corner to corner.
        bool coversHints() const override { return m_showcase >= 0 && m_bare; }

        void onEnter(App& app) override
        {
            m_inventory = app.store().pieces();
            m_row = m_inventory.active;
            m_open = -1;
            m_showcase = -1;
            m_bare = false;
            // Coming back to the tab lands on the puzzle being filled, which is
            // no use if it is scrolled off. The offset is set on the first draw,
            // once the row geometry is known.
            m_scroll.stop();
            m_recentre = true;
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            m_inventory = app.store().pieces();

            const std::vector<PieceSet>& sets = pieceSets();
            if (sets.empty())
                return;

            int count = static_cast<int>(sets.size());
            m_row = std::min(std::max(m_row, 0), count - 1);
            if (m_open >= count)
                m_open = -1;

            if (m_showcase >= 0)
                updateShowcase(app, input, count);
            else if (m_open < 0)
                updateList(app, input, dt, count);
            else
                updateDetail(app, input, sets);
        }

        void draw(App& app, Renderer& r) override
        {
            const std::vector<PieceSet>& sets = pieceSets();
            Rect area = app.contentArea();
            Rect content { area.x + theme::edge, area.y + theme::s8,
                area.w - theme::edge * 2.0f, area.h - theme::s8 - theme::s7 };

            if (m_showcase >= 0 && size_t(m_showcase) < sets.size())
                drawShowcase(app, r, content);
            else if (m_open >= 0 && size_t(m_open) < sets.size())
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

        void updateList(App& app, const Input& input, float dt, int count)
        {
            const Touch& touch = input.touch;
            if (touch.pressed)
                m_brakedTap = m_scroll.absorbPress();
            if (touch.down && touch.dragged
                && m_listArea.contains(touch.startX, touch.startY)) {
                m_scroll.drag(-touch.dy, dt);
                m_dragging = true;
            } else if (m_dragging && !touch.down) {
                m_scroll.release();
                m_dragging = false;
            }
            m_scroll.update(dt);

            TouchTarget tap;
            // A tap that only stopped a flick is not a tap on the row that
            // happens to be under the finger now.
            if (!m_brakedTap && app.takeTap(tap) && tap.is(Zone_Row) && tap.index >= 0
                && tap.index < count) {
                m_row = tap.index;
                open(tap.index);
                return;
            }

            int was = m_row;
            if (input.navDown)
                m_row = (m_row + 1) % count;
            if (input.navUp)
                m_row = (m_row - 1 + count) % count;
            if (m_row != was) {
                m_scroll.stop();
                m_scroll.centerOn(float(m_row) * (kRowHeight + theme::s3)
                    + kRowHeight * 0.5f);
            }

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

            if (input.accept()) {
                // Inside a finished puzzle A shows it, because filling is not
                // on offer and that is the only thing left to want. The list's
                // Y still means fill and says so there, which is why this
                // decision is here and not inside fill().
                if (canShow(m_open))
                    m_showcase = m_open;
                else
                    fill(app, m_open);
            }
        }

        void updateShowcase(App& app, const Input& input, int count)
        {
            if (m_showcase >= count || !m_inventory.complete(m_showcase)) {
                m_showcase = -1;
                return;
            }
            TouchTarget tap;
            if (app.takeTap(tap)) {
                // A tap anywhere leaves bare mode; on the arrow it closes the
                // picture. Bare has no arrow to aim at, which is the point.
                if (m_bare)
                    m_bare = false;
                else if (tap.is(Zone_Back))
                    m_showcase = -1;
                return;
            }

            // A toggles rather than only entering. Bare has no hint strip, so
            // nothing on screen says which button leaves - and the one anybody
            // will try first is the one that got them there.
            if (input.accept())
                m_bare = !m_bare;
            if (input.back()) {
                if (m_bare)
                    m_bare = false;
                else
                    m_showcase = -1;
            }

            // Left and right walk the finished ones, so a gallery of several
            // is a gallery rather than a thing you back out of each time.
            int step = input.navRight ? 1 : (input.navLeft ? -1 : 0);
            if (step != 0) {
                for (int i = 1; i <= count; i++) {
                    int candidate = (m_showcase + step * i + count * i) % count;
                    if (m_inventory.complete(candidate)
                        && pictures().has(pieceSets()[size_t(candidate)].image)) {
                        m_showcase = candidate;
                        break;
                    }
                }
            }
        }

        // A finished puzzle, as the picture it turned out to be.
        void drawShowcase(App& app, Renderer& r, const Rect& content)
        {
            const PieceSet& spec = pieceSets()[size_t(m_showcase)];
            pictures().request(spec.image);

            if (m_bare) {
                // Every pixel of the panel. The rail, the pip and the hint
                // strip are all suppressed, and 1920x1080 is exactly the 16:9
                // the artwork was cut to, so it lands corner to corner with
                // nothing left over.
                drawPicture(r, spec, r.viewport());
                return;
            }

            app.hint("A", "fill the screen");
            app.hint("B", "back");

            Rect back { content.x, content.y, 44.0f, 44.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back, 0);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            r.text(back.right() + theme::s4,
                content.y + (back.h - title.size * theme::leadingTight) * 0.5f,
                spec.name, title);

            // Who it took, and when it came together. Both read off the
            // provenance rather than stored: the sources are already there.
            int people = 0;
            uint64_t finished = 0;
            std::vector<std::string> seen;
            for (const PieceSource& src : m_inventory.sources) {
                if (src.picture != spec.image)
                    continue;
                finished = std::max(finished, src.when);
                if (!src.who.empty()
                    && std::find(seen.begin(), seen.end(), src.who) == seen.end()) {
                    seen.push_back(src.who);
                    people++;
                }
            }

            TextStyle caption;
            caption.size = theme::textSm;
            caption.color = theme::fg3;
            std::string line = people == 1
                ? std::string("One person brought this")
                : format("%d people brought this", people);
            if (finished != 0)
                line += " - finished " + relativeTime(finished, nowUnix());
            r.text(Rect { content.x, content.y, content.w, back.h }, line, caption,
                Align::Right, VAlign::Middle);

            drawPicture(r, spec,
                Rect { content.x, content.y + back.h + theme::s5, content.w,
                    content.bottom() - (content.y + back.h + theme::s5) });
        }

        // The picture, as large as the given box allows, keeping the 16:9 the
        // artwork was cut to.
        void drawPicture(Renderer& r, const PieceSet& spec, const Rect& box)
        {
            float w = std::min(box.w,
                box.h * (kCropW * float(kPerRow)) / (kCropH * float(kRows)));
            float h = w * (kCropH * float(kRows)) / (kCropW * float(kPerRow));
            Rect whole { box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, w, h };

            if (!pictures().resident(spec.image)) {
                // The slot holds another puzzle's picture until the upload
                // lands. One frame, and a stale picture under the wrong name
                // would be worse than a moment of nothing.
                r.roundRect(whole, theme::r3, theme::bg2);
                return;
            }
            const float uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            // Square corners when it fills the screen: a rounded edge against
            // the bezel reads as a card, and this is meant to read as a view.
            r.picture(whole, uv, m_bare ? 0.0f : theme::r3);
        }

        void open(int set)
        {
            m_open = set;
            m_piece = 0;
        }

        // The picture is uploaded from drawing, not from input: it needs the
        // frame's command buffer, and asking every frame costs nothing once it
        // is the resident one.
        void wantPicture(int set) const
        {
            const std::vector<PieceSet>& sets = pieceSets();
            if (set >= 0 && size_t(set) < sets.size())
                pictures().request(sets[size_t(set)].image);
        }

        // Whether this puzzle is finished and there is a picture to show for
        // it. Both have to be true: a finished puzzle on a card with no
        // artwork has nothing behind the grid.
        bool canShow(int set) const
        {
            const std::vector<PieceSet>& sets = pieceSets();
            if (set < 0 || size_t(set) >= sets.size())
                return false;
            return m_inventory.complete(set) && pictures().has(sets[size_t(set)].image);
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

            r.text(content.x, y,
                "Every console you cross brings one piece. Pick which puzzle they go into.",
                sub);
            y += sub.size * theme::leadingNormal + theme::s6;

            // Four rows fit under the heading, for a fifth: the rows scroll instead.
            Rect list { content.x, y, content.w, content.bottom() - y };
            m_listArea = list;
            float total = float(sets.size()) * (kRowHeight + theme::s3) - theme::s3;
            m_scroll.setBounds(list.h, std::max(0.0f, total));
            if (m_recentre) {
                m_scroll.centerOn(float(m_row) * (kRowHeight + theme::s3)
                    + kRowHeight * 0.5f);
                m_recentre = false;
            }

            r.pushClipVertical(list.inset(0.0f, -theme::focusRoom));
            float rowY = list.y - m_scroll.offset();
            for (size_t i = 0; i < sets.size(); i++) {
                Rect row { list.x, rowY, list.w - kScrollGutter, kRowHeight };
                // Only what is on screen, so an off-screen row does not leave a
                // touch zone behind where nothing is drawn.
                if (row.bottom() > list.y && row.y < list.bottom())
                    drawRow(app, r, row, static_cast<int>(i));
                rowY += kRowHeight + theme::s3;
            }
            r.popClip();

            if (m_scroll.scrollable()) {
                ui::scrollbar(r, Rect { list.right() - 8.0f, list.y, 8.0f, list.h },
                    m_scroll.progress(), m_scroll.visibleFraction());
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

            app.hint("A", canShow(m_open) ? "look at it" : fillHintFor(m_open));
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
            // Asked for every frame; the store only does work when the picture
            // it holds is not this one.
            wantPicture(m_open);
            bool art = pictures().resident(spec.image);

            // Nothing missing and a picture to show: stop drawing a board.
            if (art && m_inventory.complete(m_open)) {
                drawWhole(app, r, box);
                return;
            }

            float tileW = (box.w - float(kPerRow - 1) * kTileGap) / float(kPerRow);
            float tileH = (box.h - float(kRows - 1) * kTileGap) / float(kRows);

            for (int i = 0; i < int(spec.count); i++) {
                Rect tile { box.x + float(i % kPerRow) * (tileW + kTileGap),
                    box.y + float(i / kPerRow) * (tileH + kTileGap), tileW, tileH };

                if (m_inventory.has(m_open, uint8_t(i))) {
                    if (art) {
                        // The crop this piece stands for. The grid is exactly
                        // five by three of the picture, so the piece index is
                        // the whole of the arithmetic.
                        float u0 = float(i % kPerRow) / float(kPerRow);
                        float v0 = float(i / kPerRow) / float(kRows);
                        const float uv[4] = { u0, v0, u0 + 1.0f / float(kPerRow),
                            v0 + 1.0f / float(kRows) };
                        r.picture(tile, uv, theme::r2);
                    } else {
                        // No artwork in this build: numbered and tinted by
                        // index. Enough to tell two pieces apart, and obviously
                        // not the finished thing.
                        r.roundRect(tile, theme::r2, theme::cardTheme(uint32_t(i) % 6).tint);
                        r.strokeRect(tile, theme::r2, theme::stroke, theme::stroke2);

                        TextStyle number;
                        number.size = theme::text2xl;
                        number.weight = FontWeight::Bold;
                        number.color = theme::fg1;
                        r.text(tile, format("%d", i + 1), number, Align::Center,
                            VAlign::Middle);
                    }
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

        // A finished puzzle, as the one picture it is.
        void drawWhole(App& app, Renderer& r, const Rect& box)
        {
            // The box was measured with the gaps in it, so it is a hair wider
            // than 16:9. Fit the picture inside it rather than stretch to it.
            float w = box.w;
            float h = w * (kCropH * float(kRows)) / (kCropW * float(kPerRow));
            if (h > box.h) {
                h = box.h;
                w = h * (kCropW * float(kPerRow)) / (kCropH * float(kRows));
            }
            Rect whole { box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, w, h };

            const float uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            r.picture(whole, uv, theme::r3);

            // The cursor still has to work: the panel underneath names whoever
            // brought the piece being pointed at, and with no tile edges left
            // there is nothing else saying which one that is.
            const PieceSet& spec = pieceSets()[size_t(m_open)];
            float tileW = whole.w / float(kPerRow);
            float tileH = whole.h / float(kRows);
            for (int i = 0; i < int(spec.count); i++) {
                Rect tile { whole.x + float(i % kPerRow) * tileW,
                    whole.y + float(i / kPerRow) * tileH, tileW, tileH };
                app.touchZone(tile, Zone_Tile, m_open * 64 + i);
                if (i != m_piece)
                    continue;

                // A dark hairline under the ring. Accent on its own is legible
                // over the app's own surfaces and not over a painting, which
                // can be any colour behind any part of it.
                r.strokeRect(tile.inset(1.0f, 1.0f), theme::r2, theme::stroke,
                    Color { 0.0f, 0.0f, 0.0f, 0.5f });
                ui::focusRing(r, tile, theme::r2, 0.6f + 0.4f * m_pulse);
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

        // Room kept clear on the right of a row so the scrollbar never sits on
        // top of the chevron.
        static constexpr float kScrollGutter = 24.0f;

        PieceInventory m_inventory;
        ui::ScrollView m_scroll;
        Rect m_listArea;
        bool m_dragging = false;
        bool m_brakedTap = false;
        bool m_recentre = false;
        int m_row = 0;    // highlighted row in the list
        int m_open = -1;  // the puzzle filling the screen, or -1 for the list
        int m_piece = 0;  // cursor within the open puzzle
        // The finished puzzle being looked at, with nothing else on screen, or
        // -1. Reached from the open puzzle rather than the list: you have to
        // have filled it to be offered it.
        int m_showcase = -1;
        // The picture with nothing else on it: no name, no caption, no arrow,
        // and the rail and pip suppressed.
        bool m_bare = false;
        float m_pulse = 0.0f;
    };
}

std::unique_ptr<Scene> makePuzzlesScene() { return std::make_unique<PuzzlesScene>(); }

} // namespace nxp
