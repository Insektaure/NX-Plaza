#include "core/crossing_extras.h"

#include "core/log.h"
#include "core/util.h"

#include <cstdio>
#include <cstring>

namespace nxp {

namespace {
    const char* kFile = "crossings.ext";

    constexpr char kMagic[4] = { 'N', 'X', 'P', 'X' };
    // 1 was a whole-file rewrite: rows of packed fields, and every change
    // rewrote the lot.
    // 2 is the same fields as an append-only log. A v1 file is
    // still read, and the first flush leaves a v2 one behind.
    constexpr uint16_t kVersion = 2;
    constexpr uint16_t kVersionRewrite = 1;
    constexpr size_t kHeaderSize = 16;

    // What an entry does.
    constexpr uint8_t kSet = 0;
    constexpr uint8_t kClear = 1;   // this tag is gone
    constexpr uint8_t kDropRow = 2; // every tag for this id is gone

    constexpr size_t kEntryHeader = 16 + 2 + 2 + 1 + 1; // id, tag, len, kind, pad

    // Guards against a corrupt or hostile file, not real limits.
    constexpr size_t kMaxRows = 20000;
    constexpr size_t kMaxFieldValue = 4 * 1024;
    constexpr size_t kMaxEntries = 400000;

    void put16(std::string& out, uint16_t v)
    {
        out.push_back(char(v & 0xFF));
        out.push_back(char(v >> 8));
    }
    void put32(std::string& out, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            out.push_back(char(v >> (i * 8)));
    }

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

    void putHeader(std::string& out)
    {
        out.append(kMagic, 4);
        put16(out, kVersion);
        put16(out, 0);
        put32(out, 0);
        put32(out, 0);
    }

    // Appends to the end of a file, creating it if need be.
    bool appendTo(const std::string& path, const std::string& bytes)
    {
        if (bytes.empty())
            return true;
        FILE* f = fopen(path.c_str(), "a+b");
        if (!f)
            return false;
        fseek(f, 0, SEEK_END);
        bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        if (fflush(f) != 0)
            ok = false;
        if (fclose(f) != 0)
            ok = false;
        return ok;
    }
}

// ------------------------------------------------------------------ one row

const CrossingExtras::Field* CrossingExtras::find(uint16_t tag) const
{
    for (const Field& f : m_fields) {
        if (f.tag == tag)
            return &f;
    }
    return nullptr;
}

CrossingExtras::Field& CrossingExtras::put(uint16_t tag)
{
    for (Field& f : m_fields) {
        if (f.tag == tag)
            return f;
    }
    m_fields.push_back(Field { tag, {} });
    return m_fields.back();
}

void CrossingExtras::erase(uint16_t tag)
{
    for (size_t i = 0; i < m_fields.size(); i++) {
        if (m_fields[i].tag == tag) {
            m_fields.erase(m_fields.begin() + ptrdiff_t(i));
            return;
        }
    }
}

bool CrossingExtras::has(uint16_t tag) const { return find(tag) != nullptr; }

bool CrossingExtras::getU32(uint16_t tag, uint32_t& out) const
{
    const Field* f = find(tag);
    if (!f || f->value.size() != 4)
        return false;
    const uint8_t* p = f->value.data();
    out = get32(p);
    return true;
}

bool CrossingExtras::getU64(uint16_t tag, uint64_t& out) const
{
    const Field* f = find(tag);
    if (!f || f->value.size() != 8)
        return false;
    out = 0;
    for (int i = 0; i < 8; i++)
        out |= uint64_t(f->value[size_t(i)]) << (i * 8);
    return true;
}

bool CrossingExtras::getText(uint16_t tag, std::string& out) const
{
    const Field* f = find(tag);
    if (!f)
        return false;
    // An empty value is legal, and an empty vector's data() may be null:
    // assign(nullptr, 0) is undefined however harmless it looks.
    out.clear();
    if (!f->value.empty())
        out.assign(reinterpret_cast<const char*>(f->value.data()), f->value.size());
    return true;
}

bool CrossingExtras::getBytes(uint16_t tag, std::vector<uint8_t>& out) const
{
    const Field* f = find(tag);
    if (!f)
        return false;
    out = f->value;
    return true;
}

std::vector<uint16_t> CrossingExtras::tags() const
{
    std::vector<uint16_t> out;
    out.reserve(m_fields.size());
    for (const Field& f : m_fields)
        out.push_back(f.tag);
    return out;
}

// ------------------------------------------------------------------- writing

bool CrossingExtraFile::validId(const std::string& id)
{
    if (id.size() != 32)
        return false;
    for (char c : id) {
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex)
            return false;
    }
    return true;
}

