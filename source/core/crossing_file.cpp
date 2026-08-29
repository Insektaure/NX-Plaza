#include "core/crossing_file.h"

#include "core/log.h"
#include "core/util.h"

#include <cstdio>
#include <cstring>

namespace nxp {

namespace {
    const char* kIndexFile = "crossings.idx";
    const char* kBlobFile = "crossings.dat";

    constexpr char kMagic[4] = { 'N', 'X', 'P', 'C' };
    constexpr uint16_t kVersion = 1;
    // A face travels as hex, but it is stored here as the bytes it decodes to:
    // half the space, and the field then has to hold any face the relay allows
    // rather than only this version's. The protocol permits 8 to 128 hex
    // characters, so 64 bytes.
    constexpr size_t kMiiMaxBytes = 64;

    // The record, field by field. Offsets are derived rather than written out,
    // because the two that are read back on their own -- the blob pointer --
    // were hard-coded numbers, and a number that has to agree with a layout it
    // is not part of is a number that will one day disagree with it.
    constexpr size_t kOffFlags = 0;
    constexpr size_t kOffTheme = kOffFlags + 1;
    constexpr size_t kOffReserved = kOffTheme + 1;
    constexpr size_t kOffId = kOffReserved + 2;
    constexpr size_t kOffMiiLen = kOffId + 16;
    constexpr size_t kOffMii = kOffMiiLen + 1;
    constexpr size_t kOffPortrait = kOffMii + kMiiMaxBytes;
    constexpr size_t kOffFirstSeen = kOffPortrait + 4;
    constexpr size_t kOffLastSeen = kOffFirstSeen + 8;
    constexpr size_t kOffCount = kOffLastSeen + 8;
    constexpr size_t kOffBlobOffset = kOffCount + 4;
    constexpr size_t kOffBlobLength = kOffBlobOffset + 4;
    constexpr size_t kRecordUsed = kOffBlobLength + 4;

    constexpr uint16_t kRecordSize = 128;
    static_assert(kRecordUsed <= kRecordSize, "the record does not fit");

    constexpr size_t kHeaderSize = 16;

    // Flags in the first byte of a record.
    constexpr uint8_t kLive = 1u << 0;
    constexpr uint8_t kOpened = 1u << 1;
    constexpr uint8_t kTradedBack = 1u << 2;

    // A blob block is a handful of short strings; this is a sanity bound, not a
    // limit anything reaches. A sanitised pass is well under a kilobyte.
    constexpr uint32_t kMaxBlobBlock = 8192;

    // ---- little-endian scalars, written by hand so the file does not depend
    // ---- on how this compiler happens to lay out a struct.

    void put8(uint8_t*& p, uint8_t v) { *p++ = v; }
    void put16(uint8_t*& p, uint16_t v) { *p++ = uint8_t(v); *p++ = uint8_t(v >> 8); }
    void put32(uint8_t*& p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            *p++ = uint8_t(v >> (i * 8));
    }
    void put64(uint8_t*& p, uint64_t v)
    {
        for (int i = 0; i < 8; i++)
            *p++ = uint8_t(v >> (i * 8));
    }

