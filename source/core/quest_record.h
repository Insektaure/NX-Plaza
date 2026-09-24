#pragma once

#include "core/quest_rules.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nxp {

// How the climbing has gone, and everything it has turned up:
// sdmc:/switch/nx-plaza/quest.dat
//
// Here rather than in profile.json's score table with the tower's and the
// dash's records because what is in it decides payouts. A floor pays coins
// the first time it is reached and never again, so the record of what has
// been reached is the whole of the anti-farming rule - and a rule kept in
// plain JSON is a rule anybody can rewind with a text editor to be paid for
// the same floors twice. The gear is here for the same reason: it is what
// the tower is climbed for now.
//
// So this file is the wallet's file, in miniature: the same header, the same
// hash over its own contents keyed on the console's secret token, the same
// refusal to trust a file that does not verify. That token is in
// identity.json and nowhere else, which makes forging this as much work as
// forging the coins.
//
// It is tamper resistant, not tamper proof, and it does not pretend
// otherwise: the token is on the same SD card as the file.
class QuestRecord {
public:
    // A whole climb's worth. A run from the bottom to
    // the top turns up five to eight hundred pieces, and at 250 the bag was
    // full long before the top and throwing gear out on the way up. Eight
    // kilobytes of file at eight bytes a piece.
    //
    // Raising it is safe in both directions. A file written with more than a
    // reader's limit is refused rather than truncated, so a bag of 1000
    // opened by a build that still says 250 reads as no record at all - which
    // is why this only ever goes up.
    static constexpr size_t kBagLimit = 1000;

    // Four item ids, in slot order. Zero is an empty slot.
    struct Loadout {
        uint16_t worn[Slot_Count] = { 0, 0, 0, 0 };
    };

    static QuestRecord& get();

    // Reads quest.dat. A missing file is a console that has never climbed;
    // one that does not verify is treated the same way, which errs towards
    // no record rather than towards free coins and free gear.
    void load();

    // Writes it, if anything changed.
    bool flush();

    // Whether a cleared floor waits for you or carries on by itself. A
    // preference rather than progress, but it lives here because this is
    // the quest's file and a climb is where it is set - the alternative was
    // a row in Settings for something only one screen has ever heard of.
    bool autoAdvance() const { return (m_flags & kAutoAdvance) != 0; }
    void setAutoAdvance(bool on);

    uint32_t deepest() const { return m_deepest; }
    uint32_t climbs() const { return m_climbs; }

    // Records a floor as reached. True only when it is deeper than anything
    // before; this is the all-time record the shelf shows, not the thing a
    // payout is gated on any more - see the week, below.
    bool noteFloor(uint32_t floor);
    void noteClimb();

    // ------------------------------------------------------------ the week
    //
    // The tower pays for a floor once a week rather than once ever, so a
    // console that has finished climbing still has a reason to go back on a
    // Monday. It is one number because a climb always starts at the bottom:
    // "floors up to here have been paid" says everything, and cannot be
    // farmed by re-clearing floor three.
    //
    // Which Monday it is comes from the plaza's clock, never this console's,
    // for exactly the reason the daily coins do: a clock its owner can set
    // is a clock that pays weekly every five minutes.

    // Which Monday the plaza last said it was. Zero until a check-in has
    // answered even once.
    uint32_t week() const { return m_week; }

    // When this week turns, in unix seconds; 0 when no week is known yet.
    //
    // For saying how long is left and nothing else. The week itself turns on
    // the plaza's clock, which is the only one that can be trusted to mint a
    // coin - this is worked out from the week the plaza last gave us and then
    // compared against the console's own clock, which is wrong on plenty of
    // consoles. A countdown that is an hour out is a countdown; a coin that
    // is an hour out is a bug.
    uint64_t weekEndsAt() const;

    // The deepest floor already paid for this week.
    uint32_t paidThisWeek() const { return m_paidThisWeek; }

    // Rolls the week over when the plaza's clock says it is a new one.
    // Cheap and safe to call on every check-in, like the wallet's own.
    void notePlazaTime(uint64_t serverTime);

    // True the first time this floor is cleared in the current week, and
    // records it. What both the coin and the certain drop hang on.
    bool notePaidFloor(uint32_t floor);

    // ------------------------------------------------------------ the bag

    const std::vector<Item>& items() const { return m_items; }
    bool bagFull() const { return m_items.size() >= kBagLimit; }

