#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nxp {

// Blessings taken during a climb, and gone when it ends.
//
// The quest is an auto-battler: everything is decided before you press the
// button, and after that you watch. That is the point of it, but it leaves
// a climb with exactly one decision in it. These are the others - three
// drawn from the pool after every fifth floor, one taken, and they last
// until the party comes back down.
//
// Nothing here is stored. A blessing cannot make a person permanently
// better, which is deliberate: what an ally is worth comes off their pass
// and how often you cross them, and a climb that could edit that would
// quietly unhook the tower from the plaza it is standing in.
//
// The pool is written to argue with itself. Reckless and Bulwark pull in
// opposite directions; Rally and Second wind both answer "somebody is going
// to fall" and answer it differently; Momentum is worthless on a short
// climb and the best thing here on a long one; three of them are worth
// nothing at all unless the right class is in the party, so they are only
// ever offered when it is.

// The three numbers the stacking blessings move are sums, not products:
// every copy adds its share of the party's own number rather than a share of
// whatever the copies before it had already made. Multiplied, they grew
// exponentially with the floor - a blessing every fifth floor, and past the
// first dozen every offer is three of the six that stack - while the tower
// grows in a straight line, so a long climb ended up hitting for hundreds of
// times its weight and being hit for 1 at floor 475.
//
// Summed, a sum can go below anything sensible, so what the fight reads is
// attack() and suffered(), held to kLeast. Kept apart from the sums rather
// than applied as each copy lands, so the order the blessings were taken in
// makes no difference to where they end up.
struct Boons {
    static constexpr float kLeast = 0.25f; // a quarter, at the very least

    float atk = 1.0f;       // what the party's blows are multiplied by
    float taken = 1.0f;     // and what it suffers
    float hp = 1.0f;        // applied to everybody's maximum, once

    float attack() const { return atk > kLeast ? atk : kLeast; }
    float suffered() const { return taken > kLeast ? taken : kLeast; }

    int spd = 0;
    int crit = 12;          // per cent
    int mpPerFloor = 4;
    int skillCost = 6;
    bool fullMp = false;    // wind back to full at every floor
    bool secondWind = false;
    bool spentWind = false;
    float rally = 0.0f;     // extra attack for each ally who has fallen
    float perFloor = 0.0f;  // extra attack for each floor since it was taken
    int since = 0;
    bool lastStand = false;
    bool guardSweep = false; // standing in front covers the sweep too
    float mendPower = 3.0f;  // a heal is the Mender's attack times this
    float mendAt = 0.6f;     // and fires below this share of health
    float sparkBite = 0.08f; // how much of the shadow's arm a Spark takes
    int dropChance = 50;
};

struct BoonInfo {
    uint8_t id;
    const char* name;
    const char* what;
    // The class it needs to be worth anything, plus one. Zero is anybody.
    uint8_t needs;
    // Whether it can be offered again once held, each copy adding the same
    // again - see Boons.
    bool stacks;
    // And how many copies it can be offered up to: 1 for the ones that do
    // not stack. See kPool in quest_boons.cpp for why the rest stop.
    uint8_t most;
};

const BoonInfo& boonInfo(uint8_t id);

// Applies it to the run. Once per copy taken: the ones that stack add up,
// and the pool never offers one that does not stack a second time.
void applyBoon(Boons& boons, uint8_t id);

// Three to choose between: never one already held unless it stacks, never
// a stacking one already held as many times as it may be, never one whose
// class is not in the party, and never the same one twice in an offer. Fewer than
// three when the pool has run that low, none when it is empty.
std::vector<uint8_t> offerBoons(const std::vector<uint8_t>& held, uint32_t classes);

} // namespace nxp
