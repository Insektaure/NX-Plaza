#include "app.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/scroll.h"
#include "ui/mii_render.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {

// Everything you have ever collected, as a grid. The mockups list this under
// "add the collection grid screen"; it is the natural home for passes once
// they stop being news.
class CollectionScene final : public Scene {
public:
    enum Zone : int {
        Zone_Cell = Touch_SceneBase,
    };

    // Six across, matching the inbox tile rhythm. One column count for every
    // console: a Lite cannot dock, so there is no second layout.
    static constexpr int kColumns = 6;

    void onEnter(App& app) override
    {
        syncCrossings(app);
        restoreCursor(app);
    }

    void update(App& app, const Input& input, float dt) override
    {
        syncCrossings(app);
        restoreCursor(app);

        int count = static_cast<int>(m_crossings.size());
        int columns = kColumns;

        const Touch& touch = input.touch;
        if (touch.pressed)
            m_brakedTap = m_scroll.absorbPress();

        if (touch.down && touch.dragged && m_gridRect.contains(touch.startX, touch.startY)) {
            m_scroll.drag(-touch.dy, dt);
            m_dragging = true;
        } else if (m_dragging && !touch.down) {
            m_scroll.release();
            clampSelectionToView(columns);
            m_dragging = false;
        }
        m_scroll.update(dt);

        TouchTarget tap;
        if (!m_brakedTap && app.takeTap(tap) && tap.is(Zone_Cell) && tap.index < count) {
            m_selected = tap.index;
            app.openEncounter(m_crossings[static_cast<size_t>(m_selected)].id, orderedIds());
            return;
        }

        if (count > 0) {
            int before = m_selected;
            if (input.navRight)
                m_selected = std::min(m_selected + 1, count - 1);
            if (input.navLeft)
                m_selected = std::max(m_selected - 1, 0);
            if (input.navDown)
                m_selected = std::min(m_selected + columns, count - 1);
            if (input.navUp)
                m_selected = std::max(m_selected - columns, 0);
            if (m_selected != before)
                revealSelection(columns);

            if (input.accept())
                app.openEncounter(m_crossings[static_cast<size_t>(m_selected)].id, orderedIds());

            if (input.pressed(HidNpadButton_Y) && m_selected >= 0
                && m_selected < static_cast<int>(m_crossings.size())) {
                const Crossing& c = m_crossings[static_cast<size_t>(m_selected)];
                bool on = !app.store().isFavourite(c.id);
                app.store().setFavourite(c.id, on);
                app.toast(on ? "Starred " + c.pass.handle : "Unstarred " + c.pass.handle,
                    on ? "Kept when the collection fills up."
                       : "No longer kept when the collection fills up.");

                // Only this screen knows its order depends on the star.
                // The store deliberately does not bump the generation for a
                // favourite - that would make the plaza re-copy the collection
                // and The Square reshuffle its whole cast - so the re-sort is
                // asked for here, by the one view that cares.
                if (m_sort == Sort_Starred)
                    m_sortDirty = true;
            }

            if (input.pressed(HidNpadButton_X)) {
                m_sort = (m_sort + 1) % Sort_Count;
                // Re-sorted on the next sync, which also keeps the cursor on
                // the card it was on rather than on the slot.
                m_sortDirty = true;
            }
        }

        m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
    }