    uint8_t get8(const uint8_t*& p) { return *p++; }
    uint16_t get16(const uint8_t*& p)
    {
        uint16_t v = uint16_t(p[0]) | uint16_t(p[1]) << 8;
        p += 2;
        return v;
    }
    uint32_t get32(const uint8_t*& p)
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++)
            v |= uint32_t(p[i]) << (i * 8);
        p += 4;
        return v;
    }
    uint64_t get64(const uint8_t*& p)
    {
        uint64_t v = 0;
        for (int i = 0; i < 8; i++)
            v |= uint64_t(p[i]) << (i * 8);
        p += 8;
        return v;
    }

    // ---- the blob: length-prefixed UTF-8, and counted lists of the same

    void putText(std::string& out, const std::string& text)
    {
        uint16_t n = uint16_t(std::min<size_t>(text.size(), 0xFFFF));
        out.push_back(char(n & 0xFF));
        out.push_back(char(n >> 8));
        out.append(text, 0, n);
    }

    bool takeText(const uint8_t*& p, const uint8_t* end, std::string& out)
    {
        if (end - p < 2)
            return false;
        uint16_t n = get16(p);
        if (size_t(end - p) < n)
            return false;
        out.assign(reinterpret_cast<const char*>(p), n);
        p += n;
        return true;
    }

    void putList(std::string& out, const std::vector<std::string>& items)
    {
        out.push_back(char(std::min<size_t>(items.size(), 0xFF)));
        for (size_t i = 0; i < items.size() && i < 0xFF; i++)
            putText(out, items[i]);
    }

    bool takeList(const uint8_t*& p, const uint8_t* end, std::vector<std::string>& out)
    {
        if (p >= end)
            return false;
        uint8_t n = get8(p);
        out.clear();
        out.reserve(n);
        for (uint8_t i = 0; i < n; i++) {
            std::string item;
            if (!takeText(p, end, item))
                return false;
            out.push_back(std::move(item));
        }
        return true;
    }

    // Everything about a crossing that is not fixed width.
    std::string packBlob(const Crossing& c)
    {
        std::string out;
        out.reserve(256);
        putText(out, c.pass.handle);
        putText(out, c.pass.greeting);
        putText(out, c.pass.activity);
        putText(out, c.pass.playing);
        putText(out, c.pass.district);
        putText(out, c.place);
        putList(out, c.pass.carrying);
        putList(out, c.pass.games);
        return out;
    }

    bool unpackBlob(const std::string& raw, Crossing& c)
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(raw.data());
        const uint8_t* end = p + raw.size();
        return takeText(p, end, c.pass.handle)
            && takeText(p, end, c.pass.greeting)
            && takeText(p, end, c.pass.activity)
            && takeText(p, end, c.pass.playing)
            && takeText(p, end, c.pass.district)
            && takeText(p, end, c.place)
            && takeList(p, end, c.pass.carrying)
            && takeList(p, end, c.pass.games);
    }

    // ---- hex ids, stored as the sixteen bytes they are

    bool idToBytes(const std::string& hex, uint8_t out[16])
    {
        if (hex.size() != 32)
            return false;
        return fromHex(hex, out, 16);
    }

    std::string idFromBytes(const uint8_t in[16]) { return toHex(in, 16); }

    // ---- files

    bool readWhole(const std::string& path, std::string& out)
    {
        return readWholeFile(path, out);
    }

    // Writes `bytes` at `offset`, growing the file if need be.
    bool writeAt(const std::string& path, uint64_t offset, const void* data, size_t bytes)
    {
        FILE* f = fopen(path.c_str(), "r+b");
        if (!f)
            f = fopen(path.c_str(), "w+b");
        if (!f)
            return false;

        bool ok = fseek(f, long(offset), SEEK_SET) == 0
            && fwrite(data, 1, bytes, f) == bytes;
        fclose(f);
        return ok;
    }

    bool appendTo(const std::string& path, const void* data, size_t bytes, uint64_t& offsetOut)
    {
        FILE* f = fopen(path.c_str(), "a+b");
        if (!f)
            return false;
        fseek(f, 0, SEEK_END);
        long where = ftell(f);
        bool ok = where >= 0 && fwrite(data, 1, bytes, f) == bytes;
        fclose(f);
        if (ok)
            offsetOut = uint64_t(where);
        return ok;
    }
}

// ---------------------------------------------------------------- reading

bool CrossingFile::absent()
{
    return !fileExists(dataPath(kIndexFile));
}

