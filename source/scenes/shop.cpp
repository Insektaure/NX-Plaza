#include "app.h"
#include "core/pieces.h"
#include "core/store.h"
#include "core/util.h"
#include "core/wallet.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace nxp {

namespace {

    // Takes the coins and hands over the piece, in that order of checking and
    // the other order of doing.
    //
    // Lives outside the scene because the confirmation dialog calls it a frame
    // or more later, from a callback that has no business holding on to the
    // screen that opened it. Everything it needs is two values.
    void settle(App& app, uint32_t price, bool activeOnly)
    {
        Wallet& wallet = Wallet::get();
        // Re-checked rather than trusted: the dialog was drawn against a
        // balance and a shelf from an earlier frame.
        if (wallet.balance() < price) {
            app.toast(format("%u coins short", unsigned(price - wallet.balance())),
                "Ten arrive on each new day you open the app.");
            return;
        }

        // Granted first, then paid for: if there turns out to be nothing to
        // grant, nobody has been charged for it.
        Store::PiecePurchase bought = app.store().buyPiece(activeOnly);
        if (bought.set < 0) {
            app.toast("Nothing to sell there",
                "There are no pieces left to find there.");
            return;
        }
        wallet.spend(price);
        wallet.flush();

        // The picture is named even for the piece you chose: a random draw can
        // land in a puzzle you are not looking at, and the two toasts should
        // not need reading differently.
        const std::vector<PieceSet>& sets = pieceSets();
        app.toast(format("Piece %d of %s", bought.piece + 1,
                      sets[size_t(bought.set)].name),
            format("%u coins left.", unsigned(wallet.balance())));
    }

