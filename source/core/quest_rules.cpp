#include "core/quest_rules.h"

#include "core/model.h"
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

    // The stat budget of each quality, in points. HP is the odd one out
    // because its base range is four times everybody else's, so a point of
    // it is worth four - see kHpPerPoint.
    //
    // Chosen by simulating a whole climb rather than by looking at the
    // numbers, which is the only way to see what gear is actually worth:
    //
    //     party        bare  common  uncommon  rare  epic  legendary  godlike
    //     four met      3.1     3.9       4.9   6.1   7.8       10.0     13.3
    //     fifteen met   6.9     7.6       8.9  10.1  12.0       14.5     18.3
    //     twenty-five  11.8    12.6      14.0  15.5  17.6       20.4     24.6
    //
    // Those are the numbers after the affix pass, and they went up rather
    // than down. Spreading a budget over five stats instead of two ought to
    // be weaker than concentrating it - and it is not, because a point of
    // health is worth four and staying alive is worth more to this fight
    // than hitting harder. Predicted the wrong way round, measured the
    // right one, and the budgets were left alone.
    //
    // A tier is worth about a floor at the bottom and three at the top, and a
    // full set of the best gear carries a four-person collection roughly to
    // where a twenty-five-person one starts bare. That is the point of it:
    // somewhere to get on a console that is not meeting anybody new. It never
    // replaces the roster, though - the geared deep collection is still nine
    // floors ahead of the geared thin one.
    constexpr uint16_t kBudget[Quality_Count] = { 3, 6, 10, 16, 24, 36 };
    constexpr uint16_t kHpPerPoint = 4;

    const char* kQualities[Quality_Count] = {
        "common", "uncommon", "rare", "epic", "legendary", "godlike",
    };

    const char* kSlots[Slot_Count] = { "weapon", "armour", "ring", "accessory" };

    // Four names a slot can wear, picked by the seed. Deliberately plain
    // objects rather than a fantasy catalogue: this is a plaza, and a lance
    // is easier to picture than a Vorpal Greatsword of the Ninth Dawn.
    // Flat, four to a slot, because the scanner that keeps the translations
    // honest reads a table of labels and cannot see into a second dimension.
    constexpr int kNounsPer = 4;
    const char* kNouns[Slot_Count * kNounsPer] = {
        "Baton", "Cleaver", "Lance", "Hammer",       // weapon
        "Jerkin", "Plate", "Cloak", "Carapace",      // armour
        "Band", "Signet", "Circlet", "Seal",         // ring
        "Charm", "Talisman", "Pendant", "Feather",   // accessory
    };

    // How many stats a piece carries, its own included. This is what makes a
    // tier read as a tier before a single number is: a common has one line
    // on it and a godlike has five, and you can tell them apart across the
    // room. Only the top tier is ever all five, which is its whole identity.
    constexpr uint8_t kAffixes[Quality_Count] = { 1, 2, 2, 3, 4, 5 };

    // Which stat a slot is for, and the four its seed may spill into - every
    // other stat, in the order that suits the slot, so a rotation off the
    // seed picks a different handful each time.
    struct Split {
        int primary;
        int rest[4];
    };
    // 0 hp, 1 mp, 2 atk, 3 def, 4 spd
    constexpr Split kSplit[Slot_Count] = {
        { 2, { 4, 1, 3, 0 } }, // a weapon is attack, then speed, wind, guard
        { 0, { 3, 1, 2, 4 } }, // armour is health, then guard
        { 1, { 4, 3, 0, 2 } }, // a ring is wind
        { 4, { 2, 0, 1, 3 } }, // an accessory is speed
    };

    void addTo(Sheet& s, int which, uint16_t points)
    {
        switch (which) {
        case 0:
            s.hp = uint16_t(s.hp + points * kHpPerPoint);
            break;
        case 1:
            s.mp = uint16_t(s.mp + points);
            break;
        case 2:
            s.atk = uint16_t(s.atk + points);
            break;
        case 3:
            s.def = uint16_t(s.def + points);
            break;
        default:
            s.spd = uint16_t(s.spd + points);
            break;
        }
    }

} // namespace

// ---------------------------------------------------------------- the party