void CrossingExtraFile::queue(const std::string& id, uint16_t tag, uint8_t kind,
    const void* data, size_t bytes)
{
    if (!validId(id)) {
        LOG("extras: '%s' is not a crossing id", id.c_str());
        return;
    }
    bytes = std::min(bytes, kMaxFieldValue);

    uint8_t idBytes[16] {};
    if (!fromHex(id, idBytes, 16))
        return;

    m_pending.append(reinterpret_cast<const char*>(idBytes), 16);
    put16(m_pending, tag);
    put16(m_pending, uint16_t(bytes));
    m_pending.push_back(char(kind));
    m_pending.push_back(0);
    if (data != nullptr && bytes > 0)
        m_pending.append(static_cast<const char*>(data), bytes);
}

void CrossingExtraFile::setBytes(const std::string& id, uint16_t tag, const void* data,
    size_t bytes)
{
    if (!validId(id)) {
        LOG("extras: '%s' is not a crossing id", id.c_str());
        return;
    }
    bytes = std::min(bytes, kMaxFieldValue);

    CrossingExtras& row = m_rows[id];
    // Asked before put(), which creates the field: whether this supersedes
    // something is about the tag being present, not about its value being
    // non-empty. Testing the value counted a re-set of an empty field as a
    // brand new one, which inflates the live count - and the live count is what
    // decides when the log gets compacted, so the file would have grown longer
    // and longer before being tidied.
    bool replacing = row.has(tag);
    CrossingExtras::Field& f = row.put(tag);
    f.value.clear();
    if (data != nullptr && bytes > 0) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        f.value.assign(p, p + bytes);
    }
    if (replacing)
        m_deadEntries++; // the entry this supersedes is now dead weight
    else
        m_liveFields++;

    queue(id, tag, kSet, data, bytes);
}

void CrossingExtraFile::setU32(const std::string& id, uint16_t tag, uint32_t value)
{
    uint8_t b[4];
    for (int i = 0; i < 4; i++)
        b[i] = uint8_t(value >> (i * 8));
    setBytes(id, tag, b, sizeof(b));
}

void CrossingExtraFile::setU64(const std::string& id, uint16_t tag, uint64_t value)
{
    uint8_t b[8];
    for (int i = 0; i < 8; i++)
        b[i] = uint8_t(value >> (i * 8));
    setBytes(id, tag, b, sizeof(b));
}

void CrossingExtraFile::setText(const std::string& id, uint16_t tag, const std::string& value)
{
    setBytes(id, tag, value.data(), value.size());
}

void CrossingExtraFile::clearField(const std::string& id, uint16_t tag)
{
    auto it = m_rows.find(id);
    if (it == m_rows.end() || !it->second.has(tag))
        return;

    it->second.erase(tag);
    if (m_liveFields > 0)
        m_liveFields--;
    m_deadEntries++;
    if (it->second.empty())
        m_rows.erase(it);

    queue(id, tag, kClear, nullptr, 0);
}

void CrossingExtraFile::dropRow(const std::string& id)
{
    auto it = m_rows.find(id);
    if (it == m_rows.end())
        return;

    size_t fields = it->second.count();
    m_liveFields = m_liveFields > fields ? m_liveFields - fields : 0;
    m_deadEntries += fields;
    m_rows.erase(it);

    queue(id, 0, kDropRow, nullptr, 0);
}

