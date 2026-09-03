#include "app.h"
#include "core/store.h"
#include "core/trophies.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nxp {

namespace {

    // The metals. Not in the palette, because they are the same three colours
    // in both themes - a bronze that changed with the theme would stop being
    // bronze - and each is picked to sit on the light canvas as well as the
    // dark one. Platinum borrows the palette's teal, which is the app's other
    // colour and already solved for both.
    Color tierColor(Tier tier)
    {
        switch (tier) {
        case Tier::Bronze:
            return Color::hex(0xB08D57);
        case Tier::Silver:
            return Color::hex(0x9AA3AE);
        case Tier::Gold:
            return theme::accent;
        case Tier::Platinum:
        default:
            return theme::teal;
        }
    }

    // What this console has done.
    //
    // Every line is a question asked of the store when the screen draws:
    // "have you crossed a hundred consoles", "is every picture finished". The
    // only thing written down is the date the answer first came back yes, so
    // there is no second copy of the truth to go stale, and a restored backup
    // simply re-derives what is true of the data it restored.
    //
    // The table is expected to grow. Nothing here is keyed by position, the
    // list scrolls, and the totals are counted rather than written down, so a
    // new trophy is one entry in kTrophies and nothing else.
    class TrophiesScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Row = Touch_SceneBase,
        };

        enum Filter : int {
            Filter_All = 0,
            Filter_Earned,
            Filter_Locked,
            Filter_Count,
        };

        void onEnter(App& app) override
        {
            m_row = 0;
            m_scroll.stop();
            rebuild(app);
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            rebuild(app);

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

            if (input.pressed(HidNpadButton_X)) {
                m_filter = (m_filter + 1) % Filter_Count;
                m_row = 0;
                m_scroll.stop();
                rebuild(app);
                return;
            }

            int count = int(m_view.size());
            if (count == 0)
                return;

            TouchTarget tap;
            if (!m_brakedTap && app.takeTap(tap) && tap.is(Zone_Row) && tap.index >= 0
                && tap.index < count) {
                m_row = tap.index;
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
        }

        void draw(App& app, Renderer& r) override
        {
            rebuild(app);

            app.hint("X", format("show %s", filterName((m_filter + 1) % Filter_Count)));

            Rect area = app.contentArea();
            Rect content { area.x + theme::edge, area.y + theme::s8,
                area.w - theme::edge * 2.0f, area.h - theme::s8 - theme::s7 };

            float y = content.y;
            ui::eyebrow(r, Rect { content.x, y, content.w, 34.0f }, "trophies");
            y += 40.0f;

            const std::vector<Trophy>& all = trophies();
            TextStyle title;
            title.size = theme::text3xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            r.text(content.x, y, format("%d of %zu", m_earnedCount, all.size()), title);

            // What the list is showing, then the tiers, opposite the count -
            // the shape the collection puts its sort order in, because it is
            // the same statement: which slice of a list is on screen, and how
            // much of it there is.
            float line = title.size * theme::leadingTight;
            TextStyle meta;
            meta.size = theme::textSm;
            meta.color = theme::fg3;
            r.text(Rect { content.x, y, content.w, line },
                format("%s - %s", filterName(m_filter), tallyText().c_str()), meta,
                Align::Right, VAlign::Middle);
            y += line + theme::s3;

            // How far along, as one bar. The same shape the puzzles list uses
            // for a picture, because it is the same kind of statement.
            Rect track { content.x, y, content.w, 8.0f };
            r.roundRect(track, 4.0f, theme::bg2);
            if (!all.empty() && m_earnedCount > 0) {
                float fraction = float(m_earnedCount) / float(all.size());
                r.roundRect(Rect { track.x, track.y, track.w * fraction, track.h }, 4.0f,
                    theme::accent);
            }
            y += track.h + theme::s5;

            Rect list { content.x, y, content.w, content.bottom() - y };
            m_listArea = list;
            float total = float(m_view.size()) * (kRowHeight + theme::s3) - theme::s3;
            m_scroll.setBounds(list.h, std::max(0.0f, total));

            if (m_view.empty()) {
                TextStyle empty;
                empty.size = theme::textBase;
                empty.color = theme::fg3;
                r.text(list.x, list.y,
                    m_filter == Filter_Earned ? "Nothing earned yet. Go and meet somebody."
                                              : "Everything here is earned.",
                    empty);
                return;
            }

            r.pushClipVertical(list.inset(0.0f, -theme::focusRoom));
            float rowY = list.y - m_scroll.offset();
            for (size_t i = 0; i < m_view.size(); i++) {
                Rect row { list.x, rowY, list.w - kScrollGutter, kRowHeight };
                if (row.bottom() >= list.y - theme::focusRoom
                    && row.y <= list.bottom() + theme::focusRoom)
                    drawRow(app, r, row, m_view[i], int(i));
                rowY += kRowHeight + theme::s3;
            }
            r.popClip();

            if (m_scroll.scrollable()) {
                ui::scrollbar(r, Rect { list.right() - 8.0f, list.y, 8.0f, list.h },
                    m_scroll.progress(), m_scroll.visibleFraction());
            }
        }

    private:
        static constexpr float kRowHeight = 120.0f;
        static constexpr float kScrollGutter = 24.0f;
        static constexpr float kMedal = 64.0f;

        // Read every frame, because that is the whole idea: the screen shows
        // what is true of the store now, not what something once wrote down.
        void rebuild(App& app)
        {
            m_state = trophyState(app.store(), trophyFacts(app.store()));

            const std::vector<Trophy>& all = trophies();
            m_earnedCount = 0;
            for (size_t i = 0; i < all.size() && i < m_state.size(); i++) {
                if (m_state[i])
                    m_earnedCount++;
            }

            m_view.clear();
            for (size_t i = 0; i < all.size(); i++) {
                bool earned = i < m_state.size() && m_state[i];
                if (m_filter == Filter_Earned && !earned)
                    continue;
                if (m_filter == Filter_Locked && earned)
                    continue;
                m_view.push_back(int(i));
            }
            m_row = std::min(std::max(m_row, 0), std::max(0, int(m_view.size()) - 1));
        }

        std::string tallyText() const
        {
            const std::vector<Trophy>& all = trophies();
            int done[4] = { 0, 0, 0, 0 };
            int total[4] = { 0, 0, 0, 0 };
            for (size_t i = 0; i < all.size(); i++) {
                int tier = int(all[i].tier);
                total[tier]++;
                if (i < m_state.size() && m_state[i])
                    done[tier]++;
            }
            std::string out;
            for (int tier = 0; tier < 4; tier++) {
                if (total[tier] == 0)
                    continue;
                if (!out.empty())
                    out += "   ";
                out += format("%d/%d %s", done[tier], total[tier],
                    tierName(Tier(tier)));
            }
            return out;
        }

        static const char* filterName(int filter)
        {
            switch (filter) {
            case Filter_Earned:
                return "earned only";
            case Filter_Locked:
                return "still to earn";
            case Filter_All:
            default:
                return "all of them";
            }
        }

        void drawRow(App& app, Renderer& r, const Rect& box, int index, int slot)
        {
            const Trophy& trophy = trophies()[size_t(index)];
            bool earned = size_t(index) < m_state.size() && m_state[size_t(index)];
            bool focused = slot == m_row;

            app.touchZone(box, Zone_Row, slot);
            float focus = focused
                ? (app.touchHeld(Zone_Row, slot) ? 1.0f : 0.7f + 0.3f * m_pulse)
                : 0.0f;
            ui::card(r, box, focus, focused ? theme::bg2 : theme::bg1, theme::r3);

            Rect inner = box.inset(theme::s6, theme::s5);

            // The medal: the cup in its metal on a wash of the same colour,
            // and the same cup in the disabled ink when it is not earned yet -
            // so a locked row reads as the same kind of thing, unlit.
            Color metal = earned ? tierColor(trophy.tier) : theme::fg4;
            Rect medal { inner.x, inner.centerY() - kMedal * 0.5f, kMedal, kMedal };
            r.circle(medal.centerX(), medal.centerY(), kMedal * 0.5f,
                earned ? metal.scaleAlpha(0.20f) : theme::bg3);
            ui::icon(r, medal.inset(kMedal * 0.22f), ui::Icon::Trophy, metal, 2.5f);

            // The metal, and when it happened, along the right edge. Both are
            // about the trophy and putting them here
            // leaves the description in place on every row - a row that is
            // earned should still say what it was for.
            TextStyle right;
            right.size = theme::textSm;
            right.weight = FontWeight::Medium;
            right.color = earned ? metal : theme::fg4;
            right.tracking = theme::trackingWide;
            std::string tier = tierName(trophy.tier);

            TextStyle when;
            when.size = theme::textSm;
            when.color = theme::fg3;
            std::string date;
            if (earned) {
                uint64_t at = app.store().trophyDate(trophy.id);
                date = at != 0
                    ? format("earned %s", relativeTime(at, nowUnix()).c_str())
                    : std::string("earned");
            }

            // The column is as wide as its widest line, so the text column
            // gives up exactly what this needs and no more.
            float rightW = std::max(r.measure(tier, right), r.measure(date, when));
            Rect rightBox { inner.right() - rightW, inner.y, rightW, inner.h };

            float lineH = theme::textSm * theme::leadingSnug;
            if (earned) {
                // Two lines, sitting on the same baselines as the name and the
                // description opposite them.
                r.text(rightBox, tier, right, Align::Right, VAlign::Top);
                r.text(Rect { rightBox.x, rightBox.y + lineH + 6.0f, rightW, lineH }, date,
                    when, Align::Right, VAlign::Top);
            } else {
                r.text(rightBox, tier, right, Align::Right, VAlign::Middle);
            }

            float textX = medal.right() + theme::s5;
            float textW = std::max(rightBox.x - theme::s5 - textX, 120.0f);

            TextStyle name;
            name.size = theme::textBase;
            name.weight = FontWeight::Bold;
            name.color = earned ? theme::fg1 : theme::fg3;
            name.leading = theme::leadingSnug;
            r.text(textX, inner.y, r.ellipsize(trophy.name, name, textW), name);

            // What it takes, on every row whether it is earned or not: the
            // list reads as a record of what happened, which needs the reason
            // as much as the date.
            TextStyle sub;
            sub.size = theme::textSm;
            sub.color = earned ? theme::fg2 : theme::fg3;
            sub.leading = theme::leadingNormal;
            r.text(textX, inner.y + name.size * theme::leadingSnug + 6.0f,
                r.ellipsize(trophy.hint, sub, textW), sub);
        }

        std::vector<uint8_t> m_state;
        std::vector<int> m_view; // indices into trophies(), after the filter
        int m_earnedCount = 0;
        int m_filter = Filter_All;
        int m_row = 0;
        float m_pulse = 0.0f;

        ui::ScrollView m_scroll;
        Rect m_listArea {};
        bool m_dragging = false;
        bool m_brakedTap = false;
    };
}

std::unique_ptr<Scene> makeTrophiesScene() { return std::make_unique<TrophiesScene>(); }

} // namespace nxp