    void draw(App& app, Renderer& r) override
    {
        Rect area = app.contentArea();
        float edge = theme::edge;
        Rect content { area.x + edge, area.y + theme::s8, area.w - edge * 2.0f,
            area.h - theme::s8 };

        ui::eyebrow(r, Rect { content.x, content.y, content.w, 32.0f }, "collection");

        TextStyle title;
        title.size = theme::text2xl;
        title.weight = FontWeight::Bold;
        title.color = theme::fg1;
        title.tracking = theme::trackingTight;

        Stats stats = app.store().stats();
        r.text(content.x, content.y + 40.0f,
            format("%u %s met", stats.uniquePeople, stats.uniquePeople == 1 ? "person" : "people"),
            title);

        TextStyle meta;
        meta.size = theme::textSm;
        meta.color = theme::fg3;
        r.text(Rect { content.x, content.y + 40.0f, content.w, title.size * theme::leadingSnug },
            format("%s - %u crossings in total", sortLabel(), stats.totalCrossings),
            meta, Align::Right, VAlign::Bottom);

        app.hint("A", "open");
        app.hint("X", nextSortHint());
        if (!m_crossings.empty()) {
            bool starred = m_selected >= 0 && m_selected < static_cast<int>(m_crossings.size())
                && app.store().isFavourite(m_crossings[static_cast<size_t>(m_selected)].id);
            app.hint("Y", starred ? "unstar" : "star");
        }

        float titleBlock = 40.0f + title.size * theme::leadingSnug + theme::s5;

        // The grid stops short of the control strip.
        //
        // contentArea() already ends where the strip begins, and this screen's
        // content rect only takes an inset off the top, so the grid ran to the
        // exact pixel the strip starts on. Scrolled to the bottom, the last row
        // of cards ended flush against it: no card edge, no rounded corner, and
        // a focused card's ring -- which is drawn *outside* the card -- clipped
        // away entirely. It read as a row cut in half.
        //
        // Shrinking the viewport also lifts the last row clear, because the
        // scroll can now travel this much further.
        constexpr float kBottomMargin = theme::s6;
        Rect grid { content.x, content.y + titleBlock, content.w,
            content.h - titleBlock - kBottomMargin };

        if (m_crossings.empty()) {
            TextStyle empty;
            empty.size = theme::textBase;
            empty.color = theme::fg3;
            r.textWrapped(Rect { grid.x, grid.y + 48.0f, std::min(grid.w, 960.0f), 130.0f },
                "Nothing collected yet. The first pass you receive lands here and stays.",
                empty, 2);
            return;
        }

        int columns = kColumns;
        float gap = theme::s4;
        float cellWidth = (grid.w - gap * static_cast<float>(columns - 1))
            / static_cast<float>(columns);
        // 404/300, the inbox tile's own ratio.
        float cellHeight = cellWidth * (404.0f / 300.0f);

        float totalRows = std::ceil(static_cast<float>(m_crossings.size())
            / static_cast<float>(columns));

        // Layout is known here, so this is where the scroll bounds and the row
        // pitch that update() needs come from.
        m_gridRect = grid;
        m_rowPitch = cellHeight + gap;
        m_cellHeight = cellHeight;
        m_scroll.setBounds(grid.h, std::max(0.0f, totalRows * m_rowPitch - gap));
        float offset = m_scroll.offset();

        r.pushClipVertical(grid.inset(0.0f, -theme::focusRoom));

        // A focused cell grows and carries a ring, and the gap between cells is
        // narrower than the two together. Drawn in order it would be painted
        // over by whichever neighbour comes after it, so it is held back and
        // drawn on top of the grid.
        int liftedIndex = -1;
        Rect liftedCell {};
        float liftedFocus = 0.0f;

        for (size_t i = 0; i < m_crossings.size(); i++) {
            int column = static_cast<int>(i) % columns;
            float row = std::floor(static_cast<float>(i) / static_cast<float>(columns));
            Rect cell {
                grid.x + static_cast<float>(column) * (cellWidth + gap),
                grid.y - offset + row * m_rowPitch,
                cellWidth,
                cellHeight
            };
            if (cell.bottom() < grid.y - cellHeight || cell.y > grid.bottom() + cellHeight)
                continue;

            app.touchZone(cell, Zone_Cell, static_cast<int>(i));
            float focus = app.touchHeld(Zone_Cell, static_cast<int>(i))
                ? 1.0f
                : (static_cast<int>(i) == m_selected ? 0.7f + 0.3f * m_pulse : 0.0f);
            if (focus > 0.001f) {
                liftedIndex = static_cast<int>(i);
                liftedCell = cell;
                liftedFocus = focus;
                continue;
            }
            drawCell(app, r, cell, m_crossings[i], 0.0f);
        }

        if (liftedIndex >= 0)
            drawCell(app, r, liftedCell, m_crossings[static_cast<size_t>(liftedIndex)],
                liftedFocus);

        r.popClip();

        if (m_scroll.scrollable()) {
            ui::scrollbar(r, Rect { grid.right() + 14.0f, grid.y, 8.0f, grid.h },
                m_scroll.progress(), m_scroll.visibleFraction());
        }
    }

private:
    void revealSelection(int columns)
    {
        if (m_rowPitch <= 0.0f)
            return;
        float row = std::floor(static_cast<float>(m_selected) / static_cast<float>(columns));
        m_scroll.centerOn(row * m_rowPitch + m_cellHeight * 0.5f);
    }

