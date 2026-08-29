#pragma once

#include "core/model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nxp {

// The collection on disk: a fixed-size index and a text blob.
//
//
//     500 cards    255 KB     4.1 ms to parse    1.7 ms to write
//   5,000 cards    2.5 MB    37.5 ms            12.3 ms
//
//   * `crossings.idx` - one fixed-size record per crossing, holding
//     everything of fixed width and an offset into the blob. Read as a block
//     with no parsing at all, and a single card is updated by seeking to its
//     record and writing 96 bytes.
//   * `crossings.dat` - the variable text, length-prefixed. Appended to;
//     superseded text is left behind and reclaimed by compaction.
//
// Both are still read whole at startup. That was never the expensive half, and
// keeping the collection in memory means nothing above this file changes.
//
// Not atomic per record: a power cut during a 96-byte write corrupts that one
// record. The loader validates each and drops what it cannot read, so the
// worst case is one lost card rather than a lost collection. Compaction, which
// rewrites everything, goes through the atomic whole-file path.
class CrossingFile {
public:
    // Reads both files. Returns an empty list when neither exists.
    //
    // Records that fail validation are skipped, and `needsCompaction` comes
    // back true so the next save tidies them away.
    std::vector<Crossing> load();

    // True when the last load found dead or unreadable records, or when the
    // blob has accumulated more waste than live text.
    bool needsCompaction() const { return m_wasteBytes * 2 > m_blobBytes + 1; }

    // Writes one crossing's record in place, appending to the blob first when
    // its text has changed. `slot` is the crossing's own, stable across the
    // re-sorting the list does.
    bool writeOne(const Crossing& crossing, bool textChanged);

    // Appends a crossing that has no slot yet, and gives it one.
    bool append(Crossing& crossing);

    // Marks a record dead. The blob space is reclaimed at the next compaction.
    bool erase(const Crossing& crossing);

    // Rewrites both files from `live`, packed, assigning fresh slots. The only
    // path that touches whole files, and the only one that is atomic.
    bool compact(std::vector<Crossing>& live);

    // True when neither file exists, so a JSON collection should be migrated.
    static bool absent();

private:
    uint64_t m_blobBytes = 0;
    uint64_t m_wasteBytes = 0;
    uint32_t m_records = 0; //< including dead ones
};

} // namespace nxp