std::vector<Crossing> CrossingFile::load()
{
    std::vector<Crossing> out;
    m_blobBytes = 0;
    m_wasteBytes = 0;
    m_records = 0;

    std::string index;
    if (!readWhole(dataPath(kIndexFile), index) || index.size() < kHeaderSize)
        return out;

    const uint8_t* p = reinterpret_cast<const uint8_t*>(index.data());
    if (memcmp(p, kMagic, 4) != 0) {
        LOG("crossings: index is not ours; ignoring it");
        return out;
    }
    p += 4;
    uint16_t version = get16(p);
    uint16_t recordSize = get16(p);
    uint32_t records = get32(p);
    p += 4; // reserved

    if (version != kVersion || recordSize != kRecordSize) {
        LOG("crossings: index is version %u/%u, not %u/%u; ignoring it",
            version, recordSize, kVersion, kRecordSize);
        return out;
    }

    size_t have = (index.size() - kHeaderSize) / kRecordSize;
    if (records > have) {
        // Truncated: trust what is actually there. A half-written record at the
        // end is one card, not a broken file.
        LOG("crossings: index says %u records, file holds %zu", records, have);
        records = uint32_t(have);
    }
    m_records = records;

    std::string blob;
    readWhole(dataPath(kBlobFile), blob);
    m_blobBytes = blob.size();

    uint64_t used = 0;
    for (uint32_t i = 0; i < records; i++) {
        const uint8_t* rec = reinterpret_cast<const uint8_t*>(index.data())
            + kHeaderSize + size_t(i) * kRecordSize;
        const uint8_t* q = rec;

        uint8_t flags = get8(q);
        uint8_t theme = get8(q);
        q += 2; // reserved

        uint8_t idBytes[16];
        memcpy(idBytes, q, 16);
        q += 16;

        uint8_t miiBytes = get8(q);
        std::string miiHex;
        if (miiBytes > 0 && miiBytes <= kMiiMaxBytes)
            miiHex = toHex(q, miiBytes);
        q += kMiiMaxBytes; // fixed width whatever the length says

        uint32_t portrait = get32(q);
        uint64_t firstSeen = get64(q);
        uint64_t lastSeen = get64(q);
        uint32_t count = get32(q);
        uint32_t blobOffset = get32(q);
        uint32_t blobLength = get32(q);

        if ((flags & kLive) == 0) {
            m_wasteBytes += blobLength;
            continue;
        }
        if (blobLength > kMaxBlobBlock
            || uint64_t(blobOffset) + blobLength > blob.size()) {
            LOG("crossings: record %u points outside the blob; dropped", i);
            m_wasteBytes += blobLength;
            continue;
        }

        Crossing c;
        c.id = idFromBytes(idBytes);
        c.pass.mii = miiHex;
        c.pass.portrait = portrait;
        c.pass.theme = theme;
        c.firstSeen = firstSeen;
        c.lastSeen = lastSeen;
        c.count = count;
        c.opened = (flags & kOpened) != 0;
        c.tradedBack = (flags & kTradedBack) != 0;
        c.slot = i;

        if (!unpackBlob(blob.substr(blobOffset, blobLength), c)) {
            LOG("crossings: record %u has unreadable text; dropped", i);
            m_wasteBytes += blobLength;
            continue;
        }

        c.pass.sanitize();
        if (c.id.empty())
            continue;

        used += blobLength;
        out.push_back(std::move(c));
    }

    m_wasteBytes = m_blobBytes > used ? m_blobBytes - used : 0;
    LOG("crossings: %zu of %u records, %llu KB blob (%llu KB waste)", out.size(), records,
        (unsigned long long) (m_blobBytes / 1024), (unsigned long long) (m_wasteBytes / 1024));
    return out;
}

// ---------------------------------------------------------------- writing

namespace {
    void packRecord(uint8_t rec[kRecordSize], const Crossing& c,
        uint32_t blobOffset, uint32_t blobLength)
    {
        memset(rec, 0, kRecordSize);
        uint8_t* p = rec;

        uint8_t flags = kLive;
        if (c.opened)
            flags |= kOpened;
        if (c.tradedBack)
            flags |= kTradedBack;

        put8(p, flags);
        put8(p, uint8_t(c.pass.theme));
        put16(p, 0);

        uint8_t idBytes[16] {};
        idToBytes(c.id, idBytes);
        memcpy(p, idBytes, 16);
        p += 16;

        // Only well-formed hex is stored; anything else becomes no face, which
        // is what the receiving console would make of it anyway.
        uint8_t miiBytes = 0;
        if (!c.pass.mii.empty() && c.pass.mii.size() % 2 == 0
            && c.pass.mii.size() / 2 <= kMiiMaxBytes) {
            uint8_t decoded[kMiiMaxBytes];
            if (fromHex(c.pass.mii, decoded, c.pass.mii.size() / 2)) {
                miiBytes = uint8_t(c.pass.mii.size() / 2);
                memcpy(p + 1, decoded, miiBytes);
            }
        }
        put8(p, miiBytes);
        p += kMiiMaxBytes;

        put32(p, c.pass.portrait);
        put64(p, c.firstSeen);
        put64(p, c.lastSeen);
        put32(p, c.count);
        put32(p, blobOffset);
        put32(p, blobLength);
    }

    bool writeHeader(const std::string& path, uint32_t records)
    {
        uint8_t head[kHeaderSize] {};
        uint8_t* p = head;
        memcpy(p, kMagic, 4);
        p += 4;
        put16(p, kVersion);
        put16(p, kRecordSize);
        put32(p, records);
        put32(p, 0);
        return writeAt(path, 0, head, kHeaderSize);
    }
}

