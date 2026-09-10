#include "app.h"
#include "core/i18n.h"
#include "core/store.h"
#include "core/trophies.h"
#include "core/util.h"
#include "core/wallet.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <memory>
#include <string>
#include <vector>

namespace nxp {

namespace {

    // Everything this console has to show for itself, on one page.
    //
    // Not a single new number: `trophyFacts()` already walks the collection
    // once to answer the trophy conditions, and it comes back with forty
    // answers of which the app was showing about six. This screen is the rest
    // of them - who you have met, where, what you have built out of it, and
    // what the games gave back.
    //
    // Derived on the way past, like the trophies are. Nothing here is written
    // down, so nothing here can disagree with the collection it came from, and
    // a restored backup describes what was restored.
    class StatsScene final : public Scene {
    public:
        void onEnter(App&) override
        {
            // Always from the top: this is a page, not a list with a cursor
            // that remembers where it was.
            m_scroll.stop();
            m_scroll.centerOn(0.0f);
        }

        void update(App& app, const Input& input, float dt) override
        {
            if (input.back() || input.accept()) {
                app.popOverlay();
                return;
            }

            const Touch& touch = input.touch;
            if (touch.pressed)
                m_scroll.absorbPress();
            if (touch.down && touch.dragged
                && m_pageArea.contains(touch.startX, touch.startY)) {
                m_scroll.drag(-touch.dy, dt);
                m_dragging = true;
            } else if (m_dragging && !touch.down) {
                m_scroll.release();
                m_dragging = false;
            }

            // No cursor to follow: the page is read rather than picked
            // through, so the d-pad scrolls it directly.
            if (input.navDown)
                m_scroll.drag(kStep, dt);
            if (input.navUp)
                m_scroll.drag(-kStep, dt);
            m_scroll.update(dt);
        }

        // A page, not a card. Without this the app keeps drawing the pass
        // underneath and only puts a scrim over it.
        bool coversChrome() const override { return true; }

        void draw(App& app, Renderer& r) override
        {
            // That is only safe because of coversChrome() above.
            r.clear(theme::bg0);

            // The chrome is ours now, so nothing behind can be touched, but
            // the zone still stops a stray tap falling through to whatever
            // the app would otherwise do with it.
            app.touchZone(r.viewport(), Touch_None);

            app.hint("B", "close");

            const Store& store = app.store();
            const TrophyFacts facts = trophyFacts(store);
            const Wallet& wallet = Wallet::get();

            // Full bleed, minus the hint strip the chrome still owns: with
            // the rail gone there is no reason to leave its 112px empty.
            Rect area = app.contentArea();
            area.x = 0.0f;
            area.w = Renderer::DesignWidth;

            Rect content { area.x + theme::edge, area.y + theme::s8,
                area.w - theme::edge * 2.0f, area.h - theme::s8 - theme::s7 };

            float y = content.y;
            ui::eyebrow(r, Rect { content.x, y, content.w, 34.0f }, "your record");
            y += 40.0f;

            TextStyle title;
            title.size = theme::text3xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            r.text(content.x, y, tr("Everything so far"), title);
            y += title.size * theme::leadingTight + theme::s5;

            Rect page { content.x, y, content.w, content.bottom() - y };
            m_pageArea = page;

            r.pushClipVertical(page.inset(0.0f, -theme::focusRoom));
            float top = page.y - m_scroll.offset();
            float used = drawSections(r, Rect { page.x, top, page.w, page.h }, store,
                facts, wallet);
            r.popClip();

            m_scroll.setBounds(page.h, used);
            if (m_scroll.scrollable()) {
                ui::scrollbar(r, Rect { page.right() + theme::s4, page.y, 8.0f, page.h },
                    m_scroll.progress(), m_scroll.visibleFraction());
            }
        }

    private:
        // One d-pad press moves about a card's worth.
        static constexpr float kStep = 180.0f;
        // The height the crossing card's stats use, which is what the default
        // 48px numeral and an 18px caption need between them. A bigger numeral
        // would want 155 and the page is long enough already.
        static constexpr float kCardHeight = 132.0f;

        static float cardWidth(const Rect& row, int across)
        {
            float gaps = theme::s4 * float(across - 1);
            return (row.w - gaps) / float(across);
        }

