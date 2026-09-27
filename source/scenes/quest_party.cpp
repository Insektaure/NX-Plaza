#include "app.h"
#include "core/i18n.h"
#include "core/quest_record.h"
#include "core/quest_rules.h"
#include "core/store.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace nxp {

namespace {

    // Who goes up the tower, chosen from the whole collection.
    //
    // It keeps nothing of its own. The party is a list of crossing ids held
    // by Store, set on every change; Store writes it to profile.json with its
    // next save, a couple of seconds later. The climb screen under this one
    // asks Store for the list every frame - the copy in memory, never the
    // file - and rebuilds its party only when the list has changed.
    class PartyScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Member = Touch_SceneBase, // a place along the top
            Zone_Cell,                     // somebody in the grid
            Zone_Filter,
            Zone_Sort,
            Zone_Back,
        };

        bool coversChrome() const override { return true; }

        void onEnter(App& app) override
        {
            gather(app);
            m_focus = Focus_Grid;
            m_pick = 0;
            m_place = 1;
            m_replacing = -1;
            m_scroll.stop();
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

            // Back from the gear screen, which is the one thing that can
            // change what anybody here is worth while this screen is up.
            if (m_redress) {
                m_redress = false;
                dressAll();
                reshape();
            }

            m_scroll.update(dt);
            const Touch& touch = input.touch;
            if (touch.pressed)
                m_braked = m_scroll.absorbPress();
            if (touch.down && touch.dragged && m_grid.contains(touch.x, touch.y)) {
                m_scroll.drag(-touch.dy, dt);
                m_dragging = true;
            } else if (m_dragging && !touch.down) {
                m_scroll.release();
                m_dragging = false;
            }

            TouchTarget tap;
            if (app.takeTap(tap) && !m_braked) {
                onTap(app, tap);
                return;
            }

            if (input.back()) {
                if (m_replacing >= 0) {
                    m_replacing = -1;
                    m_focus = Focus_Grid;
                } else {
                    app.popOverlay();
                }
                return;
            }

            // Narrowing and ordering, from anywhere. Not while choosing who
            // steps aside: the person being brought in would vanish from the
            // grid under a filter that does not show them.
            if (m_replacing < 0) {
                if (input.pressed(HidNpadButton_L))
                    setFilter((m_filter + kFilters - 1) % kFilters);
                if (input.pressed(HidNpadButton_R))
                    setFilter((m_filter + 1) % kFilters);
                if (input.pressed(HidNpadButton_X))
                    setSort((m_sort + 1) % kSorts);
            }
            if (input.pressed(HidNpadButton_Y)) {
                openGear(app);
                return;
            }

            if (m_focus == Focus_Party)
                updateParty(app, input);
            else
                updateGrid(app, input);
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);
            app.touchZone(r.viewport(), Touch_None);
            drawHints(app);
            drawHeader(app, r);
            drawParty(app, r);
            drawFilters(app, r);
            drawGrid(app, r);
            drawFooter(r);
        }

    private:
        // ------------------------------------------------------------ data

        struct Person {
            std::string id; // empty for your own Mii
            std::string name;
            Mii face;
            Sheet base;
            Sheet sheet; // with their gear on
            uint32_t crossed = 0;
            uint64_t met = 0; // first crossing, unix seconds
            int worn = 0;     // pegs with something on them
            int worth = 0;
        };

        // The same number the climb screen ranks its default party by: not a
        // stat, just a way of putting the strongest first.
        static int worthOf(const Sheet& s)
        {
            return int(s.hp) / 4 + int(s.atk) * 2 + int(s.def) + int(s.spd) + int(s.mp);
        }

        void gather(App& app)
        {
            Store& store = app.store();
            const Stats record = store.stats();
            const Pass mine = store.myPass();

            m_you = Person {};
            m_you.name = mine.handle.empty() ? std::string(tr("You")) : mine.handle;
            m_you.face = mine.face();
            m_you.base = sheetFor(m_you.face, record.uniquePeople, record.totalCrossings,
                mine.hours, mine.titles);

            m_people.clear();
            for (const Crossing& c : store.crossings()) {
                Person p;
                p.id = c.id;
                p.name = c.pass.handle.empty() ? std::string(tr("A stranger"))
                                               : c.pass.handle;
                p.face = c.pass.face();
                p.base = sheetFor(p.face, c.count, c.pass.met, c.pass.hours,
                    c.pass.titles);
                p.crossed = c.count;
                p.met = c.firstSeen;
                m_people.push_back(std::move(p));
            }
            m_slots = partySlots(record.uniquePeople);

            // Only the ids still in the collection, and no more than there
            // are places: the climb screen has already done the same, but
            // this screen is not to assume it opened after that one.
            m_chosen.clear();
            for (const std::string& id : store.questParty()) {
                if (int(m_chosen.size()) >= m_slots - 1)
                    break;
                if (indexOf(id) >= 0 && !chosen(id))
                    m_chosen.push_back(id);
            }
            dressAll();
            reshape();
        }

        void dressAll()
        {
            dress(m_you);
            for (Person& p : m_people)
                dress(p);
        }

        static void dress(Person& p)
        {
            const QuestRecord& record = QuestRecord::get();
            QuestRecord::Loadout gear = record.loadout(p.id);
            p.sheet = p.base;
            p.worn = 0;
            for (int slot = 0; slot < Slot_Count; slot++) {
                if (const Item* item = record.find(gear.worn[slot])) {
                    p.sheet += itemBonus(*item);
                    p.worn++;
                }
            }
            p.worth = worthOf(p.sheet);
        }

        int indexOf(const std::string& id) const
        {
            for (size_t i = 0; i < m_people.size(); i++) {
                if (m_people[i].id == id)
                    return int(i);
            }
            return -1;
        }

        bool chosen(const std::string& id) const
        {
            return std::find(m_chosen.begin(), m_chosen.end(), id) != m_chosen.end();
        }

        // Written on every change, so leaving by any route - B, +, a crash
        // two seconds later - keeps what was chosen.
        void remember(App& app) { app.store().setQuestParty(m_chosen); }

        // --------------------------------------------------- filter & sort

        static constexpr int kFilters = 1 + Class_Count; // everyone, then each class
        static constexpr int kSorts = 4;

        static const char* sortName(int sort)
        {
            switch (sort) {
            case 1:
                return "most crossed";
            case 2:
                return "name";
            case 3:
                return "recently met";
            default:
                return "strongest";
            }
        }

        static std::string folded(const std::string& s)
        {
            std::string out = s;
            for (char& c : out)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        // Who the grid shows, in what order. Rebuilt only when one of the
        // three things it depends on changes, not every frame.
        void reshape()
        {
            std::string keep = m_pick >= 0 && m_pick < int(m_shown.size())
                ? m_people[size_t(m_shown[size_t(m_pick)])].id
                : std::string();

            m_shown.clear();
            for (size_t i = 0; i < m_people.size(); i++) {
                if (m_filter == 0 || m_people[i].sheet.cls == m_filter - 1)
                    m_shown.push_back(int(i));
            }
            std::stable_sort(m_shown.begin(), m_shown.end(), [this](int a, int b) {
                const Person& pa = m_people[size_t(a)];
                const Person& pb = m_people[size_t(b)];
                switch (m_sort) {
                case 1:
                    if (pa.crossed != pb.crossed)
                        return pa.crossed > pb.crossed;
                    break;
                case 2: {
                    std::string na = folded(pa.name), nb = folded(pb.name);
                    if (na != nb)
                        return na < nb;
                    break;
                }
                case 3:
                    if (pa.met != pb.met)
                        return pa.met > pb.met;
                    break;
                default:
                    break;
                }
                return pa.worth > pb.worth;
            });

            // The cursor stays on whoever it was on, if they are still shown.
            m_pick = 0;
            for (size_t i = 0; i < m_shown.size(); i++) {
                if (m_people[size_t(m_shown[i])].id == keep)
                    m_pick = int(i);
            }
            if (m_focus == Focus_Grid)
                scrollToPick();
        }

        void setFilter(int which)
        {
            if (which == m_filter)
                return;
            m_filter = which;
            m_pick = 0;
            reshape();
            m_scroll.stop();
            m_scroll.centerOn(0.0f);
        }

        void setSort(int which)
        {
            m_sort = which;
            reshape();
        }

        // ------------------------------------------------------------ doing

        // The one who would be affected by A right now: whoever is under the
        // cursor, in the grid or along the top.
        const Person* highlighted() const
        {
            if (m_focus == Focus_Party) {
                if (m_place == 0)
                    return &m_you;
                if (m_place - 1 < int(m_chosen.size())) {
                    int at = indexOf(m_chosen[size_t(m_place - 1)]);
                    return at >= 0 ? &m_people[size_t(at)] : nullptr;
                }
                return nullptr;
            }
            if (m_pick >= 0 && m_pick < int(m_shown.size()))
                return &m_people[size_t(m_shown[size_t(m_pick)])];
            return nullptr;
        }

        // A on somebody in the grid: in if there is room, out if they are
        // in, and when the party is full, the top row lights up to ask who
        // steps aside. The old strip dropped the longest-standing pick
        // without a word, which was only ever right by accident.
        void pickFromGrid(App& app)
        {
            if (m_pick < 0 || m_pick >= int(m_shown.size()))
                return;
            int who = m_shown[size_t(m_pick)];
            const std::string& id = m_people[size_t(who)].id;
            if (chosen(id)) {
                m_chosen.erase(std::find(m_chosen.begin(), m_chosen.end(), id));
                remember(app);
                return;
            }
            if (int(m_chosen.size()) < m_slots - 1) {
                m_chosen.push_back(id);
                remember(app);
                return;
            }
            if (m_slots <= 1)
                return; // nobody but you goes, so there is nobody to swap
            m_replacing = who;
            m_focus = Focus_Party;
            m_place = 1;
        }

        // A on a place along the top.
        void pickFromParty(App& app)
        {
            if (m_replacing >= 0) {
                if (m_place >= 1 && m_place - 1 < int(m_chosen.size())) {
                    m_chosen[size_t(m_place - 1)] = m_people[size_t(m_replacing)].id;
                    remember(app);
                }
                m_replacing = -1;
                m_focus = Focus_Grid;
                return;
            }
            if (m_place == 0)
                return; // you always go
            if (m_place - 1 < int(m_chosen.size())) {
                m_chosen.erase(m_chosen.begin() + long(m_place - 1));
                remember(app);
                return;
            }
            // A free place: go and find somebody for it.
            m_focus = Focus_Grid;
            scrollToPick();
        }

        void openGear(App& app)
        {
            const Person* p = highlighted();
            if (!p)
                return;
            std::vector<GearPerson> one;
            one.push_back(GearPerson { p->id, p->name, p->face, p->base });
            m_redress = true;
            app.pushOverlay(makeQuestGearScene(std::move(one)));
        }

        void updateGrid(App& app, const Input& input)
        {
            int n = int(m_shown.size());
            if (input.navUp) {
                if (m_pick < kColumns) {
                    m_focus = Focus_Party;
                    m_place = std::min(m_place, m_slots - 1);
                    return;
                }
                step(-kColumns);
            }
            if (input.navDown)
                step(kColumns);
            if (input.navLeft)
                step(-1);
            if (input.navRight)
                step(1);
            if (input.accept() && n > 0)
                pickFromGrid(app);
        }

        void updateParty(App& app, const Input& input)
        {
            // Choosing who steps aside: only the places that hold somebody.
            int lowest = m_replacing >= 0 ? 1 : 0;
            int highest = m_replacing >= 0 ? int(m_chosen.size()) : m_slots - 1;
            if (input.navLeft)
                m_place = std::max(lowest, m_place - 1);
            if (input.navRight)
                m_place = std::min(highest, m_place + 1);
            if (input.navDown && m_replacing < 0) {
                m_focus = Focus_Grid;
                scrollToPick();
                return;
            }
            if (input.accept())
                pickFromParty(app);
        }

        void step(int by)
        {
            int n = int(m_shown.size());
            if (n <= 0)
                return;
            m_pick = std::min(n - 1, std::max(0, m_pick + by));
            scrollToPick();
        }

        void scrollToPick()
        {
            m_scroll.centerOn(float(m_pick / kColumns) * (kCellH + kGap) + kCellH * 0.5f);
        }

        void onTap(App& app, const TouchTarget& tap)
        {
            if (tap.is(Zone_Back)) {
                app.popOverlay();
                return;
            }
            if (tap.is(Zone_Filter) && tap.index >= 0 && tap.index < kFilters
                && m_replacing < 0) {
                setFilter(tap.index);
                return;
            }
            if (tap.is(Zone_Sort) && m_replacing < 0) {
                setSort((m_sort + 1) % kSorts);
                return;
            }
            if (tap.is(Zone_Member) && tap.index >= 0 && tap.index < m_slots) {
                m_focus = Focus_Party;
                m_place = tap.index;
                pickFromParty(app);
                return;
            }
            if (tap.is(Zone_Cell) && tap.index >= 0 && tap.index < int(m_shown.size())) {
                if (m_replacing >= 0)
                    return; // the question on screen is about the top row
                // A second tap on the one under the cursor is A, the same
                // as the shadows wall.
                bool again = m_focus == Focus_Grid && tap.index == m_pick;
                m_focus = Focus_Grid;
                m_pick = tap.index;
                if (again)
                    pickFromGrid(app);
            }
        }

        // ---------------------------------------------------------- drawing

        static constexpr float kHeaderY = 44.0f;
        static constexpr float kPartyY = 134.0f;
        static constexpr float kPartyH = 112.0f;
        static constexpr float kFilterY = 272.0f;
        static constexpr float kFilterH = 52.0f;
        static constexpr float kGridY = 350.0f;
        static constexpr int kColumns = 8;
        static constexpr float kCellH = 190.0f;
        static constexpr float kGap = 20.0f;
        // Clear of the hint strip, which owns the bottom 88 pixels.
        static constexpr float kFooter = 200.0f;

        void drawHints(App& app) const
        {
            if (m_replacing >= 0) {
                app.hint("A", "replace");
                app.hint("B", "cancel");
                return;
            }
            const Person* p = highlighted();
            if (m_focus == Focus_Grid && p)
                app.hint("A", chosen(p->id) ? "leave behind" : "bring along");
            else if (m_focus == Focus_Party && m_place > 0) {
                if (m_place - 1 < int(m_chosen.size()))
                    app.hint("A", "leave behind");
                else
                    app.hint("A", "bring along");
            }
            app.hint("L/R", "class");
            app.hint("X", "sort");
            if (p)
                app.hint("Y", "gear");
            app.hint("B", "back");
        }

        void drawHeader(App& app, Renderer& r) const
        {
            TextStyle label;
            label.size = theme::textSm;
            label.weight = FontWeight::Bold;
            label.color = theme::accent;
            label.tracking = theme::trackingWider;
            label.uppercase = true;
            r.text(theme::edge, kHeaderY, tr("the party"), label);

            TextStyle title;
            title.size = theme::textLg;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            std::string line = m_replacing >= 0
                ? std::string(tr("Who do they replace?"))
                : format(tr("%d of %d places"), int(m_chosen.size()) + 1, m_slots);
            r.text(theme::edge, kHeaderY + 32.0f, line, title);

            // The same loud note as on the climb screen, for the same reason:
            // an empty place is easy to miss and costs a fifth of the party.
            int freePlaces
                = std::min(m_slots - 1, int(m_people.size())) - int(m_chosen.size());
            if (m_replacing < 0 && freePlaces > 0) {
                TextStyle note;
                note.size = theme::textBase;
                note.weight = FontWeight::Bold;
                note.color = theme::accent;
                r.text(theme::edge + r.measure(line, title) + theme::s6,
                    kHeaderY + 38.0f, format(tr("free places: %d"), freePlaces), note);
            }

            Rect back { Renderer::DesignWidth - theme::edge - 42.0f, kHeaderY, 42.0f,
                42.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);
        }

        // The places along the top: you first and always, then whoever is
        // chosen, then a dashed card for every place still free.
        void drawParty(App& app, Renderer& r) const
        {
            constexpr int kMost = 5;
            float width = (Renderer::DesignWidth - theme::edge * 2.0f
                              - theme::s5 * float(kMost - 1))
                / float(kMost);
            for (int i = 0; i < m_slots; i++) {
                Rect card { theme::edge + float(i) * (width + theme::s5), kPartyY, width,
                    kPartyH };
                app.touchZone(card, Zone_Member, i);
                bool here = m_focus == Focus_Party && i == m_place;

                const Person* p = nullptr;
                if (i == 0)
                    p = &m_you;
                else if (i - 1 < int(m_chosen.size())) {
                    int at = indexOf(m_chosen[size_t(i - 1)]);
                    p = at >= 0 ? &m_people[size_t(at)] : nullptr;
                }

                if (!p) {
                    // Nothing in it: an outline and a word, so it reads as
                    // room rather than as a card that failed to load.
                    ui::card(r, card, here ? 0.7f + 0.3f * m_pulse : 0.0f, theme::bg0,
                        theme::r3);
                    r.strokeRect(card, theme::r3, theme::stroke * 2.0f, theme::stroke2);
                    TextStyle room;
                    room.size = theme::textSm;
                    room.color = theme::fg4;
                    room.tracking = theme::trackingWide;
                    room.uppercase = true;
                    r.text(card, tr("free"), room, Align::Center, VAlign::Middle);
                    continue;
                }

                // Choosing who steps aside lights every one who could.
                bool candidate = m_replacing >= 0 && i > 0;
                float glow = here ? 0.7f + 0.3f * m_pulse : (candidate ? 0.25f : 0.0f);
                ui::card(r, card, glow, i == 0 ? theme::bg1 : theme::bg2, theme::r3);
                Rect inner = card.inset(theme::s4, theme::s3);

                constexpr float kHead = 84.0f;
                ui::miiHead(r,
                    ui::headroom(Rect { inner.x, inner.centerY() - kHead * 0.5f, kHead,
                        kHead }),
                    p->face);

                float x = inner.x + kHead + theme::s4;
                float w = inner.right() - x;
                TextStyle name;
                name.size = theme::textBase;
                name.weight = FontWeight::Bold;
                name.color = theme::fg1;
                r.text(x, inner.y + 4.0f, r.ellipsize(p->name, name, w), name);

                TextStyle role;
                role.size = theme::textXs;
                role.color = theme::accent;
                role.tracking = theme::trackingWide;
                role.uppercase = true;
                r.text(x, inner.y + 38.0f, tr(className(p->sheet.cls)), role);

                TextStyle stat;
                stat.size = theme::textXs;
                stat.color = theme::fg3;
                r.text(x, inner.y + 64.0f,
                    r.ellipsize(format("HP %u  ATK %u  DEF %u", unsigned(p->sheet.hp),
                                    unsigned(p->sheet.atk), unsigned(p->sheet.def)),
                        stat, w),
                    stat);
            }
        }

        void drawFilters(App& app, Renderer& r) const
        {
            float x = theme::edge;
            for (int i = 0; i < kFilters; i++) {
                std::string label = i == 0 ? std::string(tr("everyone"))
                                           : std::string(tr(className(uint8_t(i - 1))));
                float width = ui::segmentWidth(r, label.c_str());
                Rect chip { x, kFilterY, width, kFilterH };
                app.touchZone(chip, Zone_Filter, i);
                bool here = i == m_filter;
                ui::pill(r, chip, label, here ? theme::bg0 : theme::fg3,
                    here ? theme::accent : theme::bg2);
                x += width + theme::s3;
            }

            // The order, on the right, as a pill of its own that a finger can
            // cycle as well as X.
            std::string sort = format(tr("sort: %s"), tr(sortName(m_sort)));
            float width = ui::segmentWidth(r, sort.c_str());
            Rect chip { Renderer::DesignWidth - theme::edge - width, kFilterY, width,
                kFilterH };
            app.touchZone(chip, Zone_Sort);
            ui::pill(r, chip, sort, theme::fg2, theme::bg2);
        }

        void drawGrid(App& app, Renderer& r)
        {
            Rect grid { theme::edge, kGridY, Renderer::DesignWidth - theme::edge * 2.0f,
                Renderer::DesignHeight - kGridY - kFooter };
            m_grid = grid;

            if (m_shown.empty()) {
                TextStyle empty;
                empty.size = theme::textBase;
                empty.color = theme::fg4;
                r.text(Rect { grid.x, grid.y + 40.0f, grid.w, 40.0f },
                    m_people.empty()
                        ? std::string(tr("Nobody has crossed you yet - you climb alone."))
                        : std::string(tr("Nobody of that class yet.")),
                    empty);
                return;
            }

            float cellW = (grid.w - kGap * float(kColumns - 1)) / float(kColumns);
            int rows = (int(m_shown.size()) + kColumns - 1) / kColumns;
            m_scroll.setBounds(grid.h, float(rows) * (kCellH + kGap));

            r.pushClipVertical(grid.inset(0.0f, -theme::focusRoom));
            // The highlighted one last, so its ring is drawn over its
            // neighbours rather than under the next card painted.
            for (int pass = 0; pass < 2; pass++) {
                for (int i = 0; i < int(m_shown.size()); i++) {
                    bool here = m_focus == Focus_Grid && i == m_pick;
                    if (here != (pass == 1))
                        continue;
                    float x = grid.x + float(i % kColumns) * (cellW + kGap);
                    float y = grid.y + float(i / kColumns) * (kCellH + kGap)
                        - m_scroll.offset();
                    Rect cell { x, y, cellW, kCellH };
                    if (cell.bottom() < grid.y - theme::focusRoom
                        || cell.y > grid.bottom() + theme::focusRoom)
                        continue;
                    drawCell(app, r, cell, i, here);
                }
            }
            r.popClip();

            if (m_scroll.scrollable()) {
                ui::scrollbar(r, Rect { grid.right() + 16.0f, grid.y, 8.0f, grid.h },
                    m_scroll.progress(), m_scroll.visibleFraction());
            }
        }

        void drawCell(App& app, Renderer& r, const Rect& cell, int index, bool here) const
        {
            const Person& p = m_people[size_t(m_shown[size_t(index)])];
            bool in = chosen(p.id);
            // The one being brought in stays lit while the top row asks who
            // makes room for them.
            bool incoming = m_replacing == m_shown[size_t(index)];

            app.touchZone(cell, Zone_Cell, index);
            ui::card(r, cell, here || incoming ? 0.7f + 0.3f * m_pulse : 0.0f,
                in ? theme::bg2 : theme::bg1, theme::r3);

            constexpr float kHead = 96.0f;
            ui::miiHead(r,
                ui::headroom(Rect { cell.centerX() - kHead * 0.5f, cell.y + 10.0f, kHead,
                    kHead }),
                p.face);

            // In the party: a filled dot in the corner, the way the old strip
            // said it, and the fill of the card.
            if (in) {
                r.ellipse(cell.right() - 20.0f, cell.y + 20.0f, 9.0f, 9.0f, theme::accent,
                    0.0f);
            }

            float w = cell.w - theme::s4;
            TextStyle name;
            name.size = theme::textSm;
            name.weight = FontWeight::Bold;
            name.color = in ? theme::fg1 : theme::fg2;
            r.text(Rect { cell.x + theme::s2, cell.y + 118.0f, w, 28.0f },
                r.ellipsize(p.name, name, w), name, Align::Center, VAlign::Top);

            TextStyle role;
            role.size = theme::textXs;
            role.color = theme::accent;
            role.tracking = theme::trackingWide;
            role.uppercase = true;
            r.text(Rect { cell.x + theme::s3, cell.y + 150.0f, w, 24.0f },
                tr(className(p.sheet.cls)), role, Align::Left, VAlign::Top);

            TextStyle worth;
            worth.size = theme::textXs;
            worth.weight = FontWeight::Bold;
            worth.color = theme::fg3;
            r.text(Rect { cell.x, cell.y + 150.0f, cell.w - theme::s3, 24.0f },
                format("%d", p.worth), worth, Align::Right, VAlign::Top);
        }

        // Everything about the highlighted person along the foot, so the grid
        // can stay faces and names and still be decided from.
        void drawFooter(Renderer& r) const
        {
            float top = Renderer::DesignHeight - kFooter;
            r.rect(Rect { 0.0f, top, Renderer::DesignWidth, kFooter }, theme::bg0);
            r.rect(Rect { 0.0f, top, Renderer::DesignWidth, theme::stroke },
                theme::stroke1);

            const Person* p = highlighted();
            if (!p)
                return;
            Rect foot { theme::edge, top + theme::s4,
                Renderer::DesignWidth - theme::edge * 2.0f, 40.0f };

            TextStyle who;
            who.size = theme::textBase;
            who.weight = FontWeight::Bold;
            who.color = theme::fg1;
            std::string line = p->name + "   " + tr(className(p->sheet.cls));
            if (!p->id.empty())
                line += "   " + format(tr("crossings: %u"), unsigned(p->crossed));
            r.text(foot, line, who, Align::Left, VAlign::Middle);

            TextStyle stats;
            stats.size = theme::textSm;
            stats.color = theme::fg2;
            stats.tracking = theme::trackingWide;
            r.text(foot,
                format("HP %u   MP %u   ATK %u   DEF %u   SPD %u", unsigned(p->sheet.hp),
                    unsigned(p->sheet.mp), unsigned(p->sheet.atk), unsigned(p->sheet.def),
                    unsigned(p->sheet.spd)),
                stats, Align::Right, VAlign::Middle);

            foot.y += 44.0f;
            TextStyle note;
            note.size = theme::textSm;
            note.color = theme::fg3;
            r.text(foot, format(tr("gear: %d of 4"), p->worn), note, Align::Left,
                VAlign::Middle);
            bool in = p->id.empty() || chosen(p->id);
            if (in) {
                note.color = theme::accent;
                note.weight = FontWeight::Bold;
                r.text(foot, tr("in the party"), note, Align::Right, VAlign::Middle);
            }
        }

        // ------------------------------------------------------------ state

        enum Focus : int {
            Focus_Grid = 0,
            Focus_Party,
        };

        Person m_you;
        std::vector<Person> m_people;     // the whole collection
        std::vector<int> m_shown;         // indices into m_people, filtered and sorted
        std::vector<std::string> m_chosen; // ids, in the order they were picked
        int m_slots = 3;

        int m_focus = Focus_Grid;
        int m_pick = 0;       // in m_shown
        int m_place = 1;      // along the top; 0 is you
        int m_replacing = -1; // index into m_people waiting for a place, or -1
        int m_filter = 0;     // 0 everyone, then 1 + a class
        int m_sort = 0;
        bool m_redress = false;

        float m_pulse = 0.0f;
        ui::ScrollView m_scroll;
        Rect m_grid {};
        bool m_dragging = false;
        bool m_braked = false;
    };
}

std::unique_ptr<Scene> makeQuestPartyScene()
{
    return std::make_unique<PartyScene>();
}

} // namespace nxp