    // The id to stamp on the next thing that falls.
    //
    // Handed out in order, one past the last, for as long as sixteen bits
    // last - which a bag of a thousand, filled from the bottom of the tower
    // to the top, can spend in eighty climbs. After that it is the lowest id
    // that nothing holds: not a piece in the bag, and not a peg anybody
    // still has it on. Reusing one is safe because discard() takes a piece
    // off whoever was wearing it, so a freed id is one no loadout can come
    // back pointing at.
    //
    // Which means an id says nothing about age once the counter has run
    // out. The bag's own order does: add() appends, and nothing reorders it.
    uint16_t nextId() const;

    // Takes the item as it is, id and all. False when the bag is full, which
    // is the caller's cue to say so rather than to silently drop it, and
    // when the id is already in use, which nextId() never hands out.
    bool add(const Item& item);

    // How many pieces add() has taken since the app started, and the id of
    // the last. Not saved. For a screen that wants to notice something new
    // arriving without asking the ids, which stopped being in order the
    // moment the counter ran out.
    uint32_t added() const { return m_added; }
    uint16_t lastAdded() const { return m_lastAdded; }

    // Kept, or let go. A locked piece is refused by discard(), skipped by
    // clearOut(), and never chosen as the worst spare when a drop needs
    // room - that last one being the point, since nobody is asked first.
    void setLocked(uint16_t itemId, bool on);

    // Removes it from the bag and from whoever was wearing it. Does nothing
    // at all if it is locked.
    void discard(uint16_t itemId);

    const Item* find(uint16_t itemId) const;

    // ----------------------------------------------------------- the party
    //
    // Keyed by the crossing's public id, and by the empty string for your own
    // Mii. A person who leaves the collection keeps their gear on paper until
    // release() is called with who is still there, which is the same shape as
    // the extras file's own orphan sweep.

    Loadout loadout(const std::string& owner) const;

    // Puts `itemId` (0 to clear) in `slot` for `owner`, taking it off
    // anybody else who was wearing it first: one item, one wearer.
    void equip(const std::string& owner, uint8_t slot, uint16_t itemId);

    // Who is wearing it, if anybody. The owner can legitimately be the empty
    // string, which is why this answers with a bool rather than with a name.
    bool wearer(uint16_t itemId, std::string& owner) const;

    // ------------------------------------------------------- whetstones
    //
    // Bought in the shop and spent on one piece of gear, which re-rolls its
    // seed and keeps its tier. Nothing is created: the same item comes back
    // the same quality with a different roll behind it, which is the only
    // shape of shop item the tower can sell without undercutting itself.

    uint16_t stones() const { return m_stones; }
    void addStones(uint16_t many);

    // Spends one and re-rolls `itemId`. False when there are none, or when
    // no such item is in the bag. Whoever is wearing it goes on wearing it -
    // it is the same piece with a different roll.
    bool reforge(uint16_t itemId);

    // The lowest-rated thing in the bag that nobody is wearing, or 0 when
    // everything is spoken for. What a full bag throws out to make room.
    uint16_t worstSpare() const;

    // Whether this piece would improve somebody: it sits on a peg that
    // another person has left empty, or it beats the weakest thing anybody
    // is wearing there.
    //
    // A piece already being worn is never an upgrade to anything: it is
    // where it belongs.
    bool wouldUpgrade(const Item& item) const;

    // Whether a piece is free to go: nobody is wearing it and nobody has
    // locked it. The one place those two exemptions are written down, so the
    // panel that counts and the action that throws away cannot disagree
    // about what "spare" means.
    bool spare(const Item& item) const;

    // How many of a rank are spare, on one peg or on all of them - a `slot`
    // outside 0..Slot_Count-1 means every peg. Const, because a screen that
    // only wants the number should not need a bag it could change.
    size_t spareOfRank(uint8_t quality, int slot) const;

    // And the same rank on the same peg, thrown away. Returns how many went,
    // which is always what spareOfRank() said a moment earlier for the same
    // two arguments.
    size_t clearRank(uint8_t quality, int slot);

    // ---------------------------------------------------------- the forge
    //
    // Three pieces in, one of the rank above out.
    //
    // The tower hands out tiers by depth and depth is set by the collection,
    // so without this a console that has crossed four people farms the same
    // epic for ever: legendary does not fall below floor ten, and a party of
    // three in full epic reaches floor eight. Nothing it could do with its
    // time would ever show it a legendary.
    //
    //     four met         deepest  best tier  the party's gear, in points
    //     as it was              6      epic          189
    //     with the forge        14      godlike       531
    //
    // Fourteen is where a four-person party in full godlike stops anyway, so
    // the forge carries a thin collection to its own gear ceiling and not one
    // floor past it - the roster still decides where that ceiling is.
    //
    // Not to be confused with reforge(), which is the whetstone: that keeps
    // the rank and changes the roll, and this changes the rank.
    static constexpr size_t kForgeSlots = 3;

