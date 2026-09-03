#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nxp {

class Store;

// A record of what this console has done, in the shape a console owner already
// knows from every other game on it.
//
// The whole thing is derived. A trophy is a question asked of the store - "have
// you met a hundred people", "is every picture finished" - answered from the
// data that was going to be there, so nothing can drift out of step with
// what actually happened, and there is no second copy of the truth to forge.
// The only thing written down is *when* each was first seen, so the screen can
// give a date and the toast can fire once.
enum class Tier : uint8_t {
    Bronze,
    Silver,
    Gold,
    Platinum,
};

// Everything the conditions read, gathered in one pass over the store.
//
// A struct rather than a Store& handed to each condition: the walk over five
// thousand crossings happens once for eighteen questions, and a condition that
// cannot reach the store cannot accidentally be expensive.
struct TrophyFacts {
    uint32_t uniquePeople = 0;   // distinct consoles crossed
    uint32_t totalCrossings = 0; // every crossing, duplicates included
    uint32_t mostSeen = 0;       // the one console you have crossed most
    uint32_t places = 0;         // distinct place labels
    uint32_t starred = 0;
    uint32_t today = 0;          // crossings today
    uint32_t unopened = 0;       // cards never looked at
    uint32_t tradedBack = 0;     // cards marked as reciprocated
    uint32_t maxHours = 0;       // most hours on any pass crossed
    // Days since the earliest first crossing. How long the oldest card in the
    // collection has been in it, which is not the same as how long the app has
    // been installed - a console that met nobody for a month has neither.
    uint32_t oldestFriendDays = 0;
    // One bit per weekday a crossing happened on, Sunday first. All seven set
    // means the app has been open on every day of the week at some point, not
    // in one week.
    uint8_t weekdays = 0;

    uint32_t piecesHeld = 0;  // across every puzzle
    uint32_t piecesTotal = 0; // every piece there is to find
    uint32_t puzzlesDone = 0;
    uint32_t puzzleCount = 0; // how many there are to finish
    // Distinct people who have handed over a piece. The shop is not one of
    // them, which is the whole point of counting.
    uint32_t pieceDonors = 0;
    // A finished puzzle with no piece bought. A puzzle completed before the
    // provenance sidecar existed has no sources at all and counts as by hand,
    // which is true: there was no shop to buy from.
    bool puzzleByHand = false;

    uint32_t carrying = 0;      // things on your own pass
    uint32_t daysCheckedIn = 0; // from the wallet: ten coins a day
    uint32_t coinsSpent = 0;
    uint32_t balance = 0;
    // Coins won at the races. The one thing a race writes down, and the only
    // reason there can be trophies for it at all: a free race leaves no trace
    // anywhere, so anything about races run would have to be counted rather
    // than derived.
    uint32_t coinsWon = 0;

    bool passReady = false;    // a name, a greeting and something carried
    bool lateCrossing = false; // one between midnight and four

    // Things done to your own pass, which is state and not an event - so they
    // are still questions rather than counters.
    bool ownFace = false;      // been through the Mii maker
    bool ownTheme = false;     // picked a card look other than the first
    bool passSent = false;     // somebody, somewhere, has your pass

    // Things about the people you crossed, each a comparison against your own
    // pass, which the walk is holding anyway.
    bool matchingTheme = false; // same card look as yours
    bool sharedCarry = false;   // carrying something you also carry
    bool namesake = false;      // the same handle as yours
    bool silentPass = false;    // no greeting at all
};

struct Trophy {
    const char* id;   // the key in profile.json. Never rename one of these.
    const char* name;
    const char* hint; // what earns it, in the present tense
    Tier tier;
    // Null for the platinum, which is "all of the others" and so is asked a
    // different question.
    bool (*test)(const TrophyFacts&);
};

const std::vector<Trophy>& trophies();

// Reads the store (and the wallet) once.
TrophyFacts trophyFacts(const Store& store);

// Which trophies the facts satisfy, as one flag per entry in trophies(). The
// platinum is decided from the rest of the vector, so it is filled last.
std::vector<uint8_t> evaluateTrophies(const TrophyFacts& facts);

// The state the screen and the toast both read: what the facts say, made
// sticky by what has already been recorded.
//
// A trophy with a date keeps it. Two things would otherwise take one away
// after the fact - a new entry in the table, which the platinum then waits on,
// and a collection pruned back under a threshold - and since nothing hangs off
// a trophy but its date, keeping it is both kinder and simpler than explaining
// where it went.
std::vector<uint8_t> trophyState(const Store& store, const TrophyFacts& facts);

const char* tierName(Tier tier);

} // namespace nxp
