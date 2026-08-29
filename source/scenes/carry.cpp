#include "app.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {

// One line of the catalogue: a heading, or a thing you can carry.
//
// Headings are rows the cursor steps over. Sixty-eight objects in one
// undifferentiated column is a wall to scroll through, and the grouping is not
// decoration - it says what kind of game the thing came out of, which is how
// somebody looks for one.
struct Entry {
    const char* heading; // set for a heading row
    const char* item;    // set for a choosable row
};

// Things to carry.
//
// Deliberately generic: a purple radish seed and a rally ghost belong to no
// particular game, and a list naming real ones would be a list that dates and
// a trademark problem besides. They are written the way the passes read -
// concrete, a little worn, specific enough to start a conversation. "Watering
// can, dented" is a better thing to hand somebody than "watering can".
constexpr Entry kCatalogue[] = {
    { "Soil and patience", nullptr },
    { nullptr, "Purple radish seed" },
    { nullptr, "Turnip crate" },
    { nullptr, "Bell jar of soil" },
    { nullptr, "Watering can, dented" },
    { nullptr, "Blue tulip cutting" },
    { nullptr, "Almanac, page 12 torn" },
    { nullptr, "Windfall pear" },
    { nullptr, "Beehive frame" },
    { "Engines", nullptr },
    { nullptr, "Rally ghost, Dune Pass" },
    { nullptr, "Night decal" },
    { nullptr, "Spare gearbox" },
    { nullptr, "Tyre set, worn" },
    { nullptr, "Pit radio" },
    { nullptr, "Hand-drawn track map" },
    { nullptr, "Stopwatch, cracked" },
    { "Down in the dark", nullptr },
    { nullptr, "Lantern shard" },
    { nullptr, "Map fragment 3 of 9" },
    { nullptr, "Rope, forty metres" },
    { nullptr, "Cave moss" },
    { nullptr, "Bell rubbing" },
    { nullptr, "Iron key, unlabelled" },
    { nullptr, "Chalk, half a stick" },
    { nullptr, "Rusted compass" },
    { "Puzzles", nullptr },
    { nullptr, "Puzzle piece 14 of 30" },
    { nullptr, "Corner piece, gold" },
    { nullptr, "Metronome" },
    { nullptr, "Solved edge, framed" },
    { nullptr, "Tile, blank side up" },
    { nullptr, "Loop of knotted string" },
    { "Water", nullptr },
    { nullptr, "Bass, undersized" },
    { nullptr, "Lure, hand-tied" },
    { nullptr, "Tide table, annotated" },
    { nullptr, "Net, mended twice" },
    { nullptr, "Message bottle, empty" },
    { nullptr, "Salt-stained chart" },
    { nullptr, "Oar, one of a pair" },
    { "Kitchen", nullptr },
    { nullptr, "Sourdough starter" },
    { nullptr, "Knife, needs a stone" },
    { nullptr, "Recipe on a napkin" },
    { nullptr, "Copper pot, dented" },
    { nullptr, "Jar of something green" },
    { nullptr, "Salt from a good year" },
    { "Sound", nullptr },
    { nullptr, "Broken tambourine" },
    { nullptr, "Sheet music, bar 40 on" },
    { nullptr, "Reed, spare" },
    { nullptr, "Cassette, unlabelled" },
    { nullptr, "Tuning fork" },
    { nullptr, "Setlist from a small room" },
    { "Off-world", nullptr },
    { nullptr, "Fuel cell, half" },
    { nullptr, "Star chart, sector 7" },
    { nullptr, "Suit patch" },
    { nullptr, "Rock from somewhere else" },
    { nullptr, "Beacon, still blinking" },
    { nullptr, "Log tape, day 311" },
    { "Cards and boards", nullptr },
    { nullptr, "Card, bent corner" },
    { nullptr, "Dice, one chipped" },
    { nullptr, "Meeple, wrong colour" },
    { nullptr, "Rulebook, page 2 gone" },
    { nullptr, "Tally sheet, unfinished" },
    { nullptr, "Pawn from another set" },
    { "Keepsakes", nullptr },
    { nullptr, "Ticket stub" },
    { nullptr, "Pressed flower" },
    { nullptr, "Photo, someone's back" },
    { nullptr, "Coin, worn smooth" },
    { nullptr, "Key to nothing" },
    { nullptr, "Button, spare" },
    { nullptr, "Note in a hand not mine" },
    { nullptr, "Marble, cloudy" },
};

constexpr int kEntryCount = static_cast<int>(sizeof(kCatalogue) / sizeof(kCatalogue[0]));

