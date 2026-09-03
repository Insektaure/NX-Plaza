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

// What the shop records itself as in a piece's provenance. Written by the
// store when a piece is bought and read by the trophies that ask how a puzzle
// was filled, so it lives here rather than as a literal in both.
inline constexpr const char* kShopSource = "the shop";

struct PieceSet {
    const char* name;
    // Which picture this puzzle is, as a key into the baked artwork.
    const char* image;
    uint8_t count;
};

// The sets.
const std::vector<PieceSet>& pieceSets();

// Where a puzzle sits in the table, by its picture key. -1 when this build has
// no such puzzle, which is what a file written by a newer one looks like.
int pieceSetIndex(const std::string& picture);

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

// Who a piece came from.
//
// The handle is copied at the moment the piece is granted rather than looked up
// later through the crossing id. The collection prunes at its cap, so the card
// that brought a piece is not guaranteed to still be there - and a puzzle that
// forgets a name because you have since met five thousand people is worse than
// one that never knew it. Sixteen characters is the whole of a handle
// (model.h), so the copy costs nothing worth avoiding.
struct PieceSource {
    // Which puzzle, by its picture key rather than its position in the table.
    std::string picture;
    uint8_t piece = 0;
    std::string who;   // their handle, as it read that day
    uint64_t when = 0; // unix seconds
};

// A held set, as a bitmask. Bit N is piece N.
struct PieceInventory {
    // One mask per set, in the order pieceSets() gives them. Stored as hex so
    // profile.json stays readable and so a set can grow without the file
    // meaning something different.
    std::vector<uint32_t> owned;
    int active = 0; // the set crossings currently fill

    // Where each held piece came from. Sparse and unordered: only pieces that
    // are held appear, so a set that shrank simply loses the entries that no
    // longer point anywhere.
    std::vector<PieceSource> sources;

    bool has(int set, uint8_t piece) const;
    // True when this piece was not already held.
    bool take(int set, uint8_t piece);

    // Records who brought a piece. Call only for a piece that was new: a
    // duplicate must not overwrite the person who actually gave it to you.
    void noteSource(int set, uint8_t piece, const std::string& who, uint64_t when);
    // The same, addressed by picture key: what a saved file holds, and what a
    // build with a reordered table still reads correctly.
    void noteSourceFor(const std::string& picture, uint8_t piece, const std::string& who,
        uint64_t when);
    // Null when the piece is not held, or was collected by a build that did not
    // record this yet.
    const PieceSource* sourceFor(int set, uint8_t piece) const;
    int countHeld(int set) const;
    bool complete(int set) const;

    // Fits `owned` to the current set list, keeping what is already there. A
    // build that adds a set must not lose what the last one collected. Also
    // drops sources that no longer name a held piece.
    void normalise();

    std::vector<std::string> toHex() const;
    void fromHex(const std::vector<std::string>& hex, int activeSet);
};

} // namespace nxp
