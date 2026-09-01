#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nxp {

// Extra per-crossing data: sdmc:/switch/nx-plaza/crossings.ext
//
// This exists so features can add per-person data without rewriting
// crossings.idx or crossings.dat. Neither of those can grow safely on a console
// already in someone's hands: the index rejects an unknown version and its
// loader then returns nothing, and the blob's reader drops any record whose
// field list is longer than it expects. Either change costs a live user their
// whole collection. This file changes shape instead, and is built to.
//
// APPEND-ONLY. A change writes one short entry at the end of the file; nothing
// already written is touched.
// Superseded entries are dead weight until compaction rewrites the file once,
// on the same "more waste than live" rule the collection's blob uses.
//
// Every entry is a tag, a length and some bytes, and a reader keeps every field
// it finds whether or not it knows the tag - including through a compaction.
//
// Rows are keyed by the crossing's public id, never by its slot in the index:
// compaction there reassigns slots, so a slot-keyed row would attach itself to
// the wrong person the first time a card was deleted.
//
// Losing this file loses only extras. It is never written in the same breath as
// the collection, so a failure here cannot take a card with it.

// Tag numbers. Never reuse one: an old build carries tags it does not
// understand, so a recycled number comes back later meaning something it never
// meant.
namespace extras {
    // u64: the unix time the crossing was starred. Its presence is the flag;
    // the time is there so a screen can sort by when.
    constexpr uint16_t Favourite = 1;

    // u32: the last collectible this crossing yielded, packed as
    //
    //   bit 31      set when it was one the owner did not already hold
    //   bits 16-30  which set
    //   bits 0-15   which piece
    //
    // The flag is there because "you have this piece" and "this crossing just
    // gave you this piece" are different questions, and only the grant knows
    // the answer to the second. Without it a duplicate reads exactly like a
    // find, and the arrival toast congratulates you twelve times a day for
    // pieces you already had.
    //
    // The sidecar keeps state, not history - compaction discards anything a
    // later entry superseded - so this is the last one, not a log of them.
    constexpr uint16_t LastPiece = 2;
    constexpr uint32_t PieceWasNew = 1u << 31;
}

// One crossing's fields, as read back. Returned by value; the file is written
// through the setters on CrossingExtraFile, which is what lets a change be a
// single small append instead of a rewrite.
class CrossingExtras {
public:
    bool has(uint16_t tag) const;

    bool getU32(uint16_t tag, uint32_t& out) const;
    bool getU64(uint16_t tag, uint64_t& out) const;
    bool getText(uint16_t tag, std::string& out) const;
    bool getBytes(uint16_t tag, std::vector<uint8_t>& out) const;

    bool empty() const { return m_fields.empty(); }
    size_t count() const { return m_fields.size(); }
    std::vector<uint16_t> tags() const;

private:
    friend class CrossingExtraFile;

    struct Field {
        uint16_t tag = 0;
        std::vector<uint8_t> value;
    };
    std::vector<Field> m_fields;

    const Field* find(uint16_t tag) const;
    Field& put(uint16_t tag);
    void erase(uint16_t tag);
};

class CrossingExtraFile {
public:
    // Replays the log. A missing file is not a failure: it means nobody has any
    // extras yet, which is every console until a feature writes one.
    bool load();

    // Appends whatever has changed since the last flush, compacting first when
    // the file has more dead weight than live. Does nothing when nothing has
    // changed.
    bool flush();

    // Null when this crossing has no extras. Creates nothing.
    const CrossingExtras* find(const std::string& id) const;

    // Setting a field: updates memory and queues one entry to append.
    void setU32(const std::string& id, uint16_t tag, uint32_t value);
    void setU64(const std::string& id, uint16_t tag, uint64_t value);
    void setText(const std::string& id, uint16_t tag, const std::string& value);
    void setBytes(const std::string& id, uint16_t tag, const void* data, size_t bytes);

    // Removing one field, or a whole row. Both are recorded as entries of their
    // own: in an append-only file, forgetting something is also something to
    // write down.
    void clearField(const std::string& id, uint16_t tag);
    void dropRow(const std::string& id);

    // Drops rows for crossings no longer in the collection. The collection
    // prunes at its cap and this file has to follow, or it grows forever
    // holding notes about people who are gone.
    size_t dropOrphans(const std::vector<std::string>& liveIds);

    bool dirty() const { return !m_pending.empty(); }
    size_t rows() const { return m_rows.size(); }

    // True when the log holds more superseded entries than live fields.
    bool needsCompaction() const { return m_deadEntries > m_liveFields && m_deadEntries > 32; }

    // True when the file was there and could not be read. Writing is refused
    // while it is set: the entries are probably still on the card, and
    // compacting over them would turn a read error into permanent loss.
    bool unreadable() const { return m_unreadable; }

    static bool validId(const std::string& id);

private:
    void queue(const std::string& id, uint16_t tag, uint8_t kind, const void* data, size_t bytes);
    bool compact();

    std::map<std::string, CrossingExtras> m_rows;
    std::string m_pending; // entries written but not yet on the card
    size_t m_deadEntries = 0;
    size_t m_liveFields = 0;
    bool m_unreadable = false;
};

} // namespace nxp