    // The ids as they are laid out right now, so the detail view steps through
    // them in the order the screen is showing - which for this one depends on
    // the sort, not on the store.
    std::vector<std::string> orderedIds() const
    {
        std::vector<std::string> ids;
        ids.reserve(m_crossings.size());
        for (const Crossing& c : m_crossings)
            ids.push_back(c.id);
        return ids;
    }

    // After a drag, put the cursor on a row the user can actually see.
    void clampSelectionToView(int columns)
    {
        if (m_rowPitch <= 0.0f || m_crossings.empty())
            return;

        float middle = m_scroll.offset() + m_scroll.viewport() * 0.5f;
        int row = static_cast<int>(std::floor((middle - m_cellHeight * 0.5f) / m_rowPitch + 0.5f));
        row = std::max(row, 0);

        int column = m_selected % columns;
        int candidate = row * columns + column;
        m_selected = std::min(std::max(candidate, 0), static_cast<int>(m_crossings.size()) - 1);
    }

    // Copies the collection out of the store only when it has actually changed,
    // and sorts only when it has been copied or the sort was toggled.
    //
    // Both used to happen every frame. A copy is eight heap strings per card and
    // the sort is O(n log n) on top; at five thousand cards that was most of a
    // frame's budget spent rebuilding a list that changes when a pass arrives.
    void syncCrossings(App& app)
    {
        uint64_t generation = app.store().crossingsGeneration();
        if (generation == m_generation && !m_sortDirty)
            return;

        // Which pass the cursor is on, not which slot: sorting moves the slots,
        // and a pass arriving pushes every one of them along.
        std::string wanted;
        if (m_selected >= 0 && m_selected < static_cast<int>(m_crossings.size()))
            wanted = m_crossings[static_cast<size_t>(m_selected)].id;

        if (generation != m_generation) {
            m_crossings = app.store().crossings();
            m_generation = generation;
        }
        sort(app);
        m_sortDirty = false;

        int count = static_cast<int>(m_crossings.size());
        m_selected = count == 0 ? 0 : std::min(std::max(m_selected, 0), count - 1);

        if (wanted.empty())
            return;
        for (size_t i = 0; i < m_crossings.size(); i++) {
            if (m_crossings[i].id == wanted) {
                m_selected = static_cast<int>(i);
                break;
            }
        }
    }

    // Puts the cursor back on the pass the detail view was last showing, and
    // scrolls to it. Separate from syncCrossings because closing a card the user
    // had already read changes nothing in the store, so there is no generation
    // to notice - and the cursor still has to move.
    void restoreCursor(App& app)
    {
        std::string wanted = app.takeLastViewedCrossing();
        if (wanted.empty())
            return;
        for (size_t i = 0; i < m_crossings.size(); i++) {
            if (m_crossings[i].id == wanted) {
                m_selected = static_cast<int>(i);
                revealSelection(kColumns);
                return;
            }
        }
    }

    const char* sortLabel() const
    {
        switch (m_sort) {
        case Sort_Name: return "by name";
        case Sort_Starred: return "starred first";
        default: return "most recent first";
        }
    }

    const char* nextSortHint() const
    {
        switch (m_sort) {
        case Sort_Recent: return "sort by name";
        case Sort_Name: return "sort by starred";
        default: return "sort by recent";
        }
    }

    void sort(App& app)
    {
        if (m_sort == Sort_Name) {
            std::stable_sort(m_crossings.begin(), m_crossings.end(),
                [](const Crossing& a, const Crossing& b) {
                    return a.pass.handle < b.pass.handle;
                });
            return;
        }
        if (m_sort != Sort_Starred)
            return; // Recent is the order the store keeps

        // One lookup per card, not one per comparison. A comparator that asked
        // the store would take its lock 60000 for 5000 cards,
        // and this runs on the drawing thread.
        size_t n = m_crossings.size();
        std::vector<uint8_t> starred(n, 0);
        for (size_t i = 0; i < n; i++)
            starred[i] = app.store().isFavourite(m_crossings[i].id) ? 1 : 0;

        // Indices, because sorting the cards would leave the flags behind.
        // Stable, so within each group the store's newest-first order survives:
        // "starred first" and not "starred, then shuffled".
        std::vector<size_t> order(n);
        for (size_t i = 0; i < n; i++)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(),
            [&starred](size_t a, size_t b) { return starred[a] > starred[b]; });

