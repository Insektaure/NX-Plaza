#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nxp {

// Collectible pieces: something to actually get out of crossing somebody.
//
// A pass tells you who walked past. A piece is what you take away from it, and
// it is the difference between an app you check and an app you keep open. This
// is the Puzzle Swap idea: every crossing gives you one, you fill a set, and
// filling it is worth something.
//
// Nobody loses anything. A crossing grants a piece to each side independently
// and takes nothing away, so meeting somebody with an empty collection is worth
// exactly as much as meeting a completist.

struct PieceSet {
    const char* name;
    uint8_t count;
};

// The sets.
const std::vector<PieceSet>& pieceSets();

// Which piece a crossing yields, for a given set.
//
// Derived, not sent: both consoles work it out for themselves and nothing about
// it goes on the wire. That is what keeps this off the protocol entirely - no
// version bump, no field an older build would ignore, nothing to disagree
// about. It depends on:
//
//   who you are   so two people crossing the same stranger get different pieces
//   who they are  so meeting somebody new is worth more than meeting a regular
//   the day       so crossing the same person twice in an afternoon is one
//                 piece, not a way to farm the set
//   the set       so the piece belongs to whatever you are collecting now
//
// `theirId` is the other console's public id; `when` is unix seconds.
uint8_t pieceFor(const std::string& myId, const std::string& theirId, uint64_t when,
    int set);

// A held set, as a bitmask. Bit N is piece N.
struct PieceInventory {
    // One mask per set, in the order pieceSets() gives them. Stored as hex so
    // profile.json stays readable and so a set can grow without the file
    // meaning something different.
    std::vector<uint32_t> owned;
    int active = 0; // the set crossings currently fill

    bool has(int set, uint8_t piece) const;
    // True when this piece was not already held.
    bool take(int set, uint8_t piece);
    int countHeld(int set) const;
    bool complete(int set) const;

    // Fits `owned` to the current set list, keeping what is already there. A
    // build that adds a set must not lose what the last one collected.
    void normalise();

    std::vector<std::string> toHex() const;
    void fromHex(const std::vector<std::string>& hex, int activeSet);
};

} // namespace nxp