bool CrossingFile::append(Crossing& crossing)
{
    std::string blob = packBlob(crossing);
    uint64_t offset = 0;
    if (!appendTo(dataPath(kBlobFile), blob.data(), blob.size(), offset))
        return false;
    m_blobBytes = offset + blob.size();

    crossing.slot = m_records;

    uint8_t rec[kRecordSize];
    packRecord(rec, crossing, uint32_t(offset), uint32_t(blob.size()));
    if (!writeAt(dataPath(kIndexFile), kHeaderSize + uint64_t(crossing.slot) * kRecordSize,
            rec, kRecordSize))
        return false;

    m_records++;
    return writeHeader(dataPath(kIndexFile), m_records);
}

bool CrossingFile::writeOne(const Crossing& crossing, bool textChanged)
{
    if (crossing.slot == Crossing::kNoSlot || crossing.slot >= m_records)
        return false;

    uint64_t recordAt = kHeaderSize + uint64_t(crossing.slot) * kRecordSize;

    // Read what the record points at now. Needed either way: to keep it when
    // only a flag changed, and to count it as waste when it is superseded.
    std::string index;
    if (!readWhole(dataPath(kIndexFile), index)
        || index.size() < recordAt + kRecordSize)
        return false;

    const uint8_t* q = reinterpret_cast<const uint8_t*>(index.data())
        + recordAt + kOffBlobOffset;
    uint32_t blobOffset = get32(q);
    uint32_t blobLength = get32(q);

    if (textChanged) {
        // Appended rather than written over: rewriting in place would only work
        // while the text never grew. The old block becomes waste, which is what
        // compaction is for.
        std::string block = packBlob(crossing);
        uint64_t offset = 0;
        if (!appendTo(dataPath(kBlobFile), block.data(), block.size(), offset))
            return false;

        m_wasteBytes += blobLength;
        blobOffset = uint32_t(offset);
        blobLength = uint32_t(block.size());
        m_blobBytes = offset + block.size();
    }

    uint8_t rec[kRecordSize];
    packRecord(rec, crossing, blobOffset, blobLength);
    return writeAt(dataPath(kIndexFile), recordAt, rec, kRecordSize);
}

bool CrossingFile::erase(const Crossing& crossing)
{
    if (crossing.slot == Crossing::kNoSlot || crossing.slot >= m_records)
        return false;

    // Only the flag byte has to change; the rest of the record stays as a
    // record of what was there, and compaction removes it.
    uint64_t recordAt = kHeaderSize + uint64_t(crossing.slot) * kRecordSize;

    std::string index;
    if (readWhole(dataPath(kIndexFile), index) && index.size() >= recordAt + kRecordSize) {
        const uint8_t* q = reinterpret_cast<const uint8_t*>(index.data())
            + recordAt + kOffBlobLength;
        m_wasteBytes += get32(q);
    }

    uint8_t dead = 0;
    return writeAt(dataPath(kIndexFile), recordAt, &dead, 1);
}

bool CrossingFile::compact(std::vector<Crossing>& live)
{
    std::string blob;
    std::string index;
    index.resize(kHeaderSize);

    uint8_t* head = reinterpret_cast<uint8_t*>(&index[0]);
    memcpy(head, kMagic, 4);
    uint8_t* p = head + 4;
    put16(p, kVersion);
    put16(p, kRecordSize);
    put32(p, uint32_t(live.size()));
    put32(p, 0);

    for (size_t i = 0; i < live.size(); i++) {
        std::string block = packBlob(live[i]);
        uint32_t offset = uint32_t(blob.size());
        blob += block;

        live[i].slot = uint32_t(i);
        uint8_t rec[kRecordSize];
        packRecord(rec, live[i], offset, uint32_t(block.size()));
        index.append(reinterpret_cast<const char*>(rec), kRecordSize);
    }

    // Whole files, so this is the one path that can be atomic.
    if (!writeWholeFileAtomic(dataPath(kBlobFile), blob))
        return false;
    if (!writeWholeFileAtomic(dataPath(kIndexFile), index))
        return false;

    m_records = uint32_t(live.size());
    m_blobBytes = blob.size();
    m_wasteBytes = 0;
    return true;
}

} // namespace nxp