        /// Draws the page from `box.y` down and returns the height it used, so
        /// the caller can tell the scroll view how far there is to go.
        float drawSections(Renderer& r, const Rect& box, const Store& store,
            const TrophyFacts& facts, const Wallet& wallet) const
        {
            float y = box.y;

            // ------------------------------------------------------- people
            y = heading(r, box, y, tr("collection"));
            y = cards(r, box, y,
                { { ui::groupedNumber(facts.uniquePeople), tr("people met") },
                    { ui::groupedNumber(facts.totalCrossings), tr("crossings in total") },
                    { ui::groupedNumber(facts.mostSeen), tr("the one you cross most") } });
            y = cards(r, box, y,
                { { ui::groupedNumber(facts.places), tr("places") },
                    { days(facts.oldestFriendDays), tr("your oldest card") },
                    { ui::groupedNumber(facts.starred), tr("starred") } });
            y = weekdayStrip(r, box, y, facts.weekdays);

            // ---------------------------------------------------- your pass
            y = heading(r, box, y, tr("your pass"));
            y = cards(r, box, y,
                { { ui::groupedNumber(store.passesSent()), tr("times your pass was sent") },
                    { ui::groupedNumber(facts.tradedBack), tr("sent something back") },
                    { ui::groupedNumber(facts.unopened), tr("still unopened") } });

            // ------------------------------------------------------ puzzles
            y = heading(r, box, y, tr("puzzles"));
            y = cards(r, box, y,
                { { format("%u / %u", facts.piecesHeld, facts.piecesTotal), tr("pieces") },
                    { format("%u / %u", facts.puzzlesDone, facts.puzzleCount),
                        tr("pictures finished") },
                    { ui::groupedNumber(facts.pieceDonors), tr("people gave you a piece") } });

            // -------------------------------------------------------- coins
            y = heading(r, box, y, tr("coins"));
            y = cards(r, box, y,
                { { ui::groupedNumber(wallet.granted()), tr("earned by turning up") },
                    { ui::groupedNumber(wallet.spent()), tr("spent") },
                    { ui::groupedNumber(wallet.won()), tr("won at the games") } });

            // -------------------------------------------------------- games
            y = heading(r, box, y, tr("games"));
            y = cards(r, box, y,
                { { format(tr("%u m"), store.bestScore("dash")), tr("the longest run") },
                    { format("%u", store.bestScore("tower")), tr("the tallest tower") },
                    { format("%u", facts.diceStreakBest), tr("duels in a row") } });

            // ----------------------------------------------------- trophies
            y = heading(r, box, y, tr("trophies"));
            y = trophyRow(r, box, y, store, facts);

            return y - box.y;
        }

        float heading(Renderer& r, const Rect& box, float y, const char* label) const
        {
            y += theme::s5;
            ui::divider(r, box.x, y, box.w);
            y += theme::s5;
            ui::eyebrow(r, Rect { box.x, y, box.w, 28.0f }, label, theme::fg3);
            return y + 34.0f;
        }

        struct Stat {
            std::string value;
            const char* caption;
        };

        /// A row of cards, as wide as they need to be to fill the page.
        float cards(Renderer& r, const Rect& box, float y,
            std::initializer_list<Stat> stats) const
        {
            float width = cardWidth(box, int(stats.size()));
            float x = box.x;
            for (const Stat& stat : stats) {
                ui::statCard(r, Rect { x, y, width, kCardHeight }, stat.value,
                    stat.caption);
                x += width + theme::s4;
            }
            return y + kCardHeight + theme::s3;
        }

        /// The seven days, lit on the ones this console has ever crossed
        /// somebody. The bitmask is the trophy's own, Sunday first.
        float weekdayStrip(Renderer& r, const Rect& box, float y, uint8_t mask) const
        {
            static const char* kDays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri",
                "Sat" };

            // Card height, so the strip sits in the grid rather than beside it.
            Rect strip { box.x, y, box.w, kCardHeight };
            r.roundRect(strip, theme::r3, theme::bg1);
            Rect inner = strip.inset(theme::s5, theme::s5);

            TextStyle label;
            label.size = theme::textXs;
            label.color = theme::fg3;

            float dotY = inner.y + 14.0f;
            float pitch = inner.w / 7.0f;
            for (int day = 0; day < 7; day++) {
                float cx = inner.x + pitch * (float(day) + 0.5f);
                bool lit = (mask & (1u << day)) != 0;
                r.circle(cx, dotY, 14.0f, lit ? theme::accent : theme::bg3);
                r.text(Rect { cx - pitch * 0.5f, dotY + 14.0f + theme::s1, pitch, 22.0f },
                    tr(kDays[day]), label, Align::Center, VAlign::Top);
            }

            TextStyle note;
            note.size = theme::textXs;
            note.color = theme::fg4;
            r.text(Rect { inner.x, inner.bottom() - 22.0f, inner.w, 22.0f },
                tr("days you have crossed somebody"), note, Align::Left, VAlign::Top);

            return y + strip.h + theme::s3;
        }

        /// How many of each metal, counted the way the trophy screen counts
        /// them so the two cannot disagree.
        float trophyRow(Renderer& r, const Rect& box, float y, const Store& store,
            const TrophyFacts& facts) const
        {
            std::vector<uint8_t> state = trophyState(store, facts);
            const std::vector<Trophy>& all = trophies();

            int done[4] = { 0, 0, 0, 0 };
            int total[4] = { 0, 0, 0, 0 };
            for (size_t i = 0; i < all.size(); i++) {
                int tier = static_cast<int>(all[i].tier);
                if (tier < 0 || tier > 3)
                    continue;
                total[tier]++;
                if (i < state.size() && state[i])
                    done[tier]++;
            }

            // One card per metal. How many altogether is the first thing the
            // screen behind this one says, so it is not repeated here.
            float width = cardWidth(box, 4);
            float x = box.x;
            for (int tier = 0; tier < 4; tier++) {
                ui::statCard(r, Rect { x, y, width, kCardHeight },
                    format("%d / %d", done[tier], total[tier]), tr(tierName(Tier(tier))));
                x += width + theme::s4;
            }
            return y + kCardHeight + theme::s3;
        }

        /// A span in days, said in the largest unit that keeps it honest.
        static std::string days(uint32_t count)
        {
            if (count == 0)
                return "-";
            if (count < 60)
                return format(tr("%u days"), count);
            if (count < 730)
                return format(tr("%u months"), count / 30);
            return format(tr("%u years"), count / 365);
        }

        ui::ScrollView m_scroll;
        Rect m_pageArea {};
        bool m_dragging = false;
    };

} // namespace

std::unique_ptr<Scene> makeStatsScene() { return std::make_unique<StatsScene>(); }

} // namespace nxp
