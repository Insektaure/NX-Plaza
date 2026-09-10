#include "app.h"
#include "core/i18n.h"
#include "core/quest_record.h"
#include "core/quest_rules.h"
#include "core/store.h"
#include "core/util.h"
#include "core/wallet.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/plaza_scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace nxp {

namespace {

    uint32_t randomBelow(uint32_t n)
    {
        uint32_t bits = 0;
        randomBytes(&bits, sizeof(bits));
        return n == 0 ? 0 : bits % n;
    }

    // The rules - what a fighter is made of, what a floor asks, and what
    // falls out of one - are in core/quest_rules.h, because the screen where
    // gear is moved around needs the same numbers as the climb does.

    int partySlots(uint32_t peopleMet)
    {
        if (peopleMet >= 25)
            return 5;
        if (peopleMet >= 10)
            return 4;
        return 3;
    }

    class QuestScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Roster = Touch_SceneBase,
            Zone_Climb,
            Zone_Back,
        };

        bool coversChrome() const override { return true; }

        // A climb runs itself, so + waits until it is over one way or another.
        bool blocksExit() const override
        {
            return m_phase == Phase_Fight || m_phase == Phase_Cleared;
        }

        void onEnter(App& app) override
        {
            buildRoster(app);
            m_phase = Phase_Party;
            m_clock = 0.0f;
            m_best = QuestRecord::get().deepest();
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_clock += dt;
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

            TouchTarget tap;
            bool tapped = app.takeTap(tap);

            switch (m_phase) {
            case Phase_Fight:
                runFight(app, dt);
                if (input.back())
                    stop();
                return;
            case Phase_Cleared:
                if (m_clock >= kFloorBeat)
                    nextFloor();
                if (input.back())
                    stop();
                return;
            default:
                break;
            }

            if (tapped) {
                if (tap.is(Zone_Back))
                    app.popOverlay();
                else if (tap.is(Zone_Climb))
                    startOrAgain();
                else if (tap.is(Zone_Roster) && tap.index >= 0
                    && tap.index < int(m_roster.size())) {
                    m_cursor = tap.index;
                    toggle();
                }
                return;
            }

            if (input.back()) {
                app.popOverlay();
                return;
            }

            if (m_phase == Phase_Over) {
                if (input.accept())
                    startOrAgain();
                return;
            }

            // The gear screen is drawn over this one and can change what
            // the party is worth, and popping an overlay does not re-enter
            // the scene underneath, so the shown sheets are re-read every
            // frame. It is five people and four lookups each.
            dress(m_you);
            for (int index : m_chosen) {
                if (index >= 0 && index < int(m_roster.size()))
                    dress(m_roster[size_t(index)]);
            }

            if (input.navLeft)
                moveCursor(-1);
            if (input.navRight)
                moveCursor(1);
            if (input.accept())
                toggle();
            if (input.pressed(HidNpadButton_X))
                startOrAgain();
            if (input.pressed(HidNpadButton_Y))
                app.pushOverlay(makeQuestGearScene(party()));
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);
            ui::plazaBackdrop(r, 0.0f, kHorizon, 0.0f);
            ui::plazaGround(r, 0.0f, kHorizon);

            drawHeader(r);

            if (m_phase == Phase_Party) {
                drawShadow(r, m_floor, false, 1.0f);
                drawParty(r, true);
                drawRoster(app, r);
                drawPartyHints(app, r);
                return;
            }

            drawShadow(r, m_floor, m_bossHp <= 0, m_bossShake);
            drawBossBar(r);
            drawLog(r);
            drawParty(r, false);

            if (m_phase == Phase_Over)
                drawOver(app, r);
            else
                app.hint("B", m_phase == Phase_Cleared ? "stop here" : "stop");
        }

    private:
        enum Phase : int {
            Phase_Party = 0, // choosing who goes
            Phase_Fight,     // a floor resolving itself
            Phase_Cleared,   // the beat between floors
            Phase_Over,      // wiped or stopped; there is no top
        };

        struct Member {
            std::string id; // empty for your own Mii
            std::string name;
            Mii face;
            Sheet base;  // what they are worth with nothing on
            Sheet sheet; // and with their gear folded in
            int hp = 1;          // during a climb
            int mp = 0;
            float lunge = 0.0f;  // a step forward when they act
        };

        // Where you are inside a climb is not written down anywhere: a run
        // always starts at the bottom with everybody standing, because the
        // attrition between floors is the game and resuming at floor ten
        // with full health would be a different, easier one. What does
        // persist is the deepest floor ever reached, in QuestRecord.

        // One coin a floor, and a floor pays once a week rather than once
        // ever: a Monday puts the whole tower back on the board, so a
        // console that has finished climbing still has a reason to go up on
        // reset day.
        //
        // It still cannot be farmed inside a week: being paid means going
        // deeper than you have been since Monday, so re-clearing floor three
        // pays nothing at all.
        static constexpr uint32_t kFloorCoins = 1;

        static constexpr float kHorizon = 380.0f;
        static constexpr float kGround = 430.0f;
        static constexpr float kBeat = 0.55f;      // one action
        static constexpr float kFloorBeat = 1.30f; // the pause between floors
        static constexpr int kSkillCost = 6;

        static constexpr float kCardH = 250.0f;
        static constexpr float kCardY = 590.0f;
        static constexpr float kRosterY = 862.0f;
        static constexpr float kRosterH = 110.0f;
        static constexpr float kRosterCell = 104.0f;

        // ------------------------------------------------------- the roster

        void buildRoster(App& app)
        {
            Store& store = app.store();
            const Stats record = store.stats();
            const Pass mine = store.myPass();

            m_roster.clear();

            m_you = Member {};
            m_you.name = mine.handle.empty() ? std::string(tr("You")) : mine.handle;
            m_you.face = mine.face();
            m_you.base = sheetFor(m_you.face, record.uniquePeople,
                record.totalCrossings, mine.hours, mine.titles);

            std::vector<std::string> live;
            for (const Crossing& c : store.crossings()) {
                Member m;
                m.id = c.id;
                m.name = c.pass.handle.empty() ? std::string(tr("A stranger"))
                                               : c.pass.handle;
                m.face = c.pass.face();
                m.base = sheetFor(m.face, c.count, c.pass.met, c.pass.hours,
                    c.pass.titles);
                live.push_back(c.id);
                m_roster.push_back(std::move(m));
            }

            // Gear held by somebody who has dropped off the end of the
            // collection comes back to the bag rather than staying on a name
            // nothing can show.
            QuestRecord::get().release(live);

            dress(m_you);
            for (Member& m : m_roster)
                dress(m);

            // Strongest first, so the default party is a sensible one and the
            // list reads as a ranking rather than as a pile.
            std::stable_sort(m_roster.begin(), m_roster.end(),
                [](const Member& a, const Member& b) { return worth(a) > worth(b); });

            m_slots = partySlots(record.uniquePeople);
            m_cursor = 0;
            m_chosen.clear();
            for (size_t i = 0; i < m_roster.size() && int(m_chosen.size()) < m_slots - 1;
                 i++)
                m_chosen.push_back(int(i));
        }

        // Base plus whatever they are wearing. Read here rather than during
        // a fight so a climb runs on the numbers the party screen showed.
        static void dress(Member& m)
        {
            const QuestRecord& record = QuestRecord::get();
            QuestRecord::Loadout gear = record.loadout(m.id);
            m.sheet = m.base;
            for (int slot = 0; slot < Slot_Count; slot++) {
                if (const Item* item = record.find(gear.worn[slot]))
                    m.sheet += itemBonus(*item);
            }
        }

        // One number for sorting a roster by, which is not a stat and does
        // not pretend to be: it is only there so the strongest people are the
        // ones already in the party when the screen opens.
        static int worth(const Member& m)
        {
            return int(m.sheet.hp) / 4 + int(m.sheet.atk) * 2 + int(m.sheet.def)
                + int(m.sheet.spd) + int(m.sheet.mp);
        }

        bool inParty(int index) const
        {
            return std::find(m_chosen.begin(), m_chosen.end(), index) != m_chosen.end();
        }

        // Who is going up, in the order the cards show them: your own Mii
        // and then whoever was picked.
        std::vector<GearPerson> party() const
        {
            std::vector<GearPerson> out;
            out.push_back(GearPerson { m_you.id, m_you.name, m_you.face, m_you.base });
            for (int index : m_chosen) {
                if (index < 0 || index >= int(m_roster.size()))
                    continue;
                const Member& m = m_roster[size_t(index)];
                out.push_back(GearPerson { m.id, m.name, m.face, m.base });
            }
            return out;
        }

        void moveCursor(int by)
        {
            if (m_roster.empty())
                return;
            int count = int(m_roster.size());
            m_cursor = (m_cursor + by % count + count) % count;
        }

        void toggle()
        {
            if (m_phase != Phase_Party || m_roster.empty())
                return;
            auto it = std::find(m_chosen.begin(), m_chosen.end(), m_cursor);
            if (it != m_chosen.end()) {
                m_chosen.erase(it);
                return;
            }
            // The party opens full: buildRoster fills every place with the
            // strongest people in the collection, so "there is no room" is
            // the normal case and not the edge one. Refusing here made A do
            // nothing at all for anybody who had not first taken somebody
            // out - which is to say, for everybody. The longest-standing
            // pick steps aside instead, and your own Mii keeps the first
            // place whatever happens.
            if (int(m_chosen.size()) >= m_slots - 1 && !m_chosen.empty())
                m_chosen.erase(m_chosen.begin());
            m_chosen.push_back(m_cursor);
        }

        // -------------------------------------------------------- the climb

        void startOrAgain()
        {
            m_floor = 1;
            m_deepest = 0;
            m_earned = 0;
            m_found = 0;
            m_bestFound = 0;
            m_stopped = false;
            QuestRecord::get().noteClimb();

            m_units.clear();
            m_units.push_back(m_you);
            for (int index : m_chosen) {
                if (index >= 0 && index < int(m_roster.size()))
                    m_units.push_back(m_roster[size_t(index)]);
            }
            for (Member& u : m_units) {
                u.hp = int(u.sheet.hp);
                u.mp = int(u.sheet.mp);
            }
            beginFloor();
        }

        void beginFloor()
        {
            m_boss = bossFor(m_floor);
            m_bossHp = int(m_boss.hp);
            m_bossAtk = float(m_boss.atk);
            m_round = 0;
            m_order.clear();
            m_turn = 0;
            m_beatClock = 0.0f;
            m_bossShake = 1.0f;
            m_guard = -1;
            m_log.clear();
            m_phase = Phase_Fight;
            m_clock = 0.0f;
        }

        void nextFloor()
        {
            // A breather, not a heal: a third back and a little of the wind,
            // so a climb is a war of attrition and the party that reaches
            // floor nine is the one that got there cheaply.
            for (Member& u : m_units) {
                if (u.hp <= 0)
                    continue;
                u.hp = std::min(int(u.sheet.hp), u.hp + int(u.sheet.hp) / 3);
                u.mp = std::min(int(u.sheet.mp), u.mp + 4);
            }
            m_floor++;
            beginFloor();
        }

        void stop()
        {
            m_stopped = true;
            m_phase = Phase_Over;
            m_clock = 0.0f;
        }

        void runFight(App& app, float dt)
        {
            m_bossShake = std::min(1.0f, m_bossShake + dt * 4.0f);
            for (Member& u : m_units)
                u.lunge = std::max(0.0f, u.lunge - dt * 4.0f);

            m_beatClock += dt;
            if (m_beatClock < kBeat)
                return;
            m_beatClock = 0.0f;

            if (m_turn >= int(m_order.size()))
                beginRound();
            if (m_order.empty())
                return;

            int actor = m_order[size_t(m_turn++)];
            if (actor < 0)
                bossActs();
            else
                allyActs(actor);

            if (m_bossHp <= 0) {
                clearedFloor(app);
                return;
            }
            if (!anyoneStanding()) {
                m_phase = Phase_Over;
                m_clock = 0.0f;
                return;
            }
            // Thirty rounds is not a fight any more, it is two walls. The
            // shadow keeps the floor.
            if (m_round > 30) {
                m_phase = Phase_Over;
                m_clock = 0.0f;
            }
        }

        void beginRound()
        {
            m_round++;
            m_order.clear();
            for (size_t i = 0; i < m_units.size(); i++) {
                if (m_units[i].hp > 0)
                    m_order.push_back(int(i));
            }
            m_order.push_back(-1); // the shadow
            std::stable_sort(m_order.begin(), m_order.end(), [this](int a, int b) {
                int sa = a < 0 ? int(m_boss.spd) : int(m_units[size_t(a)].sheet.spd);
                int sb = b < 0 ? int(m_boss.spd) : int(m_units[size_t(b)].sheet.spd);
                return sa > sb;
            });
            m_turn = 0;
        }

        bool anyoneStanding() const
        {
            for (const Member& u : m_units) {
                if (u.hp > 0)
                    return true;
            }
            return false;
        }

        // A hit, with a fifth either way and the odd one landing properly.
        // The spread is what makes two runs of the same party different, and
        // it is small enough that it decides a close floor rather than a
        // whole climb.
        static int hitFor(int atk, int def, float mult)
        {
            int base = int(float(atk) - float(def) * 0.5f);
            base = std::max(1, int(float(base) * mult));
            int wobble = std::max(1, base / 4);
            int out = base + int(randomBelow(uint32_t(wobble * 2 + 1))) - wobble;
            if (randomBelow(100) < 12)
                out = int(float(out) * 1.7f);
            return std::max(1, out);
        }

        void allyActs(int index)
        {
            Member& u = m_units[size_t(index)];
            if (u.hp <= 0)
                return;
            u.lunge = 1.0f;

            bool hasMp = u.mp >= kSkillCost;
            switch (u.sheet.cls) {
            case Class_Blade:
                if (hasMp) {
                    u.mp -= kSkillCost;
                    int dealt = hitFor(u.sheet.atk, m_boss.def, 1.8f);
                    m_bossHp -= dealt;
                    m_bossShake = 0.0f;
                    m_log = format(tr("%s strikes for %d"), u.name.c_str(), dealt);
                    return;
                }
                break;
            case Class_Mender:
                if (hasMp) {
                    Member* hurt = weakest();
                    if (hurt && hurt->hp < int(hurt->sheet.hp) * 3 / 5) {
                        u.mp -= kSkillCost;
                        int given = int(u.sheet.atk) * 3;
                        hurt->hp = std::min(int(hurt->sheet.hp), hurt->hp + given);
                        m_log = format(tr("%s patches up %s"), u.name.c_str(),
                            hurt->name.c_str());
                        return;
                    }
                }
                break;
            case Class_Spark:
                if (hasMp) {
                    u.mp -= kSkillCost;
                    int dealt = hitFor(u.sheet.atk, m_boss.def, 1.3f);
                    m_bossHp -= dealt;
                    m_bossShake = 0.0f;
                    // Worn down rather than out-hit: the shadow's arm is
                    // what a Spark takes away, and it does not come back.
                    m_bossAtk = std::max(float(m_boss.atk) * 0.6f, m_bossAtk * 0.92f);
                    m_log = format(tr("%s wears it down, %d"), u.name.c_str(), dealt);
                    return;
                }
                break;
            case Class_Guard:
                if (hasMp && m_guard != index) {
                    u.mp -= kSkillCost;
                    m_guard = index;
                    m_log = format(tr("%s stands in front"), u.name.c_str());
                    return;
                }
                break;
            default:
                break;
            }

            int dealt = hitFor(u.sheet.atk, m_boss.def, 1.0f);
            m_bossHp -= dealt;
            m_bossShake = 0.0f;
            m_log = format(tr("%s hits for %d"), u.name.c_str(), dealt);
        }

        Member* weakest()
        {
            Member* out = nullptr;
            float worst = 2.0f;
            for (Member& u : m_units) {
                if (u.hp <= 0)
                    continue;
                float share = float(u.hp) / float(std::max<uint16_t>(1, u.sheet.hp));
                if (share < worst) {
                    worst = share;
                    out = &u;
                }
            }
            return out;
        }

        void bossActs()
        {
            int atk = int(m_bossAtk);

            // Every fourth round it swings at the lot for a little over half.
            if (m_round % 4 == 0) {
                int total = 0;
                for (Member& u : m_units) {
                    if (u.hp <= 0)
                        continue;
                    int dealt = hitFor(atk, u.sheet.def, 0.55f);
                    u.hp = std::max(0, u.hp - dealt);
                    total += dealt;
                }
                m_log = format(tr("The shadow sweeps, %d"), total);
                return;
            }

            Member* target = nullptr;
            if (m_guard >= 0 && m_guard < int(m_units.size())
                && m_units[size_t(m_guard)].hp > 0) {
                target = &m_units[size_t(m_guard)];
            } else {
                std::vector<int> alive;
                for (size_t i = 0; i < m_units.size(); i++) {
                    if (m_units[i].hp > 0)
                        alive.push_back(int(i));
                }
                if (alive.empty())
                    return;
                target = &m_units[size_t(alive[randomBelow(uint32_t(alive.size()))])];
            }

            // Standing in front is worth half the blow, which is the whole
            // job: a Guard turns the shadow's best round into its dullest.
            bool guarded = m_guard >= 0 && target == &m_units[size_t(m_guard)];
            int dealt = hitFor(atk, target->sheet.def, guarded ? 0.5f : 1.0f);
            target->hp = std::max(0, target->hp - dealt);
            if (target->hp == 0) {
                if (guarded)
                    m_guard = -1;
                m_log = format(tr("%s falls"), target->name.c_str());
                return;
            }
            m_log = format(tr("The shadow hits %s for %d"), target->name.c_str(), dealt);
        }

        void clearedFloor(App& app)
        {
            m_deepest = m_floor;
            m_guard = -1;

            // The all-time record, which is what the shelf shows. It stopped
            // gating the coins when the week came in.
            QuestRecord& record = QuestRecord::get();
            if (record.noteFloor(uint32_t(m_floor)))
                m_best = uint32_t(m_floor);

            // First time up here this week? That is the coin, and only the
            // coin: the loot is a flat half on every floor and every climb.
            if (record.notePaidFloor(uint32_t(m_floor))) {
                // The record goes to the card first. Something has to, and
                // of the two ways for a console to die between the writes,
                // losing a coin is a better loss than being able to earn it
                // a second time.
                record.flush();

                Wallet& wallet = Wallet::get();
                wallet.award(kFloorCoins);
                wallet.flush();
                m_earned += kFloorCoins;
            }

            m_log = format(tr("Floor %d is yours"), m_floor);
            takeDrop(app, record);

            m_clock = 0.0f;
            m_phase = Phase_Cleared;
        }

        void takeDrop(App& app, QuestRecord& record)
        {
            Item fell = rollDrop(m_floor, record.nextId());
            if (!fell.valid())
                return;

            if (record.bagFull()) {
                // A full bag used to lose the drop, which meant a hundred
                // and twenty commons could cost somebody a godlike from
                // floor thirty. It makes room instead, and only ever out of
                // the worst thing nobody is wearing.
                const Item* spare = record.find(record.worstSpare());
                if (!spare || itemRating(*spare) >= itemRating(fell)) {
                    app.toast(tr("Your bag is full"),
                        tr("Nothing in it was worse than what fell, so what fell "
                           "stayed on the floor."));
                    return;
                }
                std::string went = tr(itemNoun(*spare));
                record.discard(spare->id);
                app.toast(tr("Your bag is full"),
                    format(tr("%s was thrown out to make room."), went.c_str()));
            }

            record.add(fell);
            record.flush();
            m_found++;
            if (fell.quality > m_bestFound)
                m_bestFound = fell.quality;

            // The noun first and the tier in brackets, in that order in
            // every language: the two are both %s, so a translation that
            // swapped them would read wrong with nothing able to catch it.
            m_log = format(tr("The shadow left a %s (%s)"), tr(itemNoun(fell)),
                tr(qualityName(fell.quality)));
        }

        // ------------------------------------------------------ the painting

        void drawHeader(Renderer& r) const
        {
            TextStyle label;
            label.size = theme::textSm;
            label.weight = FontWeight::Bold;
            label.color = theme::accent;
            label.tracking = theme::trackingWider;
            label.uppercase = true;
            r.text(theme::edge, 44.0f, tr("the quest"), label);

            TextStyle floorText;
            floorText.size = theme::textXl;
            floorText.weight = FontWeight::Bold;
            floorText.color = theme::fg1;
            floorText.tracking = theme::trackingTight;
            r.text(theme::edge, 74.0f, format(tr("Floor %d"), m_floor), floorText);

            TextStyle note;
            note.size = theme::textSm;
            note.color = theme::fg3;
            note.tracking = theme::trackingWide;
            Rect right { 0.0f, 80.0f, Renderer::DesignWidth - theme::edge, 30.0f };
            if (m_best > 0) {
                r.text(right, format(tr("best floor %u"), unsigned(m_best)), note,
                    Align::Right, VAlign::Top);
            }

            // What the week has already paid for, because a reward you
            // cannot see the state of is a reward people assume is broken.
            const QuestRecord& record = QuestRecord::get();
            note.color = theme::fg4;
            right.y = 112.0f;
            uint32_t paid = record.paidThisWeek();
            std::string week = record.week() == 0
                ? std::string(tr("the week turns when the plaza is next reached"))
                : (paid == 0 ? std::string(tr("every floor pays this week"))
                             : format(tr("floors up to %u have paid this week"),
                                 unsigned(paid)));
            r.text(right, week, note, Align::Right, VAlign::Top);
        }

        void drawShadow(Renderer& r, int floor, bool beaten, float shake) const
        {
            constexpr float kFigure = 260.0f;
            float jolt = (1.0f - shake) * 14.0f;
            Rect box { Renderer::DesignWidth * 0.5f - kFigure * 0.42f + jolt,
                kGround - kFigure, kFigure * 0.84f, kFigure };

            Color ink = theme::bg0.mix(theme::danger, 0.22f);
            float opacity = beaten ? 0.28f : 1.0f;
            if (!beaten) {
                r.glow(Rect { box.centerX() - 150.0f, box.centerY() - 150.0f, 300.0f,
                           300.0f },
                    theme::danger.scaleAlpha(0.16f), 1.8f);
            }
            r.ellipse(box.centerX(), kGround + 8.0f, kFigure * 0.34f, 14.0f,
                theme::bg0.scaleAlpha(0.35f * opacity), 0.0f);
            ui::miiFigure(r, box, shadowFace(floor), opacity, false, &ink);
        }

        void drawBossBar(Renderer& r) const
        {
            constexpr float kBarW = 700.0f;
            Rect bar { Renderer::DesignWidth * 0.5f - kBarW * 0.5f, 468.0f, kBarW,
                22.0f };
            float share = m_boss.hp > 0
                ? std::max(0.0f, float(m_bossHp) / float(m_boss.hp))
                : 0.0f;
            r.roundRect(bar, 11.0f, theme::bg2);
            if (share > 0.0f) {
                r.roundRect(Rect { bar.x, bar.y, bar.w * share, bar.h }, 11.0f,
                    theme::danger);
            }

            TextStyle stat;
            stat.size = theme::textSm;
            stat.color = theme::fg3;
            stat.tracking = theme::trackingWide;
            r.text(Rect { bar.x, bar.bottom() + theme::s2, bar.w, 28.0f },
                format("ATK %u   DEF %u   SPD %u", unsigned(int(m_bossAtk)),
                    unsigned(m_boss.def), unsigned(m_boss.spd)),
                stat, Align::Center, VAlign::Top);
        }

        void drawLog(Renderer& r) const
        {
            if (m_log.empty())
                return;
            TextStyle line;
            line.size = theme::textBase;
            line.weight = FontWeight::Medium;
            line.color = theme::fg2;
            r.text(Rect { theme::edge, 534.0f, Renderer::DesignWidth - theme::edge * 2.0f,
                      36.0f },
                r.ellipsize(m_log, line, Renderer::DesignWidth - theme::edge * 2.0f),
                line, Align::Center, VAlign::Top);
        }

        // The party, as cards along the foot of the screen. In the party
        // phase these are the chosen ones with their stats; during a climb
        // they are the same cards with bars in them, so nothing jumps about
        // between picking somebody and watching them fight.
        void drawParty(Renderer& r, bool picking) const
        {
            std::vector<const Member*> show;
            if (picking) {
                show.push_back(&m_you);
                for (int index : m_chosen) {
                    if (index >= 0 && index < int(m_roster.size()))
                        show.push_back(&m_roster[size_t(index)]);
                }
            } else {
                for (const Member& u : m_units)
                    show.push_back(&u);
            }
            if (show.empty())
                return;

            float gap = theme::s4;
            float total = Renderer::DesignWidth - theme::edge * 2.0f;
            float width = (total - gap * float(m_slots - 1)) / float(m_slots);
            float x = theme::edge;
            for (int slot = 0; slot < m_slots; slot++) {
                Rect box { x, kCardY, width, kCardH };
                if (slot < int(show.size()))
                    drawMemberCard(r, box, *show[size_t(slot)], picking);
                else
                    drawEmptySlot(r, box);
                x += width + gap;
            }
        }

        void drawMemberCard(Renderer& r, const Rect& box, const Member& m,
            bool picking) const
        {
            bool down = !picking && m.hp <= 0;
            ui::card(r, box, 0.0f, down ? theme::bg1.scaleAlpha(0.6f) : theme::bg1,
                theme::r3);
            Rect inner = box.inset(theme::s4, theme::s4);

            float lunge = picking ? 0.0f : m.lunge * 10.0f;
            Rect head { inner.x - lunge, inner.y, 92.0f, 92.0f };
            ui::miiHead(r, head, m.face, down ? 0.35f : 1.0f);

            TextStyle name;
            name.size = theme::textSm;
            name.weight = FontWeight::Bold;
            name.color = down ? theme::fg4 : theme::fg1;
            float textX = head.right() + theme::s3;
            float textW = inner.right() - textX;
            r.text(textX, inner.y + 4.0f, r.ellipsize(m.name, name, textW), name);

            TextStyle role;
            role.size = theme::textXs;
            role.color = theme::accent;
            role.tracking = theme::trackingWide;
            role.uppercase = true;
            r.text(textX, inner.y + 34.0f, tr(className(m.sheet.cls)), role);

            // Bars: hit points always, and the wind for a special under it.
            float barY = inner.y + 100.0f;
            drawBar(r, Rect { inner.x, barY, inner.w, 16.0f },
                picking ? 1.0f : float(std::max(0, m.hp)) / float(m.sheet.hp),
                down ? theme::bg3 : theme::success);
            drawBar(r, Rect { inner.x, barY + 22.0f, inner.w, 10.0f },
                m.sheet.mp == 0 ? 0.0f
                                : (picking ? 1.0f : float(m.mp) / float(m.sheet.mp)),
                theme::info);

            TextStyle stat;
            stat.size = theme::textXs;
            stat.color = theme::fg3;
            stat.tracking = theme::trackingWide;
            std::string top = picking
                ? format("HP %u   MP %u", unsigned(m.sheet.hp), unsigned(m.sheet.mp))
                : format("HP %d/%u", std::max(0, m.hp), unsigned(m.sheet.hp));
            r.text(inner.x, barY + 40.0f, top, stat);
            r.text(inner.x, barY + 66.0f,
                format("ATK %u  DEF %u  SPD %u", unsigned(m.sheet.atk),
                    unsigned(m.sheet.def), unsigned(m.sheet.spd)),
                stat);
        }

        void drawEmptySlot(Renderer& r, const Rect& box) const
        {
            r.roundRect(box, theme::r3, theme::bg1.scaleAlpha(0.45f));
            r.strokeRect(box, theme::r3, theme::stroke, theme::stroke1);
            TextStyle hint;
            hint.size = theme::textSm;
            hint.color = theme::fg4;
            r.text(box.inset(theme::s5), tr("an empty place"), hint, Align::Center,
                VAlign::Middle);
        }

        static void drawBar(Renderer& r, const Rect& box, float share, Color fill)
        {
            r.roundRect(box, box.h * 0.5f, theme::bg3);
            float clamped = std::min(1.0f, std::max(0.0f, share));
            if (clamped > 0.0f) {
                r.roundRect(Rect { box.x, box.y, box.w * clamped, box.h }, box.h * 0.5f,
                    fill);
            }
        }

        // Everyone you have crossed, as a strip of heads. The party is picked
        // out of this rather than out of a menu, because the faces are the
        // part somebody recognises.
        void drawRoster(App& app, Renderer& r)
        {
            Rect strip { theme::edge, kRosterY,
                Renderer::DesignWidth - theme::edge * 2.0f, kRosterH };
            if (m_roster.empty()) {
                TextStyle empty;
                empty.size = theme::textSm;
                empty.color = theme::fg3;
                r.text(strip, tr("Nobody has crossed you yet - you climb alone."), empty,
                    Align::Center, VAlign::Middle);
                return;
            }

            int fits = std::max(1, int(strip.w / kRosterCell));
            int first = std::max(0, std::min(m_cursor - fits / 2,
                                     int(m_roster.size()) - fits));
            r.pushClipVertical(strip);
            float x = strip.x;
            for (int i = first; i < int(m_roster.size()) && x < strip.right(); i++) {
                Rect cell { x, strip.y, kRosterCell - theme::s2, strip.h };
                bool chosen = inParty(i);
                bool focused = i == m_cursor;
                app.touchZone(cell, Zone_Roster, i);
                ui::card(r, cell, focused ? 0.7f + 0.3f * m_pulse : 0.0f,
                    chosen ? theme::bg2 : theme::bg1, theme::r2);
                ui::miiHead(r, Rect { cell.x + 14.0f, cell.y + 8.0f, cell.w - 28.0f,
                                cell.w - 28.0f },
                    m_roster[size_t(i)].face, chosen ? 1.0f : 0.55f);
                if (chosen) {
                    r.circle(cell.right() - 16.0f, cell.y + 16.0f, 7.0f, theme::accent);
                }
                x += kRosterCell;
            }
            r.popClip();

            TextStyle who;
            who.size = theme::textSm;
            who.color = theme::fg2;
            r.text(Rect { strip.x, strip.bottom() - 4.0f, strip.w, 26.0f },
                m_roster[size_t(m_cursor)].name, who, Align::Center, VAlign::Top);
        }

        void drawPartyHints(App& app, Renderer& r)
        {
            app.hint("A", inParty(m_cursor) ? "leave behind" : "bring along");
            app.hint("X", "climb");
            app.hint("Y", "gear");
            app.hint("B", "back");

            std::string label = format(tr("%d of %d places"), int(m_chosen.size()) + 1,
                m_slots);
            TextStyle note;
            note.size = theme::textSm;
            note.color = theme::fg3;
            r.text(Rect { theme::edge, 552.0f,
                      Renderer::DesignWidth - theme::edge * 2.0f, 28.0f },
                label, note, Align::Center, VAlign::Top);

            Rect climb { Renderer::DesignWidth - theme::edge
                    - ui::actionButtonWidth(r, tr("Climb")),
                156.0f, ui::actionButtonWidth(r, tr("Climb")), 64.0f };
            app.touchZone(climb, Zone_Climb);
            ui::actionButton(r, climb, tr("Climb"), true,
                app.touchHeld(Zone_Climb) ? 1.0f : 0.7f + 0.3f * m_pulse);
        }

        void drawOver(App& app, Renderer& r)
        {
            app.hint("A", "climb again");
            app.hint("B", "back");

            constexpr float kW = 1000.0f;
            Rect box { Renderer::DesignWidth * 0.5f - kW * 0.5f, 120.0f, kW, 260.0f };
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.96f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s6);
            float y = inner.y;

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = m_deepest > 0 ? theme::accent : theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            r.text(inner.x, y,
                m_stopped ? tr("You came back down") : tr("The shadow keeps the floor"),
                title);
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            std::string line = m_deepest == 0
                ? std::string(tr("Not one floor. Cross a few more people and bring "
                                 "them along."))
                : format(tr("%d floors, and %u coins for the ones you had not reached."),
                    m_deepest, unsigned(m_earned));
            if (m_found == 1) {
                line += " ";
                line += format(tr("One %s piece came back with you."),
                    tr(qualityName(m_bestFound)));
            } else if (m_found > 1) {
                line += " ";
                line += format(tr("%d pieces came back with you, the best of them %s."),
                    m_found, tr(qualityName(m_bestFound)));
            }
            y += r.textWrapped(Rect { inner.x, y, inner.w, 70.0f }, line, body, 2);

            Rect again { inner.x,
                std::max(inner.bottom() - 64.0f, y + theme::s4),
                ui::actionButtonWidth(r, tr("Climb again")), 64.0f };
            app.touchZone(again, Zone_Climb);
            ui::actionButton(r, again, tr("Climb again"), true,
                app.touchHeld(Zone_Climb) ? 1.0f : 0.7f + 0.3f * m_pulse);

            Rect back { box.right() - 60.0f, box.y + 18.0f, 42.0f, 42.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);
        }

        // ------------------------------------------------------------ state

        int m_phase = Phase_Party;
        float m_clock = 0.0f;
        float m_pulse = 0.0f;

        Member m_you;
        std::vector<Member> m_roster; // the collection, strongest first
        std::vector<int> m_chosen;    // indices into m_roster
        int m_slots = 3;
        int m_cursor = 0;

        std::vector<Member> m_units; // the party as it stands this climb
        int m_floor = 1;
        int m_deepest = 0;
        uint32_t m_best = 0;
        uint32_t m_earned = 0;
        int m_found = 0;         // pieces of gear this climb turned up
        uint8_t m_bestFound = 0; // and the best of them
        bool m_stopped = false;

        Boss m_boss;
        int m_bossHp = 1;
        float m_bossAtk = 1.0f; // falls as a Spark wears it down
        float m_bossShake = 1.0f;
        int m_round = 0;
        std::vector<int> m_order; // this round, by speed; -1 is the shadow
        int m_turn = 0;
        int m_guard = -1; // who is standing in front, if anybody
        float m_beatClock = 0.0f;
        std::string m_log;
    };
}

std::unique_ptr<Scene> makeQuestScene() { return std::make_unique<QuestScene>(); }

} // namespace nxp
