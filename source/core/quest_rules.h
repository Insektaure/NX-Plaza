#pragma once

#include "core/mii.h"

#include <cstdint>
#include <string>

namespace nxp {

// What the quest is made of, with no drawing in it.
//
// Two scenes need these numbers - the climb itself and the screen where gear
// is moved around - so they live here rather than in either of them, and the
// balance can be read in one file instead of hunted through a draw call.

// ---------------------------------------------------------------- the party

enum Class : uint8_t {
    Class_Guard = 0, // takes the hits
    Class_Blade,     // deals them
    Class_Spark,     // wears the shadow down
    Class_Mender,    // keeps everybody standing
    Class_Count,
};

// The English word, for tr() at the call site.
const char* className(uint8_t cls);

// The twelve favourite colours a Mii has, four ways. A real Mii's shirt is
// the one thing about a face that reads as a uniform, and the 3DS picked its
// heroes' classes the same way.
uint8_t classOf(const Mii& mii);

// Everything a fighter is, and also what a piece of gear adds: the bonus is
// the same five numbers, so equipping is addition and nothing has to know
// about both shapes.
struct Sheet {
    uint16_t hp = 0;
    uint16_t mp = 0;
    uint16_t atk = 0;
    uint16_t def = 0;
    uint16_t spd = 0;
    uint8_t cls = Class_Guard;

    Sheet& operator+=(const Sheet& o);
};

// Nothing about a fighter is stored. Every number comes from what the app
// already knows, so a party re-reads itself as the collection changes and
// there is no second copy of anybody to go stale:
//
//   HP    their Mii's build, and how many times you have crossed them
//   ATK   how many times you have crossed them, and their hours played
//   DEF   their build again, and half of the crossings
//   SPD   their Mii's height - shorter is quicker - and how many people
//         they have met, because a console that gets out is a quick one
//   MP    how many titles are on their console, and the crossings again
//
// Crossings are in four of the five on purpose: a pass met once is a stranger
// who will do very little, and the person you cross every morning carries the
// party. Your own Mii reads the same formula from your own record - people
// met where a crossing count would be, every crossing you have ever had where
// their travels would be.
Sheet sheetFor(const Mii& mii, uint32_t crossed, uint32_t travelled, uint32_t hours,
    uint32_t titles);

// ---------------------------------------------------------------- the tower

struct Boss {
    uint16_t hp = 1;
    uint16_t atk = 1;
    uint16_t def = 0;
    uint16_t spd = 1;
};

// The tower has no top. A party's damage is fixed by who is in it and by what
// they carry; the shadow's health is not, so a climb ends where the roster
// and the gear run out rather than at a number picked here.
Boss bossFor(int floor);

// The same shadow on the same floor every time, so floor seven is a place
// rather than a shuffle.
Mii shadowFace(int floor);

// ----------------------------------------------------------------- the loot

enum Quality : uint8_t {
    Quality_Common = 0,
    Quality_Uncommon,
    Quality_Rare,
    Quality_Epic,
    Quality_Legendary,
    Quality_Godlike,
    Quality_Count,
};

enum Slot : uint8_t {
    Slot_Weapon = 0,
    Slot_Armour,
    Slot_Ring,
    Slot_Accessory,
    Slot_Count,
};

// Eight bytes, and everything else about a piece of gear is worked out from
// them: its bonus, its name, what it is worth. Storing the numbers a formula
// can produce would mean a balance change could not reach the gear people
// already own.
struct Item {
    uint16_t id = 0; // 0 is "no item"; ids are never reused
    uint8_t quality = Quality_Common;
    uint8_t slot = Slot_Weapon;
    uint16_t seed = 0;
    uint16_t floor = 0; // where it fell, for the line under its name

    bool valid() const { return id != 0; }
};

const char* qualityName(uint8_t quality);
const char* slotName(uint8_t slot);
const char* itemNoun(const Item& item);

// What it adds. The quality is the budget and the seed decides how it is
// split: seven tenths into the stat the slot is for, the rest into one of
// three the seed chooses, so two rares of the same kind are not the same
// item.
Sheet itemBonus(const Item& item);

// What it adds, written out - "+12 ATK   +4 SPD" - in the order the stat
// blocks use. Two screens show this and they have to agree, which is the
// only reason it is a function rather than four lines in each of them.
std::string itemSummary(const Item& item);

// One number to sort and compare by. Not a stat, and it does not pretend to
// be: it is there so a list can put the best first and a card can say which
// of two is the upgrade.
uint32_t itemRating(const Item& item);

// What a shadow on this floor leaves behind, if anything. `nextId` is the
// caller's counter, so ids stay unique across the whole record.
//
// `chance` is the per cent that anything falls at all - fifty, unless a
// climb has been blessed with Scavenger. It is the same on every floor and
// every climb otherwise: the weekly reset pays in coins and nothing else,
// because a certain drop on reset day would make the first climb of a week
// worth twice any other.
Item rollDrop(int floor, uint16_t nextId, int chance = 50);

} // namespace nxp