    // Why the three in front of somebody are or are not a recipe. The screen
    // prints one sentence per answer, so every way of being wrong has to be
    // its own answer rather than a bare false.
    enum class Forge : uint8_t {
        Ready, // three spare pieces of a rank, and a rank above to go to
        Empty, // nothing in it yet
        Short, // one or two
        Mixed, // not all of the same rank
        Top,   // godlike, which has nothing above it
    };

    // What they add up to. An id of 0, or one that is not a spare piece in
    // the bag, counts as an empty socket - so a screen may pass whatever it
    // happens to be holding without checking first.
    Forge check(const uint16_t ids[kForgeSlots]) const;

    // What would come out: the rank above, and the peg all three share, or
    // -1 for a peg that will be rolled. Only meaningful when check() says
    // Ready, and false otherwise.
    //
    // Three of a peg making a fourth of that peg is the whole of the second
    // recipe: it is what turns a bag full of scrap into the accessory
    // somebody actually needs, and it costs nothing to offer because the
    // three that went in were the same three either way.
    bool plan(const uint16_t ids[kForgeSlots], uint8_t& quality, int& slot) const;

    // Melts them down and returns the new piece's id, or 0 if they were not
    // a recipe. `atFloor` is where it is rolled - the deepest floor ever
    // reached, and not where the scrap fell. A console stuck on floor six
    // forging gear fit for floor six would have been sold a fix that fixes
    // nothing.
    uint16_t forge(const uint16_t ids[kForgeSlots], uint32_t atFloor);

    // Everything unworn that is worse than the piece on the same peg of
    // `gear` - which is to say everything that could never be an upgrade for
    // whoever is wearing that. A peg with nothing on it sets no threshold,
    // so an empty slot keeps its spares. With `apply` false it only counts,
    // which is what the confirmation needs to say.
    size_t clearOut(const Loadout& gear, bool apply);

    // Unequips everything worn by somebody who is no longer in the
    // collection, so their gear comes back into the bag rather than being
    // held by a name nothing can show.
    size_t release(const std::vector<std::string>& liveOwners);

private:
    QuestRecord() = default;

    std::string body() const;
    std::string signature() const;

    struct Wearing {
        std::string owner;
        Loadout gear;
    };

    Wearing* rowFor(const std::string& owner);
    const Wearing* rowFor(const std::string& owner) const;

    // Drops every row with nothing on any peg. A row is only ever made to
    // hold something, and one left empty is indistinguishable from none -
    // loadout() answers the same for both - except that it still counts
    // towards kMaxEquips, so a collection that dressed and undressed enough
    // people would find equip() quietly refusing somebody new. Called
    // wherever a peg can be emptied. Returns how many went.
    size_t dropEmptyRows();

    uint32_t m_deepest = 0;
    uint32_t m_climbs = 0;
    uint32_t m_week = 0;         // Mondays since the epoch, from the plaza
    uint16_t m_paidThisWeek = 0; // how far up this week has already paid
    // One past the last id handed out in order; 0 once sixteen bits have
    // run out, after which nextId() looks for a free one instead.
    uint16_t m_nextId = 1;
    uint32_t m_added = 0;
    uint16_t m_lastAdded = 0;

    // Moves the counter past `id`, and spends it for good when that was the
    // last one sixteen bits hold.
    void countPast(uint16_t id);

    // Whether any piece in the bag or any peg holds it.
    bool idInUse(uint16_t id) const;

    // Gives every piece that shares an id with an earlier one a fresh id of
    // its own. A record from before the counter could run out carried on
    // stamping the last id there is on everything that fell, so a bag can
    // arrive holding several pieces of the same id. The first keeps it,
    // along with whoever is wearing it; the rest were never on a peg,
    // because a peg holds an id and the id found the first. Returns how many
    // were renumbered.
    size_t renumberTwins();
    uint16_t m_stones = 0;
    // The two bytes behind the counts, which the file has always written
    // as zero. One bit of it is in use; the rest is the next small thing.
    static constexpr uint16_t kAutoAdvance = 1u << 0;
    uint16_t m_flags = 0;
    std::vector<Item> m_items;
    std::vector<Wearing> m_worn;
    bool m_loaded = false;
    bool m_dirty = false;
};

} // namespace nxp