size_t CrossingExtraFile::dropOrphans(const std::vector<std::string>& liveIds)
{
    if (m_rows.empty())
        return 0;

    std::map<std::string, bool> live;
    for (const std::string& id : liveIds)
        live.emplace(id, true);

    // Collected first: dropRow erases from the map being walked.
    std::vector<std::string> gone;
    for (const auto& entry : m_rows) {
        if (live.find(entry.first) == live.end())
            gone.push_back(entry.first);
    }
    for (const std::string& id : gone)
        dropRow(id);
    return gone.size();
}

// ------------------------------------------------------------------- reading

bool CrossingExtraFile::load()
{
    m_rows.clear();
    m_pending.clear();
    m_deadEntries = 0;
    m_liveFields = 0;
    m_unreadable = false;

    const std::string path = dataPath(kFile);
    std::string raw;
    if (!readWholeFile(path, raw)) {
        // Absent and unreadable are not the same thing, and treating them alike
        // is how a read error becomes data loss: with nothing in memory the
        // next compaction would write a fresh file over entries that were fine.
        if (fileExists(path)) {
            m_unreadable = true;
            LOG("extras: %s exists but could not be read; not writing to it this session",
                kFile);
            return false;
        }
        return true;
    }

    if (raw.size() < kHeaderSize) {
        LOG("extras: %s is too short to be a header", kFile);
        return false;
    }

    const uint8_t* p = reinterpret_cast<const uint8_t*>(raw.data());
    const uint8_t* end = p + raw.size();

    if (memcmp(p, kMagic, 4) != 0) {
        LOG("extras: %s is not an extras file", kFile);
        return false;
    }
    p += 4;
    uint16_t version = get16(p);
    p += 2;
    uint32_t claimed = get32(p);
    p += 4;

    if (version == kVersionRewrite) {
        // The old whole-file shape: rows of id, length, packed fields. Read it
        // once so nobody loses what they had, and let the next flush leave a
        // log behind instead.
        LOG("extras: %s is the old rewritten form; converting to a log", kFile);
        size_t rows = 0;
        while (p < end && rows < kMaxRows) {
            if (end - p < 18)
                break;
            char idBytes[16];
            memcpy(idBytes, p, 16);
            p += 16;
            uint16_t payload = get16(p);
            if (size_t(end - p) < payload)
                break;

            std::string id = toHex(idBytes, 16);
            const uint8_t* q = p;
            const uint8_t* rowEnd = p + payload;
            while (rowEnd - q >= 4) {
                uint16_t tag = get16(q);
                uint16_t len = get16(q);
                if (len > kMaxFieldValue || size_t(rowEnd - q) < len)
                    break;
                if (validId(id))
                    setBytes(id, tag, len > 0 ? q : nullptr, len);
                q += len;
            }
            p = rowEnd;
            rows++;
        }
        // Everything read is queued as pending, so the next flush writes the
        // whole thing back out as a log. Nothing is dead yet.
        m_deadEntries = 0;
        LOG("extras: converted %zu rows, %zu fields", m_rows.size(), m_liveFields);
        return true;
    }

    if (version != kVersion)
        LOG("extras: %s is version %u, reading it as v%u", kFile, version, kVersion);

    size_t entries = 0;
    while (p < end && entries < kMaxEntries) {
        if (size_t(end - p) < kEntryHeader)
            break;

        char idBytes[16];
        memcpy(idBytes, p, 16);
        p += 16;
        uint16_t tag = get16(p);
        uint16_t len = get16(p);
        uint8_t kind = *p++;
        p++; // pad

        if (len > kMaxFieldValue || size_t(end - p) < len) {
            LOG("extras: entry %zu claims %u bytes it does not have; stopping", entries, len);
            break;
        }

        std::string id = toHex(idBytes, 16);
        entries++;

        // Replayed in order, so a later entry wins. Everything it supersedes is
        // dead weight in the file, which is what compaction later reclaims.
        if (kind == kDropRow) {
            auto it = m_rows.find(id);
            if (it != m_rows.end()) {
                size_t fields = it->second.count();
                m_liveFields = m_liveFields > fields ? m_liveFields - fields : 0;
                m_deadEntries += fields;
                m_rows.erase(it);
            }
            m_deadEntries++;
        } else if (kind == kClear) {
            auto it = m_rows.find(id);
            if (it != m_rows.end() && it->second.has(tag)) {
                it->second.erase(tag);
                if (m_liveFields > 0)
                    m_liveFields--;
                m_deadEntries++;
                if (it->second.empty())
                    m_rows.erase(it);
            }
            m_deadEntries++;
        } else {
            CrossingExtras& row = m_rows[id];
            bool replacing = row.has(tag);
            CrossingExtras::Field& f = row.put(tag);
            if (replacing)
                m_deadEntries++;
            else
                m_liveFields++;
            f.value.clear();
            if (len > 0)
                f.value.assign(p, p + len);
        }

        p += len;
    }

    if (claimed != 0 && claimed != entries)
        LOG("extras: header hints %u entries, replayed %zu", claimed, entries);

    LOG("extras: %zu rows, %zu fields from %zu entries (%zu dead)", m_rows.size(),
        m_liveFields, entries, m_deadEntries);
    return true;
}