// Pick up to four things to carry.
class CarryScene final : public Scene {
public:
    enum Zone : int {
        Zone_Row = Touch_SceneBase,
        Zone_Card,
    };

    CarryScene(std::vector<std::string> chosen, std::function<void(std::vector<std::string>)> onDone)
        : m_chosen(std::move(chosen))
        , m_onDone(std::move(onDone))
    {
    }

    void onEnter(App& app) override
    {
        // Start on the first thing, not on a heading.
        m_focus = 0;
        step(1, false);
    }

    void update(App& app, const Input& input, float dt) override
    {
        m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

        const Touch& touch = input.touch;
        if (touch.pressed)
            m_brakedTap = m_scroll.absorbPress();
        if (touch.down && touch.dragged && m_listArea.contains(touch.startX, touch.startY)) {
            m_scroll.drag(-touch.dy, dt);
            m_dragging = true;
        } else if (m_dragging && !touch.down) {
            m_scroll.release();
            m_dragging = false;
        }
        m_scroll.update(dt);

        if (input.back()) {
            if (m_onDone)
                m_onDone(m_chosen);
            app.popOverlay();
            return;
        }

        TouchTarget tap;
        if (!m_brakedTap && app.takeTap(tap) && tap.is(Zone_Row)
            && tap.index >= 0 && tap.index < kEntryCount) {
            if (kCatalogue[tap.index].item != nullptr) {
                m_focus = tap.index;
                toggle(app);
            }
            return;
        }

        if (input.navDown)
            step(1, true);
        if (input.navUp)
            step(-1, true);

        if (input.accept())
            toggle(app);
    }

    void draw(App& app, Renderer& r) override
    {
        app.hint("A", chosenAt(m_focus) ? "put it back" : "carry it");
        app.hint("B", "done");

        constexpr float kWidth = 1000.0f;
        constexpr float kHeight = 700.0f;
        Rect box { Renderer::DesignWidth * 0.5f - kWidth * 0.5f,
            Renderer::DesignHeight * 0.5f - kHeight * 0.5f - 20.0f, kWidth, kHeight };

        app.touchZone(box, Zone_Card);
        r.roundRect(box, theme::r5, theme::bg1);
        r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);

        Rect inner = box.inset(theme::s7, theme::s7);
        float y = inner.y;

        TextStyle eyebrow;
        eyebrow.size = theme::textXs;
        eyebrow.weight = FontWeight::Bold;
        eyebrow.color = theme::teal;
        eyebrow.tracking = theme::trackingWider;
        eyebrow.uppercase = true;
        r.text(inner.x, y, "on your pass", eyebrow);
        y += eyebrow.size * theme::leadingNormal + theme::s3;

        TextStyle title;
        title.size = theme::textXl;
        title.weight = FontWeight::Bold;
        title.color = theme::fg1;
        title.tracking = theme::trackingTight;
        r.text(inner.x, y, "What you carry", title);

        // The count, right-aligned against the title. Four is the limit and
        // knowing where you are against it is the whole question here.
        TextStyle count;
        count.size = theme::textBase;
        count.weight = FontWeight::Medium;
        count.color = m_chosen.size() >= kMaxCarry ? theme::accent : theme::fg3;
        r.text(Rect { inner.x, y, inner.w, title.size },
            format("%zu of %d", m_chosen.size(), kMaxCarry), count, Align::Right,
            VAlign::Middle);
        y += title.size * theme::leadingTight + theme::s5;

        m_listArea = Rect { inner.x, y, inner.w, inner.bottom() - y };
        drawList(app, r, m_listArea);
    }

