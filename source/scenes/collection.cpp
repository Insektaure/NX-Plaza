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
        reload(app);
    }

    void update(App& app, const Input& input, float dt) override
    {
        reload(app);

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
            app.openEncounter(m_crossings[static_cast<size_t>(m_selected)].id);
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
                app.openEncounter(m_crossings[static_cast<size_t>(m_selected)].id);

            if (input.pressed(HidNpadButton_X)) {
                m_sortByName = !m_sortByName;
                sort();
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
            format("%s - %u crossings in total", m_sortByName ? "by name" : "most recent first",
                stats.totalCrossings),
            meta, Align::Right, VAlign::Bottom);

        app.hint("A", "open");
        app.hint("X", m_sortByName ? "sort by recent" : "sort by name");

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
            drawCell(r, cell, m_crossings[i], 0.0f);
        }

        if (liftedIndex >= 0)
            drawCell(r, liftedCell, m_crossings[static_cast<size_t>(liftedIndex)],
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

    void reload(App& app)
    {
        m_crossings = app.store().crossings();
        sort();
        int count = static_cast<int>(m_crossings.size());
        m_selected = count == 0 ? 0 : std::min(std::max(m_selected, 0), count - 1);
    }

    void sort()
    {
        if (!m_sortByName)
            return;
        std::stable_sort(m_crossings.begin(), m_crossings.end(),
            [](const Crossing& a, const Crossing& b) {
                return a.pass.handle < b.pass.handle;
            });
    }

    void drawCell(Renderer& r, const Rect& box, const Crossing& crossing, float focus)
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
    bool m_sortByName = false;

    ui::ScrollView m_scroll;
    Rect m_gridRect;
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