        std::vector<Crossing> sorted;
        sorted.reserve(n);
        for (size_t i : order)
            sorted.push_back(std::move(m_crossings[i]));
        m_crossings = std::move(sorted);
    }

    void drawCell(App& app, Renderer& r, const Rect& box, const Crossing& crossing,
        float focus)
    {
        // The same lift a plaza tile does: the whole cell grows and everything
        // on it grows with it. Letting ui::card grow the surface on its own left
        // the portrait and the name at their original size, so the artwork came
        // away from the corners it is supposed to fill and a band of bare card
        // opened up around it.
        Rect cell = box;
        if (focus > 0.001f) {
            float grow = theme::focusGrow * focus;
            cell = box.inset(-box.w * grow, -box.h * grow);
        }

        r.roundRect(cell, theme::r3, theme::bg2);
        r.strokeRect(cell, theme::r3, theme::stroke, theme::stroke1);

        Rect top { cell.x, cell.y, cell.w, cell.h * (250.0f / 404.0f) };
        r.pushClip(top);
        ui::miiStage(r, top, crossing.pass.face(), crossing.pass.theme,
            ui::StageFigure { 0.52f, 0.56f, 0.30f, 0.50f, 0.0f, 0.17f }, theme::r3, 0.0f);
        r.popClip();

        if (!crossing.opened)
            r.circle(cell.right() - 22.0f, cell.y + 22.0f, 9.0f, theme::teal);

        // Top left, opposite the unread dot, so a card can carry both. On its
        // own disc: the stage behind it is a card theme of the pass's choosing
        // and an amber star on amber would vanish.
        if (app.store().isFavourite(crossing.id)) {
            float cx = cell.x + 26.0f;
            float cy = cell.y + 26.0f;
            r.circle(cx, cy, 17.0f, theme::bg0.scaleAlpha(0.72f));
            ui::icon(r, Rect { cx - 13.0f, cy - 13.0f, 26.0f, 26.0f }, ui::Icon::Star,
                theme::accent, 2.0f);
        }

        TextStyle name;
        name.size = theme::textMd;
        name.weight = FontWeight::Bold;
        name.color = theme::fg1;
        r.text(Rect { cell.x + theme::s5, top.bottom() + theme::s4, cell.w - theme::s5 * 2.0f,
                   40.0f },
            r.ellipsize(crossing.pass.handle, name, cell.w - theme::s5 * 2.0f), name,
            Align::Left, VAlign::Top);

        TextStyle meta;
        meta.size = theme::textXs;
        meta.color = theme::fg3;
        // 2 lines :
        // The time of last crossing
        // The number of times crossed
        float textWidth = cell.w - theme::s5 * 2.0f;
        float metaY = top.bottom() + theme::s4 + theme::textMd * theme::leadingSnug + 6.0f;

        r.text(Rect { cell.x + theme::s5, metaY, textWidth, 32.0f },
            r.ellipsize(relativeTime(crossing.lastSeen, nowUnix()), meta, textWidth),
            meta, Align::Left, VAlign::Top);

        // Only once it has happened more than once.
        // "met 1 times" is not a thing anybody needs told.
        if (crossing.count > 1) {
            r.text(Rect { cell.x + theme::s5, metaY + theme::textXs * theme::leadingNormal,
                       textWidth, 32.0f },
                r.ellipsize(format("met %u times", crossing.count), meta, textWidth),
                meta, Align::Left, VAlign::Top);
        }

        ui::focusRing(r, cell, theme::r3, focus);
    }

    std::vector<Crossing> m_crossings;
    int m_selected = 0;
    float m_pulse = 0.0f;
    // Three orders now, so a flag will not do. Recent is what the store hands
    // back, so it costs nothing; the other two reorder the copy.
    enum Sort : int { Sort_Recent = 0, Sort_Name, Sort_Starred, Sort_Count };
    int m_sort = Sort_Recent;

    ui::ScrollView m_scroll;
    Rect m_gridRect;
    // What the store looked like when m_crossings was copied from it.
    uint64_t m_generation = 0;
    bool m_sortDirty = false;

    float m_rowPitch = 0.0f;   // cached from draw
    float m_cellHeight = 0.0f;
    bool m_dragging = false;
    bool m_brakedTap = false; // this gesture only stopped a flick
};

} // namespace

std::unique_ptr<Scene> makeCollectionScene()
{
    return std::unique_ptr<Scene>(new CollectionScene());
}

} // namespace nxp