private:
    static constexpr float kRowHeight = 76.0f;
    static constexpr float kHeadingHeight = 58.0f;
    static constexpr float kRowGap = 6.0f;
    static constexpr size_t kMaxCarry = 4;

    static float heightOf(int index)
    {
        return kCatalogue[index].heading != nullptr ? kHeadingHeight : kRowHeight;
    }

    static float topOf(int index)
    {
        float y = 0.0f;
        for (int i = 0; i < index; i++)
            y += heightOf(i) + kRowGap;
        return y;
    }

    bool chosenAt(int index) const
    {
        const char* item = kCatalogue[index].item;
        if (item == nullptr)
            return false;
        return std::find(m_chosen.begin(), m_chosen.end(), std::string(item))
            != m_chosen.end();
    }

    // Moves the cursor by one choosable row, stepping over headings.
    void step(int delta, bool reveal)
    {
        int at = m_focus;
        for (int guard = 0; guard < kEntryCount; guard++) {
            if (kCatalogue[at].item != nullptr && (guard > 0 || delta == 0))
                break;
            at += delta;
            if (at < 0 || at >= kEntryCount) {
                at = std::max(0, std::min(at, kEntryCount - 1));
                if (kCatalogue[at].item != nullptr)
                    break;
                delta = -delta;
            }
        }
        if (kCatalogue[at].item == nullptr)
            return;

        m_focus = at;
        if (reveal)
            m_scroll.centerOn(topOf(m_focus) + kRowHeight * 0.5f);
    }

    void toggle(App& app)
    {
        const char* item = kCatalogue[m_focus].item;
        if (item == nullptr)
            return;

        auto at = std::find(m_chosen.begin(), m_chosen.end(), std::string(item));
        if (at != m_chosen.end()) {
            m_chosen.erase(at);
            return;
        }

        if (m_chosen.size() >= kMaxCarry) {
            // Refused rather than silently dropping the oldest: a pass that
            // quietly rewrites itself is worse than one that says no.
            app.toast("Four is the limit",
                "Put something back before picking this up.");
            return;
        }
        m_chosen.emplace_back(item);
    }

    void drawList(App& app, Renderer& r, const Rect& box)
    {
        m_scroll.setBounds(box.h, topOf(kEntryCount));

        r.pushClipVertical(box.inset(0.0f, -theme::focusRoom));
        float y = box.y - m_scroll.offset();
        for (int i = 0; i < kEntryCount; i++) {
            float height = heightOf(i);
            Rect row { box.x, y, box.w - theme::s6, height };
            if (row.bottom() > box.y - height && row.y < box.bottom() + height)
                drawRow(app, r, row, i);
            y += height + kRowGap;
        }
        r.popClip();

        if (m_scroll.scrollable())
            ui::scrollbar(r, Rect { box.right() - 8.0f, box.y, 8.0f, box.h },
                m_scroll.progress(), m_scroll.visibleFraction());
    }

    void drawRow(App& app, Renderer& r, const Rect& box, int index)
    {
        const Entry& entry = kCatalogue[index];

        if (entry.heading != nullptr) {
            TextStyle heading;
            heading.size = theme::textXs;
            heading.weight = FontWeight::Bold;
            heading.color = theme::fg3;
            heading.tracking = theme::trackingWider;
            heading.uppercase = true;
            r.text(Rect { box.x + theme::s2, box.y, box.w, box.h }, entry.heading,
                heading, Align::Left, VAlign::Bottom);
            return;
        }

        app.touchZone(box, Zone_Row, index);

        bool focused = index == m_focus;
        bool taken = chosenAt(index);
        float focus = app.touchHeld(Zone_Row, index)
            ? 1.0f
            : (focused ? 0.7f + 0.3f * m_pulse : 0.0f);
        ui::card(r, box, focus, focused || taken ? theme::bg2 : theme::bg1, theme::r3);

        Rect inner = box.inset(theme::s5, theme::s3);

        // A filled tick where a thing is being carried, an empty ring where it
        // is not: the row says which without the label having to.
        float mark = 30.0f;
        Rect box2 { inner.x, inner.centerY() - mark * 0.5f, mark, mark };
        if (taken) {
            r.roundRect(box2, mark * 0.28f, theme::accent);
            ui::icon(r, box2.inset(6.0f), ui::Icon::Check, theme::bg0, 3.0f);
        } else {
            r.strokeRect(box2, mark * 0.28f, theme::stroke, theme::stroke3);
        }

        TextStyle label;
        label.size = theme::textBase;
        label.weight = taken ? FontWeight::Bold : FontWeight::Regular;
        label.color = taken ? theme::fg1 : theme::fg2;
        r.text(Rect { inner.x + mark + theme::s4, inner.y,
                   inner.w - mark - theme::s4, inner.h },
            entry.item, label, Align::Left, VAlign::Middle);
    }

    std::vector<std::string> m_chosen;
    std::function<void(std::vector<std::string>)> m_onDone;

    int m_focus = 0;
    float m_pulse = 0.0f;

    ui::ScrollView m_scroll;
    Rect m_listArea {};
    bool m_dragging = false;
    bool m_brakedTap = false;
};

} // namespace

std::unique_ptr<Scene> makeCarryScene(std::vector<std::string> chosen,
    std::function<void(std::vector<std::string>)> onDone)
{
    return std::unique_ptr<Scene>(new CarryScene(std::move(chosen), std::move(onDone)));
}

} // namespace nxp