const char* className(uint8_t cls)
{
    switch (cls) {
    case Class_Guard:
        return "Guard";
    case Class_Blade:
        return "Blade";
    case Class_Spark:
        return "Spark";
    default:
        return "Mender";
    }
}

uint8_t classOf(const Mii& mii) { return uint8_t(mii.favouriteColour % Class_Count); }

Sheet& Sheet::operator+=(const Sheet& o)
{
    hp = uint16_t(hp + o.hp);
    mp = uint16_t(mp + o.mp);
    atk = uint16_t(atk + o.atk);
    def = uint16_t(def + o.def);
    spd = uint16_t(spd + o.spd);
    return *this;
}

Sheet sheetFor(const Mii& mii, uint32_t crossed, uint32_t travelled, uint32_t hours,
    uint32_t titles)
{
    uint32_t known = std::min(crossed, 25u);
    // The face's own numbers are documented as 0..127 and clamped on read,
    // but a stat block is not the place to find out that one got through: a
    // height of 200 would come out as a negative speed and sort the party
    // into a shape nobody could explain.
    uint32_t build = std::min<uint32_t>(mii.build, 127);
    uint32_t height = std::min<uint32_t>(mii.height, 127);

    Sheet s;
    s.hp = uint16_t(48 + build / 3 + known * 4);
    s.atk = uint16_t(8 + known + std::min(hours, 60u) / 6);
    s.def = uint16_t(3 + build / 24 + std::min(crossed, 12u) / 2);
    s.spd = uint16_t(10 + (127 - height) / 12 + std::min(travelled, 60u) / 8);
    s.mp = uint16_t(6 + std::min(titles, 32u) / 4 + std::min(crossed, 12u));
    s.cls = classOf(mii);
    return s;
}

// ---------------------------------------------------------------- the tower

Boss bossFor(int floor)
{
    // Clamped only to keep the arithmetic honest. Boss health passes what a
    // uint16_t holds at floor 1056, which is some hundreds of floors below
    // where any party dies, and below the record file's own ceiling.
    int f = std::min(999, std::max(1, floor));
    Boss b;
    b.hp = uint16_t(90 + 62 * f);
    b.atk = uint16_t(12 + 5 * f);
    b.def = uint16_t(2 + f);
    b.spd = uint16_t(11 + f / 2);
    return b;
}

Mii shadowFace(int floor)
{
    Pass p;
    p.portrait = 0x9E3779B9u * uint32_t(floor) + 0x85EBCA6Bu;
    return p.face();
}

// ----------------------------------------------------------------- the loot

const char* qualityName(uint8_t quality)
{
    return kQualities[quality < Quality_Count ? quality : uint8_t(Quality_Common)];
}

const char* slotName(uint8_t slot)
{
    return kSlots[slot < Slot_Count ? slot : uint8_t(Slot_Weapon)];
}

const char* itemNoun(const Item& item)
{
    uint8_t slot = item.slot < Slot_Count ? item.slot : uint8_t(Slot_Weapon);
    return kNouns[slot * kNounsPer + item.seed % kNounsPer];
}