    // What a day of opening the app is worth.
    //
    // Square tiles on a scrolling grid rather than a column of rows, because
    // the shelf is meant to grow: a row is a shape that says "there are a few
    // of these", and five across says "there are as many as there are". The
    // tile carries an icon, a name and a price and nothing else; the sentence
    // explaining the item follows the cursor into the header, where there is
    // room for it.
    class ShopScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Tile = Touch_SceneBase,
        };

        void onEnter(App& app) override
        {
            m_focus = 0;
            m_scroll.stop();
            build(app);
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            build(app);

            const Touch& touch = input.touch;
            if (touch.pressed)
                m_brakedTap = m_scroll.absorbPress();
            if (touch.down && touch.dragged
                && m_gridArea.contains(touch.startX, touch.startY)) {
                m_scroll.drag(-touch.dy, dt);
                m_dragging = true;
            } else if (m_dragging && !touch.down) {
                m_scroll.release();
                m_dragging = false;
            }
            m_scroll.update(dt);

            int count = int(m_items.size());
            if (count == 0)
                return;

            TouchTarget tap;
            // A tap that only stopped a flick is not a tap on the tile that
            // happens to be under the finger now.
            if (!m_brakedTap && app.takeTap(tap) && tap.is(Zone_Tile)
                && tap.index >= 0 && tap.index < count) {
                m_focus = tap.index;
                buy(app);
                return;
            }

            // Left and right walk the shelf in order, so a half-empty last row
            // is not a dead end. Up and down move a whole row and stop at the
            // ends rather than wrapping, which is what a grid the eye can see
            // the shape of wants.
            int was = m_focus;
            if (input.navRight)
                m_focus = std::min(m_focus + 1, count - 1);
            if (input.navLeft)
                m_focus = std::max(m_focus - 1, 0);
            if (input.navDown)
                m_focus = m_focus + kColumns < count ? m_focus + kColumns : count - 1;
            if (input.navUp && m_focus >= kColumns)
                m_focus -= kColumns;

            if (m_focus != was && m_pitch > 0.0f) {
                m_scroll.stop();
                m_scroll.centerOn(float(m_focus / kColumns) * m_pitch + m_pitch * 0.5f);
            }

            if (input.accept())
                buy(app);
        }

        void draw(App& app, Renderer& r) override
        {
            build(app);
            app.hint("A", m_items.empty() ? "-" : "buy");

            Rect area = app.contentArea();
            Rect content { area.x + theme::edge, area.y + theme::s8,
                area.w - theme::edge * 2.0f, area.h - theme::s8 - theme::s7 };

            float y = content.y;
            ui::eyebrow(r, Rect { content.x, y, content.w, 34.0f }, "the shop");
            y += 40.0f;

            // The balance is the headline: it is the number every price on the
            // shelf is read against.
            uint32_t coins = Wallet::get().balance();
            TextStyle title;
            title.size = theme::text3xl;
            title.weight = FontWeight::Bold;
            title.color = theme::accent;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            ui::coinAmount(r, content.x, y, coins, title);

            // Earned and spent, opposite it. Two totals is all the wallet
            // keeps, and it is the only way to see a day has been credited
            // without having watched the balance.
            TextStyle totals;
            totals.size = theme::textSm;
            totals.color = theme::fg4;
            totals.tracking = theme::trackingWide;
            r.text(Rect { content.x, y, content.w, title.size * theme::leadingTight },
                format("%u earned - %u spent", unsigned(Wallet::get().granted()),
                    unsigned(Wallet::get().spent())),
                totals, Align::Right, VAlign::Middle);
            y += title.size * theme::leadingTight + theme::s3;

            // One line, following the cursor: on a square tile there is room
            // for a name and a price and nothing more.
            TextStyle sub;
            sub.size = theme::textBase;
            sub.color = theme::fg3;
            r.text(content.x, y, r.ellipsize(caption(), sub, content.w), sub);
            y += sub.size * theme::leadingNormal + theme::s6;

            drawGrid(app, r, Rect { content.x, y, content.w, content.bottom() - y });
        }

    private:
        // Five across: 1680 of content less the 24px gutter and four 24px
        // gaps, over five, is a 312px square - and two rows of those fit the
        // 667px the header leaves, so the grid shows ten and scrolls at eleven.
        static constexpr int kColumns = 5;
        static constexpr float kGap = theme::s5;
        static constexpr float kScrollGutter = 24.0f;
        // Inside a 312px tile: 24 inset, 96 of icon, 16, up to three 30.7px
        // lines of name ending at 228, and the pill on the floor at 250.
        static constexpr float kIconBox = 96.0f;

        struct Item {
            ui::Icon icon = ui::Icon::Puzzle;
            std::string label;
            std::string caption; // the sentence, shown for the focused tile
            uint32_t price = 0;
            bool stocked = false;    // there is something left to sell
            bool activeOnly = false; // drawn from the puzzle being filled only
        };

        void build(App& app)
        {
            m_items.clear();
            const std::vector<PieceSet>& sets = pieceSets();
            if (sets.empty())
                return;

            PieceInventory pieces = app.store().pieces();
            int active = pieces.active;
            const PieceSet& set = sets[size_t(active)];

            // Naming the puzzle on the tile, not just in the caption: this is
            // the one purchase whose result depends on a choice made on another
            // screen, so the tile has to say which choice is in force.
            Item chosen;
            chosen.icon = ui::Icon::Puzzle;
            chosen.price = Wallet::kChosenPiecePrice;
            chosen.activeOnly = true;
            chosen.label = format("A piece of %s", set.name);
            chosen.stocked = !pieces.complete(active);
            chosen.caption = chosen.stocked
                ? format("A piece you do not have yet of %s - the puzzle your "
                         "crossings are filling, %d of %u so far. Twice the price, "
                         "because you get to say which picture it goes into.",
                      set.name, pieces.countHeld(active), unsigned(set.count))
                : format("%s is finished. Choose another to fill on the puzzles "
                         "screen, or take your chances with any puzzle.",
                      set.name);
            m_items.push_back(chosen);

            // The same goods without the choice. Cheaper for exactly that
            // reason, and it keeps working once the puzzle being filled is
            // finished and the tile above has nothing left to sell.
            int unfinished = 0;
            int outstanding = 0;
            for (size_t i = 0; i < sets.size(); i++) {
                int held = pieces.countHeld(int(i));
                int total = int(sets[i].count);
                if (held < total) {
                    unfinished++;
                    outstanding += total - held;
                }
            }

            Item any;
            any.icon = ui::Icon::Dice;
            any.price = Wallet::kAnyPiecePrice;
            any.activeOnly = false;
            any.label = "A piece of any puzzle";
            any.stocked = unfinished > 0;
            any.caption = any.stocked
                ? format("Half the price, and you do not choose: one piece drawn "
                         "from every unfinished puzzle at once - %d still to find "
                         "across %d of them.",
                      outstanding, unfinished)
                : std::string("Every puzzle is finished. There is nothing left to "
                              "sell you.");
            m_items.push_back(any);

            m_focus = std::min(std::max(m_focus, 0), int(m_items.size()) - 1);
        }

        // What the header says under the balance.
        std::string caption() const
        {
            if (m_items.empty())
                return "Nothing on the shelf yet.";
            const Item& item = m_items[size_t(m_focus)];
            uint32_t coins = Wallet::get().balance();
            if (item.stocked && coins < item.price) {
                return format("%u more coins needed. Ten arrive on every new day "
                              "you open the app.",
                    unsigned(item.price - coins));
            }
            return item.caption;
        }

        void drawGrid(App& app, Renderer& r, const Rect& grid)
        {
            m_gridArea = grid;
            float tile = (grid.w - kScrollGutter - kGap * float(kColumns - 1))
                / float(kColumns);
            m_pitch = tile + kGap;

            int rows = (int(m_items.size()) + kColumns - 1) / kColumns;
            float total = float(rows) * m_pitch - kGap;
            m_scroll.setBounds(grid.h, std::max(0.0f, total));

            r.pushClipVertical(grid.inset(0.0f, -theme::focusRoom));
            for (size_t i = 0; i < m_items.size(); i++) {
                int row = int(i) / kColumns;
                int column = int(i) % kColumns;
                Rect box { grid.x + float(column) * m_pitch,
                    grid.y + float(row) * m_pitch - m_scroll.offset(), tile, tile };
                // Only what is on screen, so a tile scrolled away does not
                // leave a touch zone behind where nothing is drawn.
                if (box.bottom() >= grid.y - theme::focusRoom
                    && box.y <= grid.bottom() + theme::focusRoom)
                    drawTile(app, r, box, m_items[i], int(i));
            }
            r.popClip();

            if (m_scroll.scrollable()) {
                ui::scrollbar(r, Rect { grid.right() - 8.0f, grid.y, 8.0f, grid.h },
                    m_scroll.progress(), m_scroll.visibleFraction());
            }
        }

        void drawTile(App& app, Renderer& r, const Rect& box, const Item& item, int index)
        {
            uint32_t coins = Wallet::get().balance();
            bool afford = coins >= item.price;
            bool enabled = item.stocked && afford;
            bool focused = index == m_focus;

            app.touchZone(box, Zone_Tile, index);
            float focus = 0.0f;
            if (focused)
                focus = app.touchHeld(Zone_Tile, index) ? 1.0f : 0.7f + 0.3f * m_pulse;
            ui::card(r, box, focus, focused ? theme::bg2 : theme::bg1, theme::r3);

            Rect inner = box.inset(theme::s5, theme::s5);
            Color ink = enabled ? theme::accent : theme::fg4;

            // Icon, name, price, top to bottom, all centred: the tile is square
            // so there is no long edge to hang them off.
            ui::icon(r, Rect { inner.centerX() - kIconBox * 0.5f, inner.y, kIconBox,
                         kIconBox },
                item.icon, ink, 3.5f);

            TextStyle label;
            label.size = theme::textBase;
            label.weight = FontWeight::Bold;
            label.color = enabled ? theme::fg1 : theme::fg3;
            label.leading = theme::leadingSnug;
            // Three lines, not two: "A piece of Mountain in the Fog" is the
            // longest thing the shelf can be asked to say today and a picture
            // added later could be longer. 136 + 3 * 30.7 lands at 228, clear
            // of the pill at 250.
            r.textWrapped(Rect { inner.x, inner.y + kIconBox + theme::s4, inner.w,
                             label.size * theme::leadingSnug * 3.0f },
                item.label, label, 3, Align::Center);

            TextStyle priceText;
            priceText.size = theme::textSm;
            priceText.weight = FontWeight::Bold;
            std::string price = item.stocked ? format("%u", unsigned(item.price))
                                             : std::string("sold out");
            float width = r.measure(price, priceText) + theme::s6;
            ui::pill(r,
                Rect { inner.centerX() - width * 0.5f, inner.bottom() - 38.0f, width,
                    38.0f },
                price, enabled ? theme::bg0 : theme::fg3,
                enabled ? theme::accent : theme::bg3, theme::textSm);
        }

        // A on a tile asks; it does not spend.
        //
        // The shelf is a grid and A is also "open this" everywhere else in the
        // app, so a cursor that drifted one tile while your thumb was moving
        // would otherwise cost a hundred coins. The dialog opens on *cancel* -
        // that is what askConfirm does - so a second stray A is harmless too.
        void buy(App& app)
        {
            if (m_focus < 0 || m_focus >= int(m_items.size()))
                return;
            const Item& item = m_items[size_t(m_focus)];
            uint32_t coins = Wallet::get().balance();

            if (!item.stocked) {
                app.toast("Nothing to sell there", item.caption);
                return;
            }
            if (coins < item.price) {
                app.toast(format("%u coins short", unsigned(item.price - coins)),
                    "Ten arrive on each new day you open the app.");
                return;
            }

            uint32_t price = item.price;
            bool activeOnly = item.activeOnly;
            std::string what = item.label;
            // Lower case, because it lands mid-sentence in the question.
            if (!what.empty())
                what[0] = char(std::tolower(static_cast<unsigned char>(what[0])));

            app.askConfirm(format("Buy %s for %u coins?", what.c_str(),
                               unsigned(price)),
                format("%s That leaves you %u.",
                    activeOnly
                        ? "One you do not hold yet, into the puzzle you are filling."
                        : "Drawn from every unfinished puzzle at once, so it may not "
                          "be the one you are filling.",
                    unsigned(coins - price)),
                "Buy it",
                [appPtr = &app, price, activeOnly]() {
                    settle(*appPtr, price, activeOnly);
                });
        }

        std::vector<Item> m_items;
        int m_focus = 0;
        float m_pulse = 0.0f;

        ui::ScrollView m_scroll;
        Rect m_gridArea {};
        float m_pitch = 0.0f;
        bool m_dragging = false;
        bool m_brakedTap = false;
    };
}

std::unique_ptr<Scene> makeShopScene() { return std::make_unique<ShopScene>(); }

} // namespace nxp
