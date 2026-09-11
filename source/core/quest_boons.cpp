#include "core/quest_boons.h"

#include "core/quest_rules.h"
#include "core/util.h"

#include <algorithm>

namespace nxp {

namespace {

    uint32_t randomBelow(uint32_t n)
    {
        uint32_t bits = 0;
        randomBytes(&bits, sizeof(bits));
        return n == 0 ? 0 : bits % n;
    }

    // Eighteen, which is enough that a run of six sees a third of them and
    // two runs rarely rhyme. `needs` is a class plus one; those three are
    // dead weight without it and are held back until the party has one.
    //
    // Measured over six hundred climbs a party, one offered after every
    // fifth floor:
    //
    //     party        bare   blessed
    //     four met      3.1     3.1
    //     fifteen met   7.1     7.5
    //     twenty-five  12.2    13.8
    //
    // Nothing at all for a thin collection, which never reaches the fifth
    // floor to be offered one. That is the shape on purpose: gear is what
    // moves a thin party - a full set is worth nine floors to them - and
    // this is texture for a run that is already going well, not a way up
    // the tower. Which is why it is drawn rather than bought, and gone
    // when the run is.
    const BoonInfo kPool[] = {
        { 1, "Whetstone", "The party hits a fifth harder.", 0 },
        { 2, "Ironclad", "The party takes a fifth less.", 0 },
        { 3, "Hale", "A fifth more health, and it fills now.", 0 },
        { 4, "Fleet", "Everybody is three quicker.", 0 },
        { 5, "Reckless", "A third harder, and a quarter softer.", 0 },
        { 6, "Bulwark", "A third tougher, and a fifth weaker.", 0 },
        { 7, "Keen edge", "Telling blows land three times as often.", 0 },
        { 8, "Second wind", "The first to fall gets up once, at half.", 0 },
        { 9, "Rally", "Each one who falls makes the rest hit a fifth harder.", 0 },
        { 10, "Momentum", "The party hits a twentieth harder for every floor from "
                          "here.",
            0 },
        { 11, "Last stand", "Alone, the last one standing hits twice as hard.", 0 },
        { 12, "Deep breath", "Twice as much wind back between floors.", 0 },
        { 13, "Battle rhythm", "Everybody starts each floor with their wind full.",
            0 },
        { 14, "Cheap tricks", "Specials cost four instead of six.", 0 },
        { 15, "Long watch", "Standing in front covers the sweep as well.",
            Class_Guard + 1 },
        { 16, "Mending hands", "The Mender heals half again, and steps in sooner.",
            Class_Mender + 1 },
        { 17, "Bright spark", "The Spark takes twice as much off the shadow's arm.",
            Class_Spark + 1 },
        { 18, "Scavenger", "The shadow leaves something four times in five.", 0 },
    };
    constexpr size_t kPoolSize = sizeof(kPool) / sizeof(kPool[0]);

} // namespace

const BoonInfo& boonInfo(uint8_t id)
{
    for (const BoonInfo& b : kPool) {
        if (b.id == id)
            return b;
    }
    return kPool[0];
}

void applyBoon(Boons& boons, uint8_t id)
{
    switch (id) {
    case 1:
        boons.atk *= 1.20f;
        break;
    case 2:
        boons.taken *= 0.80f;
        break;
    case 3:
        boons.hp *= 1.20f;
        break;
    case 4:
        boons.spd += 3;
        break;
    case 5:
        boons.atk *= 1.35f;
        boons.taken *= 1.25f;
        break;
    case 6:
        boons.taken *= 0.65f;
        boons.atk *= 0.80f;
        break;
    case 7:
        boons.crit = 36;
        break;
    case 8:
        boons.secondWind = true;
        break;
    case 9:
        boons.rally = 0.20f;
        break;
    case 10:
        boons.perFloor = 0.05f;
        boons.since = 0;
        break;
    case 11:
        boons.lastStand = true;
        break;
    case 12:
        boons.mpPerFloor = 8;
        break;
    case 13:
        boons.fullMp = true;
        break;
    case 14:
        boons.skillCost = 4;
        break;
    case 15:
        boons.guardSweep = true;
        break;
    case 16:
        boons.mendPower = 4.5f;
        boons.mendAt = 0.75f;
        break;
    case 17:
        boons.sparkBite = 0.16f;
        break;
    case 18:
        boons.dropChance = 80;
        break;
    default:
        break;
    }
}

std::vector<uint8_t> offerBoons(const std::vector<uint8_t>& held, uint32_t classes)
{
    std::vector<uint8_t> pool;
    for (const BoonInfo& b : kPool) {
        if (std::find(held.begin(), held.end(), b.id) != held.end())
            continue;
        // A blessing about a class nobody is is not a choice, it is a
        // wasted third of the offer.
        if (b.needs != 0 && (classes & (1u << (b.needs - 1))) == 0)
            continue;
        pool.push_back(b.id);
    }

    std::vector<uint8_t> out;
    for (int i = 0; i < 3 && !pool.empty(); i++) {
        size_t at = randomBelow(uint32_t(pool.size()));
        out.push_back(pool[at]);
        pool.erase(pool.begin() + long(at));
    }
    return out;
}

} // namespace nxp