// ------------------------------------------------------------------ flushing

bool CrossingExtraFile::compact()
{
    // The only path that rewrites the file, and the only one that is atomic.
    // Everything live is written as a fresh set of entries - tags this build
    // does not understand included, because they were read into the rows the
    // same as any other.
    std::string out;
    putHeader(out);

    size_t written = 0;
    for (const auto& entry : m_rows) {
        if (!validId(entry.first) || entry.second.empty())
            continue;
        uint8_t idBytes[16] {};
        if (!fromHex(entry.first, idBytes, 16))
            continue;

        for (const CrossingExtras::Field& f : entry.second.m_fields) {
            size_t n = std::min(f.value.size(), kMaxFieldValue);
            out.append(reinterpret_cast<const char*>(idBytes), 16);
            put16(out, f.tag);
            put16(out, uint16_t(n));
            out.push_back(char(kSet));
            out.push_back(0);
            if (n > 0)
                out.append(reinterpret_cast<const char*>(f.value.data()), n);
            written++;
        }
    }

    if (!writeWholeFileAtomic(dataPath(kFile), out)) {
        LOG("extras: could not compact %s", kFile);
        return false;
    }

    LOG("extras: compacted to %zu entries, %zu bytes", written, out.size());
    m_pending.clear();
    m_deadEntries = 0;
    m_liveFields = written;
    return true;
}

bool CrossingExtraFile::flush()
{
    if (m_unreadable) {
        if (!m_pending.empty())
            LOG("extras: refusing to write to a file that would not read");
        m_pending.clear();
        return false;
    }
    if (m_pending.empty() && !needsCompaction())
        return true;

    if (needsCompaction())
        return compact();

    const std::string path = dataPath(kFile);

    // A file that does not exist yet needs its header before its first entry.
    if (!fileExists(path)) {
        std::string head;
        putHeader(head);
        if (!appendTo(path, head)) {
            LOG("extras: could not create %s", kFile);
            return false;
        }
    }

    if (!appendTo(path, m_pending)) {
        LOG("extras: could not append to %s", kFile);
        return false; // left dirty on purpose, so the next flush tries again
    }
    m_pending.clear();
    return true;
}

const CrossingExtras* CrossingExtraFile::find(const std::string& id) const
{
    auto it = m_rows.find(id);
    return it == m_rows.end() ? nullptr : &it->second;
}

} // namespace nxp
