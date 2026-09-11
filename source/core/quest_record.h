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
    // Room for three full parties over and a bit. A bag with no bottom is a
    // bag nobody ever tidies, and it is also a file that grows forever.
    static constexpr size_t kBagLimit = 120;

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

    // The id to stamp on the next thing that falls. Ids are never reused, so
    // a loadout cannot come back pointing at whatever took a dead item's
    // place in the list.
    uint16_t nextId() const { return m_nextId; }

    // Takes the item as it is, id and all. False when the bag is full, which
    // is the caller's cue to say so rather than to silently drop it.
    bool add(const Item& item);

    // Removes it from the bag and from whoever was wearing it.
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

    uint32_t m_deepest = 0;
    uint32_t m_climbs = 0;
    uint32_t m_week = 0;         // Mondays since the epoch, from the plaza
    uint16_t m_paidThisWeek = 0; // how far up this week has already paid
    uint16_t m_nextId = 1;
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
