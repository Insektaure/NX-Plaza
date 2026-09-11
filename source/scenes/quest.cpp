#include "app.h"
#include "core/i18n.h"
#include "core/quest_boons.h"
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

    // What a shadow is made of.
    const Color kShadowInk = Color::hex(0x2E2733);

    // miiFigure has no canvas of its own to fade as a whole, so it hands
    // the opacity to every part and the parts overlap
    // One flat colour at full strength has no seams
    // to show, and reads as "down" more plainly than a ghost does.
    const Color kFallenInk = Color::hex(0x8A857E);

    class QuestScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Roster = Touch_SceneBase,
            Zone_Boon,
            Zone_Back,
        };

        bool coversChrome() const override { return true; }

        // A climb runs itself, so + waits until it is over one way or another.
        bool blocksExit() const override
        {
            return m_phase == Phase_Fight || m_phase == Phase_Won
                || m_phase == Phase_Boon;
        }

        void onEnter(App& app) override
        {
            m_floor = 1;
            buildRoster(app);
            syncUnits();
            previewFloor();
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
            case Phase_Won:
                agePops(dt);
                if (input.accept()) {
                    // Every fifth floor, the tower offers something before
                    // it offers the next floor.
                    if (m_floor % 5 == 0 && offer())
                        return;
                    nextFloor();
                } else if (input.back()) {
                    stop();
                }
                return;
            case Phase_Boon:
                if (m_offer.empty()) {
                    takeBoon(-1);
                    return;
                }
                if (tapped && tap.is(Zone_Boon) && tap.index >= 0
                    && tap.index < int(m_offer.size())) {
                    m_boonPick = tap.index;
                    takeBoon(m_boonPick);
                    return;
                }
                if (input.navLeft)
                    m_boonPick = (m_boonPick + int(m_offer.size()) - 1)
                        % int(m_offer.size());
                if (input.navRight)
                    m_boonPick = (m_boonPick + 1) % int(m_offer.size());
                if (input.accept())
                    takeBoon(m_boonPick);
                return;
            default:
                break;
            }

            if (tapped) {
                if (tap.is(Zone_Back))
                    app.popOverlay();
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
                // Back to the party screen rather than straight up the
                // tower again. A run that has just ended is exactly when
                // somebody wants to change who goes and what they are
                // carrying, and starting the next one for them takes that
                // away at the only moment it matters.
                if (input.accept())
                    regroup(app);
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
            syncUnits();

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
            if (input.pressed(HidNpadButton_ZR))
                app.pushOverlay(makeQuestBagScene());
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);
            ui::plazaBackdrop(r, 0.0f, kHorizon, 0.0f);
            ui::plazaGround(r, 0.0f, kHorizon);

            drawHeader(r);

            // One field in two states. Choosing a party is done on the
            // same ground the fight happens on, in the same formation, with
            // the same shadow waiting on the left - so what you arrange is
            // what you watch, and nothing moves when the fight begins.
            if (m_phase == Phase_Party) {
                drawBossSide(r);
                drawField(r);
                drawPanel(r, true);
                drawRoster(app, r);
                drawPartyHints(app, r);
                return;
            }

            drawTurnOrder(r);
            drawBossSide(r);
            drawField(r);
            drawPanel(r, false);
            drawHeld(r);
            drawSay(r);
            drawPops(r);

            if (m_phase == Phase_Boon) {
                drawBoons(app, r);
                return;
            }
            if (m_phase == Phase_Won) {
                drawWon(app, r);
                return;
            }
            if (m_phase == Phase_Over) {
                drawOver(app, r);
                return;
            }
            app.hint("B", "stop");
        }

    private:
        enum Phase : int {
            Phase_Party = 0, // choosing who goes
            Phase_Fight,     // a floor resolving itself
            Phase_Won,       // the floor's spoils, waiting on you
            Phase_Boon,      // three blessings, waiting on you
            Phase_Over,      // wiped or stopped; there is no top
        };

        // A number rising off whoever it happened to, which is how a fight
        // says what it did now that there is no line of text saying it.
        struct Pop {
            float x = 0.0f;
            float y = 0.0f;
            float life = 1.0f; // 1 at the target, 0 gone
            int amount = 0;
            bool heal = false;
            bool crit = false;
        };

        struct Member {
            std::string id; // empty for your own Mii
            std::string name;
            Mii face;
            Sheet base;  // what they are worth with nothing on
            Sheet sheet; // and with their gear folded in
            int maxHp = 1;       // their own, times whatever Hale did to it
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

        // The field, laid out the way a turn-based battle has been laid out
        // since Final Fantasy: the thing you are fighting stands alone on
        // the left, your party is ranged down the right in a stagger so
        // nobody hides behind anybody, and their condition is in a panel
        // under them rather than on cards across the middle of the fight.
        static constexpr float kHorizon = 300.0f;
        static constexpr float kBossX = 560.0f;
        static constexpr float kBossGround = 560.0f;
        static constexpr float kBossFigure = 320.0f;

        static constexpr float kPartyX = 1210.0f; // the front of the line
        static constexpr float kPartyStepX = 78.0f;
        static constexpr float kPartyGround = 430.0f;
        static constexpr float kPartyStepY = 40.0f;
        static constexpr float kPartyFigure = 190.0f;

        static constexpr float kPanelX = 1150.0f;
        static constexpr float kPanelY = 648.0f;
        static constexpr float kPanelRow = 56.0f;
        static constexpr float kHeldY = 660.0f;
        static constexpr float kOrderY = 24.0f;
        // The box a head sits in, and how much of it the face may take.
        //
        // Solved rather than guessed, for the worst aspect the artwork
        // uses, with the chin standing on kOrderFloor:
        //
        //     hair top    = chin - faceHeight * 1.20  >= 0
        //     beard foot  = chin + faceHeight * 0.10  <= the card
        static constexpr float kOrderCard = 84.0f;
        static constexpr float kOrderCardH = 104.0f;
        static constexpr float kOrderHead = 56.0f;
        static constexpr float kOrderFloor = 88.0f; // where the chin rests
        // The pitch, twelve wider than the card, so no two cards touch.
        // Six of these span 576px of a 1920 screen, centred, well clear of
        // the floor number on the left and the week on the right.
        static constexpr float kOrderCell = 96.0f;

        static constexpr float kBeat = 0.55f; // one action
        static constexpr float kPopLife = 0.9f;
        static constexpr float kLunge = 30.0f; // how far a step forward goes

        static constexpr float kRosterY = 700.0f;
        static constexpr float kRosterH = 168.0f;
        // Wide enough for the longest class in any of the eleven: the
        // Portuguese Mender is CURANDEIRO, ten uppercase characters, and
        // at 104 it ran straight out of its card.
        static constexpr float kRosterCell = 124.0f;

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
                syncUnits();
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
            syncUnits();
        }

        // -------------------------------------------------------- the climb

        // Back to choosing. The roster is rebuilt because a climb can have
        // changed what people are carrying, and their sheets with it.
        void regroup(App& app)
        {
            m_pops.clear();
            m_say.clear();
            m_spoils = Item {};
            m_floor = 1;
            buildRoster(app); // a climb can have changed what people carry
            syncUnits();
            previewFloor();
            m_phase = Phase_Party;
            m_clock = 0.0f;
        }

        // The party as it stands, standing. Kept up to date while you are
        // still choosing, so the party screen and the fight draw the same
        // people from the same list in the same formation - the whole point
        // of the two screens looking alike is that they are the same screen
        // with the shadow awake.
        void syncUnits()
        {
            m_units.clear();
            m_units.push_back(m_you);
            for (int index : m_chosen) {
                if (index >= 0 && index < int(m_roster.size()))
                    m_units.push_back(m_roster[size_t(index)]);
            }
            for (Member& u : m_units) {
                u.maxHp = std::max(1, int(float(u.sheet.hp) * m_boons.hp));
                u.hp = u.maxHp;
                u.mp = int(u.sheet.mp);
            }
        }

        // What is waiting on the floor you are about to climb to, so the
        // party screen can show it without the fight having started.
        void previewFloor()
        {
            m_boss = bossFor(m_floor);
            m_bossHp = int(m_boss.hp);
            m_bossAtk = float(m_boss.atk);
            m_bossShake = 1.0f;
        }

        void startOrAgain()
        {
            m_floor = 1;
            m_deepest = 0;
            m_earned = 0;
            m_paidNow = 0;
            m_found = 0;
            m_bestFound = 0;
            m_stopped = false;
            m_wiped = false;
            m_fallen = 0;
            m_pops.clear();
            m_say.clear();
            m_spoils = Item {};
            QuestRecord::get().noteClimb();

            m_boons = Boons {};
            m_held.clear();
            syncUnits();

            // Straight up. Blessings come after the fifth floor and every
            // fifth after that, and not before the first: a climb should
            // open on the tower rather than on a menu, and a party that has
            // not cleared anything has not earned anything.
            //
            // It costs a thin collection the only blessing it would ever
            // have seen - measured, six hundred climbs a party:
            //
            //     party        bare   every 5   with one at the door
            //     four met      3.1     3.1            3.4
            //     fifteen met   7.1     7.5            7.9
            //     twenty-five  12.2    13.8           14.8
            //
            // which is the trade: the shallow end of the tower is now
            // plain, and gear is the only thing that moves it.
            m_floor = 1;
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
            m_say.clear();
            m_phase = Phase_Fight;
            m_clock = 0.0f;
        }

        // True when there was something to offer, which is the caller's
        // cue to stop and let it be chosen.
        bool offer()
        {
            uint32_t classes = 0;
            for (const Member& u : m_units)
                classes |= 1u << u.sheet.cls;
            m_offer = offerBoons(m_held, classes);
            if (m_offer.empty())
                return false;
            m_boonPick = 0;
            m_phase = Phase_Boon;
            m_clock = 0.0f;
            return true;
        }

        void takeBoon(int which)
        {
            if (which >= 0 && which < int(m_offer.size())) {
                uint8_t id = m_offer[size_t(which)];
                m_held.push_back(id);
                applyBoon(m_boons, id);
                // Hale is the one that has to reach into people rather than
                // sit in the run's numbers: everybody's ceiling moves, and
                // the room it opens is given to them straight away.
                for (Member& u : m_units) {
                    int was = u.maxHp;
                    u.maxHp = std::max(1, int(float(u.sheet.hp) * m_boons.hp));
                    if (u.hp > 0 && u.maxHp > was)
                        u.hp += u.maxHp - was;
                }
            }
            m_offer.clear();

            nextFloor();
        }

        void nextFloor()
        {
            // A breather, not a heal: a third back and a little of the wind,
            // so a climb is a war of attrition and the party that reaches
            // floor nine is the one that got there cheaply.
            for (Member& u : m_units) {
                if (u.hp <= 0)
                    continue;
                u.hp = std::min(u.maxHp, u.hp + u.maxHp / 3);
                u.mp = m_boons.fullMp
                    ? int(u.sheet.mp)
                    : std::min(int(u.sheet.mp), u.mp + m_boons.mpPerFloor);
            }
            m_boons.since++;
            m_floor++;
            beginFloor();
        }

        void stop()
        {
            m_stopped = true;
            m_phase = Phase_Over;
            m_clock = 0.0f;
        }

        void agePops(float dt)
        {
            for (Pop& p : m_pops)
                p.life -= dt / kPopLife;
            m_pops.erase(std::remove_if(m_pops.begin(), m_pops.end(),
                             [](const Pop& p) { return p.life <= 0.0f; }),
                m_pops.end());
        }

        // Spawned over whoever it happened to, so a sweep puts a number on
        // every head at once and you can see the shape of the round without
        // reading a word of it.
        void popOver(const Rect& who, int amount, bool heal, bool crit)
        {
            m_pops.push_back(Pop { who.centerX(), who.y + 24.0f, 1.0f, amount, heal,
                crit });
        }

        void runFight(App& app, float dt)
        {
            m_bossShake = std::min(1.0f, m_bossShake + dt * 4.0f);
            for (Member& u : m_units)
                u.lunge = std::max(0.0f, u.lunge - dt * 4.0f);
            agePops(dt);

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
                m_wiped = true;
                m_phase = Phase_Over;
                m_clock = 0.0f;
                return;
            }
            // Thirty rounds is not a fight any more, it is two walls, and
            // the shadow is the one that can wait.
            if (m_round > 30) {
                m_wiped = true;
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
                int sa = a < 0 ? int(m_boss.spd) : spdOf(m_units[size_t(a)]);
                int sb = b < 0 ? int(m_boss.spd) : spdOf(m_units[size_t(b)]);
                return sa > sb;
            });
            m_turn = 0;
        }

        // A sheet is what somebody is; these are what they are *now*. Read
        // through here everywhere a fight uses a number, so a blessing
        // never has to be written into anybody.
        int atkOf(const Member& u) const
        {
            float gain = (1.0f + m_boons.rally * float(m_fallen))
                * (1.0f + m_boons.perFloor * float(m_boons.since));
            if (m_boons.lastStand && standing() == 1)
                gain *= 2.0f;
            return std::max(1, int(float(u.sheet.atk) * m_boons.atk * gain));
        }

        int spdOf(const Member& u) const { return int(u.sheet.spd) + m_boons.spd; }

        int standing() const
        {
            int up = 0;
            for (const Member& u : m_units) {
                if (u.hp > 0)
                    up++;
            }
            return up;
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
        static int hitFor(int atk, int def, float mult, bool* crit = nullptr)
        {
            int base = int(float(atk) - float(def) * 0.5f);
            base = std::max(1, int(float(base) * mult));
            int wobble = std::max(1, base / 4);
            int out = base + int(randomBelow(uint32_t(wobble * 2 + 1))) - wobble;
            bool big = randomBelow(100) < 12;
            if (big)
                out = int(float(out) * 1.7f);
            if (crit)
                *crit = big;
            return std::max(1, out);
        }

        void allyActs(int index)
        {
            Member& u = m_units[size_t(index)];
            if (u.hp <= 0)
                return;
            u.lunge = 1.0f;

            bool hasMp = u.mp >= m_boons.skillCost;
            bool crit = false;
            switch (u.sheet.cls) {
            case Class_Blade:
                if (hasMp) {
                    u.mp -= m_boons.skillCost;
                    strike(hitFor(atkOf(u), m_boss.def, 1.8f, &crit), crit);
                    return;
                }
                break;
            case Class_Mender:
                if (hasMp) {
                    Member* hurt = weakest();
                    if (hurt && float(hurt->hp) < float(hurt->maxHp) * m_boons.mendAt) {
                        u.mp -= m_boons.skillCost;
                        int given = int(float(atkOf(u)) * m_boons.mendPower);
                        int before = hurt->hp;
                        hurt->hp = std::min(hurt->maxHp, hurt->hp + given);
                        popOver(unitRect(int(hurt - m_units.data())),
                            hurt->hp - before, true, false);
                        return;
                    }
                }
                break;
            case Class_Spark:
                if (hasMp) {
                    u.mp -= m_boons.skillCost;
                    strike(hitFor(atkOf(u), m_boss.def, 1.3f, &crit), crit);
                    // Worn down rather than out-hit: the shadow's arm is
                    // what a Spark takes away, and it does not come back.
                    m_bossAtk = std::max(float(m_boss.atk) * 0.6f,
                        m_bossAtk * (1.0f - m_boons.sparkBite));
                    return;
                }
                break;
            case Class_Guard:
                if (hasMp && m_guard != index) {
                    u.mp -= m_boons.skillCost;
                    m_guard = index;
                    m_say = format(tr("%s stands in front"), u.name.c_str());
                    return;
                }
                break;
            default:
                break;
            }

            strike(hitFor(atkOf(u), m_boss.def, 1.0f, &crit), crit);
        }

        void strike(int dealt, bool crit)
        {
            m_bossHp -= dealt;
            m_bossShake = 0.0f;
            m_say.clear();
            popOver(bossRect(), dealt, false, crit);
        }

        Member* weakest()
        {
            Member* out = nullptr;
            float worst = 2.0f;
            for (Member& u : m_units) {
                if (u.hp <= 0)
                    continue;
                float share = float(u.hp) / float(std::max(1, u.maxHp));
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

            // Every fourth round it swings at the lot for a little over
            // half, and a number lands on every head at once - which is the
            // whole reason the numbers are better than a line of text.
            if (m_round % 4 == 0) {
                m_say.clear();
                for (size_t i = 0; i < m_units.size(); i++) {
                    Member& u = m_units[i];
                    if (u.hp <= 0)
                        continue;
                    bool crit = false;
                    float bite = 0.55f * m_boons.taken
                        * (m_boons.guardSweep && m_guard >= 0 ? 0.5f : 1.0f);
                    int dealt = hitFor(atk, u.sheet.def, bite, &crit);
                    u.hp = std::max(0, u.hp - dealt);
                    popOver(unitRect(int(i)), dealt, false, crit);
                    if (u.hp == 0)
                        m_fallen++;
                    if (u.hp == 0)
                        m_say = format(tr("%s falls"), u.name.c_str());
                }
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
            bool crit = false;
            float bite = (guarded ? 0.5f : 1.0f) * m_boons.taken;
            int dealt = hitFor(atk, target->sheet.def, bite, &crit);
            target->hp = std::max(0, target->hp - dealt);
            popOver(unitRect(int(target - m_units.data())), dealt, false, crit);
            if (target->hp == 0) {
                // Back up once, if this run was blessed with it.
                if (m_boons.secondWind && !m_boons.spentWind) {
                    m_boons.spentWind = true;
                    target->hp = std::max(1, target->maxHp / 3);
                    popOver(unitRect(int(target - m_units.data())), target->hp, true,
                        false);
                    m_say = format(tr("%s gets back up"), target->name.c_str());
                    return;
                }
                m_fallen++;
                if (guarded)
                    m_guard = -1;
                m_say = format(tr("%s falls"), target->name.c_str());
                return;
            }
            m_say.clear();
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
            m_paidNow = 0;
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
                m_paidNow = kFloorCoins;
            }

            m_say.clear();
            m_pops.clear();
            takeDrop(app, record);

            m_clock = 0.0f;
            m_phase = Phase_Won;
        }

        void takeDrop(App& app, QuestRecord& record)
        {
            m_spoils = Item {};
            Item fell = rollDrop(m_floor, record.nextId(), m_boons.dropChance);
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
            m_spoils = fell;
            if (fell.quality > m_bestFound)
                m_bestFound = fell.quality;
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

        Rect bossRect() const
        {
            float jolt = (1.0f - m_bossShake) * 16.0f;
            return Rect { kBossX - kBossFigure * 0.42f + jolt,
                kBossGround - kBossFigure, kBossFigure * 0.84f, kBossFigure };
        }

        // Staggered down and to the right, so the fifth in the line is not
        // standing behind the first. Lunging is a step to the left, towards
        // the thing being hit.
        Rect unitRect(int index) const
        {
            float lunge = index >= 0 && index < int(m_units.size())
                ? m_units[size_t(index)].lunge
                : 0.0f;
            float x = kPartyX + float(index) * kPartyStepX - lunge * kLunge;
            float ground = kPartyGround + float(index) * kPartyStepY;
            return Rect { x - kPartyFigure * 0.42f, ground - kPartyFigure,
                kPartyFigure * 0.84f, kPartyFigure };
        }

        void drawBossSide(Renderer& r) const
        {
            Rect box = bossRect();
            // Beaten, it goes pale rather than see-through, for the same
            // reason the fallen do: alpha on a figure made of overlapping
            // parts shows every one of its joins.
            bool beaten = m_bossHp <= 0;
            Color ink = beaten ? kShadowInk.mix(theme::bg0, 0.72f) : kShadowInk;
            if (!beaten) {
                r.glow(Rect { box.centerX() - 170.0f, box.centerY() - 170.0f, 340.0f,
                           340.0f },
                    theme::danger.scaleAlpha(0.16f), 1.8f);
            }
            r.ellipse(box.centerX(), kBossGround + 8.0f, kBossFigure * 0.34f, 15.0f,
                theme::bg0.scaleAlpha(beaten ? 0.10f : 0.35f), 0.0f);
            ui::miiFigure(r, box, shadowFace(m_floor), 1.0f, false, &ink);

            constexpr float kBarW = 620.0f;
            Rect bar { kBossX - kBarW * 0.5f, kBossGround + 40.0f, kBarW, 24.0f };
            float share = m_boss.hp > 0
                ? std::max(0.0f, float(m_bossHp) / float(m_boss.hp))
                : 0.0f;
            r.roundRect(bar, 12.0f, theme::bg2);
            if (share > 0.0f) {
                r.roundRect(Rect { bar.x, bar.y, bar.w * share, bar.h }, 12.0f,
                    theme::danger);
            }

            TextStyle count;
            count.size = theme::textSm;
            count.weight = FontWeight::Bold;
            count.color = theme::fg2;
            r.text(Rect { bar.x, bar.bottom() + theme::s2, bar.w, 28.0f },
                format("%d / %u", std::max(0, m_bossHp), unsigned(m_boss.hp)), count,
                Align::Center, VAlign::Top);

            TextStyle stat;
            stat.size = theme::textXs;
            stat.color = theme::fg3;
            stat.tracking = theme::trackingWide;
            r.text(Rect { bar.x, bar.bottom() + 42.0f, bar.w, 26.0f },
                format("ATK %u   DEF %u   SPD %u", unsigned(int(m_bossAtk)),
                    unsigned(m_boss.def), unsigned(m_boss.spd)),
                stat, Align::Center, VAlign::Top);
        }

        // The party on the field: figures only, because their numbers are
        // in the panel below and a fight is easier to follow when the thing
        // moving is the only thing to look at.
        void drawField(Renderer& r) const
        {
            for (size_t i = 0; i < m_units.size(); i++) {
                const Member& u = m_units[i];
                Rect box = unitRect(int(i));
                bool down = u.hp <= 0;
                r.ellipse(box.centerX(), box.bottom() + 8.0f, kPartyFigure * 0.30f,
                    12.0f, theme::bg0.scaleAlpha(down ? 0.12f : 0.30f), 0.0f);
                if (u.lunge > 0.01f) {
                    Color rim = theme::accent.scaleAlpha(0.45f * u.lunge);
                    ui::miiSilhouette(r, box.inset(-5.0f), u.face, rim);
                }
                if (down) {
                    Color grey = kFallenInk;
                    ui::miiFigure(r, box, u.face, 1.0f, false, &grey);
                } else {
                    ui::miiFigure(r, box, u.face);
                }
            }
        }

        // Who is up, in the order they go. The one acting is lit; the
        // rest wait their turn along the top, which is the one thing a
        // watcher cannot work out from the field itself.
        //
        // Every head gets a card of its own, sized so the whole head fits
        // inside it. Without one the highlight was cut to the cell while
        // the hair was not, so it missed the top of whoever it was meant
        // to be pointing at, spilled onto the neighbours, and left a row
        // of faces whose chins lined up and whose crowns did not.
        void drawTurnOrder(Renderer& r) const
        {
            if (m_order.empty())
                return;
            int count = int(m_order.size());
            float total = float(count) * kOrderCell;
            float x = Renderer::DesignWidth * 0.5f - total * 0.5f;
            int acting = m_turn - 1;

            for (int i = 0; i < count; i++) {
                bool now = i == acting;
                Rect card { x + float(i) * kOrderCell, kOrderY, kOrderCard,
                    kOrderCardH };
                r.roundRect(card, theme::r3,
                    now ? theme::accent.scaleAlpha(0.30f) : theme::bg2);
                r.strokeRect(card, theme::r3, now ? theme::stroke * 2.0f : theme::stroke,
                    now ? theme::accent : theme::stroke1);

                // miiHead stands the face on the bottom of what it is
                // given, so the well's floor is where the chin goes and
                // everything else - hair above, beard below - is the room
                // left around it. 56 across keeps the face clear of the
                // 52px line where miiHead stops drawing beards at all.
                Rect well { card.centerX() - kOrderHead * 0.5f, card.y + 8.0f,
                    kOrderHead, kOrderFloor - 8.0f };
                // Every face at full strength. Whose turn it is is said
                // by the card behind them - accent fill, accent border -
                // and once there was a card to say it, fading the face as
                // well stopped reading as "waiting" and started reading as
                // a half-drawn Mii. The same mistake the roster cards made.
                int slot = m_order[size_t(i)];
                if (slot < 0) {
                    Color ink = kShadowInk;
                    ui::miiHead(r, well, shadowFace(m_floor), 1.0f, &ink);
                } else if (slot < int(m_units.size())) {
                    ui::miiHead(r, well, m_units[size_t(slot)].face);
                }
            }
        }

        // One row a head. Before a climb the bars are all full and say
        // nothing, so the row carries the stat block instead; during one
        // the numbers that move are the only ones worth the width.
        void drawPanel(Renderer& r, bool resting) const
        {
            if (m_units.empty())
                return;
            float height = float(m_units.size()) * kPanelRow + theme::s4 * 2.0f;
            Rect panel { kPanelX, kPanelY, Renderer::DesignWidth - theme::edge - kPanelX,
                height };
            r.roundRect(panel, theme::r3, theme::bg1);
            r.strokeRect(panel, theme::r3, theme::stroke, theme::stroke1);

            Rect inner = panel.inset(theme::s5, theme::s4);
            for (size_t i = 0; i < m_units.size(); i++) {
                const Member& u = m_units[i];
                float y = inner.y + float(i) * kPanelRow;
                bool down = u.hp <= 0;

                TextStyle name;
                name.size = theme::textSm;
                name.weight = FontWeight::Bold;
                name.color = down ? theme::fg4 : theme::fg1;
                r.text(inner.x, y + 2.0f,
                    r.ellipsize(u.name, name, 230.0f), name);

                TextStyle role;
                role.size = theme::textXs;
                role.color = down ? theme::fg4 : theme::accent;
                role.tracking = theme::trackingWide;
                role.uppercase = true;
                r.text(inner.x, y + 28.0f, tr(className(u.sheet.cls)), role);

                if (resting) {
                    TextStyle stat;
                    stat.size = theme::textXs;
                    stat.color = theme::fg3;
                    stat.tracking = theme::trackingWide;
                    r.text(Rect { inner.x, y + 4.0f, inner.w, 26.0f },
                        format("HP %u   ATK %u   DEF %u   SPD %u   MP %u",
                            unsigned(u.sheet.hp), unsigned(u.sheet.atk),
                            unsigned(u.sheet.def), unsigned(u.sheet.spd),
                            unsigned(u.sheet.mp)),
                        stat, Align::Right, VAlign::Top);
                    continue;
                }

                TextStyle count;
                count.size = theme::textXs;
                count.color = down ? theme::fg4 : theme::fg3;
                r.text(Rect { inner.x + 244.0f, y + 4.0f, 110.0f, 24.0f },
                    format("%d", std::max(0, u.hp)), count, Align::Right, VAlign::Top);

                Rect bar { inner.x + 366.0f, y + 8.0f, inner.right() - inner.x - 366.0f,
                    14.0f };
                drawBar(r, bar,
                    float(std::max(0, u.hp)) / float(std::max(1, u.maxHp)),
                    down ? theme::bg3 : theme::success);
                drawBar(r, Rect { bar.x, bar.bottom() + 5.0f, bar.w, 8.0f },
                    u.sheet.mp == 0
                        ? 0.0f
                        : float(u.mp) / float(std::max<uint16_t>(1, u.sheet.mp)),
                    theme::info);
            }
        }

        // What this climb is carrying boon wise, down the bottom-left, where the
        // roster sits while you are still choosing.
        void drawHeld(Renderer& r) const
        {
            if (m_held.empty())
                return;

            TextStyle label;
            label.size = theme::textXs;
            label.color = theme::fg4;
            label.tracking = theme::trackingWider;
            label.uppercase = true;
            // Its own word, not the encounter card's "carrying" - that one
            // labels the radishes and rally ghosts on somebody's pass, and
            // a key shared between the two could only ever be right for
            // one of them.
            r.text(theme::edge, kHeldY, tr("blessings"), label);

            TextStyle name;
            name.size = theme::textSm;
            name.weight = FontWeight::Bold;
            name.color = theme::accent;

            float y = kHeldY + 30.0f;
            for (uint8_t id : m_held) {
                // Eight is more than any real climb takes - the pool is
                // eighteen and they come one floor in five - but a run that
                // went deep enough would otherwise write over the hint bar.
                if (y + 28.0f > kHeldY + 8.0f * 30.0f)
                    break;
                r.text(theme::edge, y, tr(boonInfo(id).name), name);
                y += 30.0f;
            }
        }

        // The one thing a number cannot say by itself - somebody stepping in
        // front, somebody going down.
        void drawSay(Renderer& r) const
        {
            if (m_say.empty())
                return;
            TextStyle line;
            line.size = theme::textSm;
            line.color = theme::fg3;
            line.tracking = theme::trackingWide;
            r.text(Rect { theme::edge, kOrderY + kOrderCardH + theme::s3,
                      Renderer::DesignWidth - theme::edge * 2.0f, 30.0f },
                m_say, line, Align::Center, VAlign::Top);
        }

        // The numbers, rising and fading off whoever they happened to.
        // A damage number is not body text and must not be coloured like
        // it. fg1 flips with the palette - near-white on the dark theme,
        // near-black on the light one - so the same code read cleanly on
        // one and vanished into the scenery on the other.
        //
        // Fixed colours, the same in both, over a dark shadow: a
        // number that floats across a Mii, a shadow and the ground has no
        // background to be chosen against, so it carries its own.
        void drawPops(Renderer& r) const
        {
            const Color kHurt = Color::hex(0xF7F3EC);
            const Color kBig = Color::hex(0xF2B23A);
            const Color kMend = Color::hex(0x74C25C);
            const Color kUnder = Color::hex(0x17130F);

            for (const Pop& p : m_pops) {
                float gone = 1.0f - p.life;
                float alpha = std::min(1.0f, p.life * 2.4f);

                TextStyle text;
                text.size = p.crit ? theme::textXl : theme::textLg;
                text.weight = FontWeight::Bold;
                text.tracking = theme::trackingTight;

                std::string body = p.heal ? format("+%d", p.amount)
                                          : format("%d", p.amount);
                Rect at { p.x - 120.0f, p.y - gone * 54.0f, 240.0f, 60.0f };

                text.color = kUnder.scaleAlpha(alpha * 0.7f);
                r.text(at.offset(3.0f, 3.0f), body, text, Align::Center, VAlign::Top);

                text.color = (p.heal ? kMend : (p.crit ? kBig : kHurt))
                                 .scaleAlpha(alpha);
                r.text(at, body, text, Align::Center, VAlign::Top);
            }
        }

        // The party, as cards along the foot of the screen. In the party
        // phase these are the chosen ones with their stats; during a climb
        // they are the same cards with bars in them, so nothing jumps about
        // between picking somebody and watching them fight.
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
        // Each one carries a face, a name and a class,
        // because "who should I bring" is a question
        // about what they do, and a strip of bare heads made you count
        // rather than choose.
        void drawRoster(App& app, Renderer& r)
        {
            Rect strip { theme::edge, kRosterY, kPanelX - theme::s5 - theme::edge,
                kRosterH };
            if (m_roster.empty()) {
                TextStyle empty;
                empty.size = theme::textSm;
                empty.color = theme::fg3;
                r.textWrapped(strip, tr("Nobody has crossed you yet - you climb alone."),
                    empty, 2);
                return;
            }

            int fits = std::max(1, int(strip.w / kRosterCell));
            int first = std::max(0,
                std::min(m_cursor - fits / 2, int(m_roster.size()) - fits));
            // Room for the ring. ui::card draws its focus outside the box,
            // so a clip tight to the strip sliced the top and bottom off
            // whichever cell the cursor was on.
            r.pushClipVertical(strip.inset(0.0f, -theme::focusRoom));
            float x = strip.x;
            for (int i = first; i < int(m_roster.size()) && x < strip.right(); i++) {
                const Member& m = m_roster[size_t(i)];
                Rect cell { x, strip.y, kRosterCell - theme::s2, strip.h };
                bool chosen = inParty(i);
                bool focused = i == m_cursor;
                app.touchZone(cell, Zone_Roster, i);
                ui::card(r, cell, focused ? 0.7f + 0.3f * m_pulse : 0.0f,
                    chosen ? theme::bg2 : theme::bg1, theme::r2);

                // Every face at full strength. Whether they are coming is
                // already said by the card's fill, the dot in its corner and
                // the colour of the name and the class under it; fading the
                // face as well read as a half-drawn Mii rather than as a
                // fifth way of saying the same thing.
                constexpr float kHead = 80.0f;
                ui::miiHead(r, Rect { cell.centerX() - kHead * 0.5f, cell.y + 18.0f,
                                kHead, kHead },
                    m.face);

                TextStyle name;
                name.size = theme::textXs;
                name.weight = FontWeight::Bold;
                name.color = chosen ? theme::fg1 : theme::fg3;
                r.text(Rect { cell.x + 4.0f, cell.y + kHead + 32.0f, cell.w - 8.0f,
                          22.0f },
                    r.ellipsize(m.name, name, cell.w - 8.0f), name, Align::Center,
                    VAlign::Top);

                // No wide tracking here, unlike every other small-caps
                // label in the app: the six per cent it adds is six per
                // cent this cell has not got. Ellipsized as a backstop, so
                // a language that grows one of these later loses a letter
                // rather than painting over the card next to it.
                TextStyle role;
                role.size = theme::textXs;
                role.color = chosen ? theme::accent : theme::fg4;
                role.uppercase = true;
                float room = cell.w - 8.0f;
                r.text(Rect { cell.x + 4.0f, cell.y + kHead + 58.0f, room, 22.0f },
                    r.ellipsize(tr(className(m.sheet.cls)), role, room), role,
                    Align::Center, VAlign::Top);

                if (chosen)
                    r.circle(cell.right() - 15.0f, cell.y + 15.0f, 7.0f, theme::accent);
                x += kRosterCell;
            }
            r.popClip();

            // What the one under the cursor is worth, which is the whole
            // question when they are not in the party and their row is not
            // in the panel.
            const Member& at = m_roster[size_t(m_cursor)];
            TextStyle line;
            line.size = theme::textXs;
            line.color = theme::fg3;
            line.tracking = theme::trackingWide;
            r.text(strip.x, strip.bottom() + theme::s3,
                format("HP %u   ATK %u   DEF %u   SPD %u   MP %u",
                    unsigned(at.sheet.hp), unsigned(at.sheet.atk),
                    unsigned(at.sheet.def), unsigned(at.sheet.spd),
                    unsigned(at.sheet.mp)),
                line);
        }

        void drawPartyHints(App& app, Renderer& r)
        {
            app.hint("A", inParty(m_cursor) ? "leave behind" : "bring along");
            app.hint("X", "climb");
            app.hint("Y", "gear");
            app.hint("ZR", "the bag");
            app.hint("B", "back");

            std::string label = format(tr("%d of %d places"), int(m_chosen.size()) + 1,
                m_slots);
            TextStyle note;
            note.size = theme::textSm;
            note.color = theme::fg3;
            r.text(Rect { theme::edge, kRosterY - 34.0f, kPanelX - theme::s5
                      - theme::edge, 28.0f },
                label, note, Align::Left, VAlign::Top);
        }

        // Three, side by side, and the run keeps whichever one you take.
        //
        // Laid out as cards rather than a list because they are meant to be
        // compared, and because the question is which of these three, not
        // which of eighteen - the pool is deep so that the three are
        // different, not so that the screen is long.
        void drawBoons(App& app, Renderer& r)
        {
            app.hint("A", "take it");

            // Over the fight, not instead of it, and behind a veil so the
            // field stops competing for the eye.
            r.rect(r.viewport(), theme::scrim);

            constexpr float kW = 420.0f;
            constexpr float kH = 300.0f;
            constexpr float kHead = 44.0f;
            constexpr float kSub = 30.0f;

            int count = std::max(1, int(m_offer.size()));
            float row = float(count) * kW + float(count - 1) * theme::s5;
            float boxW = row + theme::s7 * 2.0f;
            float boxH = kHead + theme::s3 + kSub + theme::s6 + kH
                + theme::s7 * 2.0f;
            Rect box { Renderer::DesignWidth * 0.5f - boxW * 0.5f,
                Renderer::DesignHeight * 0.5f - boxH * 0.5f, boxW, boxH };
            r.roundRect(box, theme::r5, theme::bg1);
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s7);

            TextStyle head;
            head.size = theme::textLg;
            head.weight = FontWeight::Bold;
            head.color = theme::fg1;
            head.tracking = theme::trackingTight;
            r.text(Rect { inner.x, inner.y, inner.w, kHead },
                tr("The stair offers you something"), head, Align::Center,
                VAlign::Top);

            TextStyle sub;
            sub.size = theme::textSm;
            sub.color = theme::fg3;
            r.text(Rect { inner.x, inner.y + kHead + theme::s3, inner.w, kSub },
                tr("It lasts as long as the climb does."), sub, Align::Center,
                VAlign::Top);

            float y = inner.y + kHead + theme::s3 + kSub + theme::s6;
            for (int i = 0; i < int(m_offer.size()); i++) {
                const BoonInfo& boon = boonInfo(m_offer[size_t(i)]);
                Rect card { inner.x + float(i) * (kW + theme::s5), y, kW, kH };
                bool here = i == m_boonPick;
                app.touchZone(card, Zone_Boon, i);
                // bg2 on bg1, so a card reads as a card inside the box
                // rather than as a hole in it.
                ui::card(r, card, here ? 0.7f + 0.3f * m_pulse : 0.0f,
                    here ? theme::bg3 : theme::bg2, theme::r4);
                Rect at = card.inset(theme::s6, theme::s6);

                TextStyle name;
                name.size = theme::textXl;
                name.weight = FontWeight::Bold;
                name.color = here ? theme::accent : theme::fg1;
                name.tracking = theme::trackingTight;
                name.leading = theme::leadingTight;
                float used = r.textWrapped(Rect { at.x, at.y, at.w, 120.0f },
                    tr(boon.name), name, 2);

                TextStyle what;
                what.size = theme::textBase;
                what.color = theme::fg3;
                what.leading = theme::leadingNormal;
                r.textWrapped(Rect { at.x, at.y + used + theme::s4, at.w, 160.0f },
                    tr(boon.what), what, 4);

                // The class it wants, when it wants one. It is only ever
                // offered to a party that has one, but knowing why it is
                // here is half of deciding whether to take it.
                if (boon.needs != 0) {
                    TextStyle tag;
                    tag.size = theme::textXs;
                    tag.color = theme::accent;
                    tag.tracking = theme::trackingWide;
                    tag.uppercase = true;
                    r.text(Rect { at.x, at.bottom() - 24.0f, at.w, 24.0f },
                        tr(className(uint8_t(boon.needs - 1))), tag, Align::Right,
                        VAlign::Top);
                }
            }
        }

        // What the floor gave up, and whether to go on. A screen rather
        // than a beat, because a drop nobody had time to read is a drop
        // that may as well not have fallen.
        void drawWon(App& app, Renderer& r)
        {
            app.hint("A", "next floor");
            app.hint("B", "stop here");

            constexpr float kW = 860.0f;
            // Over a veil and fully opaque. At 0.97 the field showed
            // through, which in the light theme is pale text over a pale
            // Mii over a pale panel and nothing readable at all.
            r.rect(r.viewport(), theme::scrim);
            Rect box { Renderer::DesignWidth * 0.5f - kW * 0.5f, 300.0f, kW, 300.0f };
            r.roundRect(box, theme::r5, theme::bg1);
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s6);
            float y = inner.y;

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = theme::accent;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            r.text(inner.x, y, format(tr("Floor %d is yours"), m_floor), title);

            // This floor's coin, not the climb's purse: the panel is about
            // the floor it is standing on.
            if (m_paidNow > 0) {
                TextStyle purse;
                purse.size = theme::textBase;
                purse.weight = FontWeight::Bold;
                purse.color = theme::fg2;
                float width = ui::coinAmountWidth(r, m_paidNow, purse);
                ui::coinAmount(r, inner.right() - width,
                    y + (title.size * theme::leadingTight - r.lineHeight(purse)) * 0.5f,
                    m_paidNow, purse);
            }
            y += title.size * theme::leadingTight + theme::s5;

            if (!m_spoils.valid()) {
                TextStyle none;
                none.size = theme::textBase;
                none.color = theme::fg4;
                r.text(inner.x, y, tr("nothing this time"), none);
                return;
            }

            // The drop, laid out the way the gear screen lays one out, so
            // the thing you just found and the thing you are about to put
            // on read the same.
            TextStyle noun;
            noun.size = theme::textXl;
            noun.weight = FontWeight::Bold;
            noun.color = theme::fg1;
            r.text(inner.x, y, tr(itemNoun(m_spoils)), noun);

            TextStyle tier;
            tier.size = theme::textSm;
            tier.weight = FontWeight::Bold;
            tier.color = ui::qualityColour(m_spoils.quality);
            tier.tracking = theme::trackingWide;
            tier.uppercase = true;
            r.text(Rect { inner.x, y + 6.0f, inner.w, 30.0f },
                tr(qualityName(m_spoils.quality)), tier, Align::Right, VAlign::Top);
            y += noun.size * theme::leadingSnug + theme::s3;

            TextStyle gain;
            gain.size = theme::textBase;
            gain.color = theme::fg3;
            r.text(inner.x, y, itemSummary(m_spoils), gain);
        }

        void drawOver(App& app, Renderer& r)
        {
            app.hint("A", "pick again");
            app.hint("B", "back");

            constexpr float kW = 1000.0f;
            r.rect(r.viewport(), theme::scrim);
            Rect box { Renderer::DesignWidth * 0.5f - kW * 0.5f, 120.0f, kW, 260.0f };
            r.roundRect(box, theme::r5, theme::bg1);
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
                m_stopped ? tr("You came back down") : tr("The party falls"), title);
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
        uint32_t m_earned = 0;   // the whole climb's purse
        uint32_t m_paidNow = 0;  // and what the floor just cleared was worth
        int m_found = 0;         // pieces of gear this climb turned up
        uint8_t m_bestFound = 0; // and the best of them
        bool m_stopped = false;

        Boons m_boons;                // what this climb has been blessed with
        std::vector<uint8_t> m_held;  // and which ones, so none comes twice
        std::vector<uint8_t> m_offer; // the three on the table
        int m_boonPick = 0;

        std::vector<Pop> m_pops;
        Item m_spoils;        // what the floor just now gave up, if anything
        bool m_wiped = false; // the run ended standing or it did not

        Boss m_boss;
        int m_bossHp = 1;
        float m_bossAtk = 1.0f; // falls as a Spark wears it down
        float m_bossShake = 1.0f;
        int m_round = 0;
        std::vector<int> m_order; // this round, by speed; -1 is the shadow
        int m_turn = 0;
        int m_guard = -1;  // who is standing in front, if anybody
        int m_fallen = 0;  // how many have gone down this climb, for Rally
        float m_beatClock = 0.0f;
        std::string m_say; // the odd thing a number cannot say by itself
    };
}

std::unique_ptr<Scene> makeQuestScene() { return std::make_unique<QuestScene>(); }

} // namespace nxp
