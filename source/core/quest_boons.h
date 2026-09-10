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

struct Boons {
    float atk = 1.0f;       // what the party's blows are multiplied by
    float taken = 1.0f;     // and what it suffers
    float hp = 1.0f;        // applied to everybody's maximum, once
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
};

const BoonInfo& boonInfo(uint8_t id);

// Applies it to the run. Idempotent only in the sense that the caller must
// not apply the same blessing twice - the pool never offers one twice.
void applyBoon(Boons& boons, uint8_t id);

// Three to choose between: never one already held, never one whose class is
// not in the party, and never the same one twice in an offer. Fewer than
// three when the pool has run that low, none when it is empty.
std::vector<uint8_t> offerBoons(const std::vector<uint8_t>& held, uint32_t classes);

} // namespace nxp