Sheet itemBonus(const Item& item)
{
    Sheet bonus;
    if (!item.valid())
        return bonus;

    uint8_t quality
        = item.quality < Quality_Count ? item.quality : uint8_t(Quality_Common);
    uint8_t slot = item.slot < Slot_Count ? item.slot : uint8_t(Slot_Weapon);

    // The seed is the roll. It is picked when the thing falls and then never
    // changes, so an item is as random as any loot game's and still costs
    // two bytes to keep - but every bit of it has to be spent, or two rares
    // of a kind come out identical and there is nothing to compare:
    //
    //   bits 0-1    which of the four names it wears
    //   bits 2-4    where in the ring of other stats the rest starts
    //   bits 5-9    how much of the budget stays on the stat the slot is
    //               for, anywhere from 55 to 86 per cent
    //   bits 10-15  the budget itself, from 70 to 133 per cent of the tier's
    //
    // That last one is what makes a drop worth reading rather than just
    // worth counting: the tiers overlap, so a well-rolled rare beats a poor
    // epic, and the best thing you own is not simply the highest tier you
    // have seen. It averages a hair over 100 per cent, so the tiers land
    // where they were simulated.
    uint32_t roll = item.seed;
    uint32_t turn = (roll >> 2) & 7;          // which stats the rest goes to
    uint32_t split = 55 + ((roll >> 5) & 31); // how much stays on the primary
    uint32_t scale = 70 + ((roll >> 10) & 63);

    // Rounded, not truncated. Truncating every roll cost four and a half
    // per cent of every tier, which is a balance change nobody asked for
    // hiding inside an integer divide.
    uint32_t others = kAffixes[quality] - 1u;
    uint32_t budget = (uint32_t(kBudget[quality]) * scale + 50) / 100;
    if (budget < others + 1)
        budget = others + 1; // a point each, at the very least

    uint32_t primary = (budget * split + 50) / 100;
    if (budget - primary < others)
        primary = budget - others;
    if (primary < 1)
        primary = 1;
    uint32_t rest = budget - primary;

    const Split& parts = kSplit[slot];
    addTo(bonus, parts.primary, uint16_t(primary));

    // The rest, in descending shares: one clear second stat and then
    // smaller ones, which is easier to read than four equal crumbs. Never
    // zero, or a legendary would print with a legendary's tier and an
    // uncommon's number of lines.
    uint32_t weight = others;
    uint32_t total = others * (others + 1) / 2;
    uint32_t given = 0;
    for (uint32_t i = 0; i < others; i++) {
        uint32_t share = i + 1 == others
            ? (rest > given ? rest - given : 1u) // unsigned: never let it wrap
            : std::max(1u, (rest * weight) / total);
        addTo(bonus, parts.rest[(turn + i) % 4], uint16_t(share));
        given += share;
        weight--;
    }
    return bonus;
}

std::string itemSummary(const Item& item)
{
    Sheet b = itemBonus(item);
    const char* names[5] = { "HP", "MP", "ATK", "DEF", "SPD" };
    uint16_t values[5] = { b.hp, b.mp, b.atk, b.def, b.spd };
    std::string out;
    for (int i = 0; i < 5; i++) {
        if (values[i] == 0)
            continue;
        if (!out.empty())
            out += "   ";
        out += format("+%u %s", unsigned(values[i]), names[i]);
    }
    return out;
}

uint32_t itemRating(const Item& item)
{
    Sheet b = itemBonus(item);
    // Weighed the way the fight weighs them: a point of health is a quarter
    // of a point of anything else, which is exactly how it was handed out.
    return uint32_t(b.hp) / kHpPerPoint + b.atk + b.def + b.spd + b.mp;
}

Item rollDrop(int floor, uint16_t nextId, int chance)
{
    Item item;
    // Half the time the shadow leaves nothing, on every floor and every
    // climb alike, unless the run is carrying something that says otherwise.
    if (int(randomBelow(100)) >= std::max(0, std::min(100, chance)))
        return item;

    int f = std::min(999, std::max(1, floor));
    // Deeper is better, and past floor twenty it is the only place the good
    // things are: common falls away to nothing while the top three tiers
    // only start once the tower is worth the trouble.
    uint32_t weights[Quality_Count] = {
        uint32_t(std::max(1, 60 - 3 * f)),
        uint32_t(25 + f),
        uint32_t(8 + f),
        uint32_t(std::max(0, f - 3)),
        uint32_t(std::max(0, (f - 8) / 2)),
        uint32_t(std::max(0, (f - 15) / 4)),
    };
    uint32_t total = 0;
    for (uint32_t w : weights)
        total += w;

    uint32_t pick = randomBelow(total);
    uint8_t quality = Quality_Common;
    for (uint8_t i = 0; i < Quality_Count; i++) {
        if (pick < weights[i]) {
            quality = i;
            break;
        }
        pick -= weights[i];
    }

    item.id = nextId;
    item.quality = quality;
    item.slot = uint8_t(randomBelow(Slot_Count));
    // Every bit of the seed is read by itemBonus, so all sixteen are rolled.
    item.seed = uint16_t(randomBelow(65536));
    item.floor = uint16_t(f);
    return item;
}

} // namespace nxp
