#include "core/mii_parts.h"

#include "core/log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nxp {

namespace {

constexpr uint8_t kMagic[4] = { 'M', 'I', 'I', 'P' };
constexpr uint16_t kVersion = 2;

#pragma pack(push, 1)
struct FileHeader {
    uint8_t magic[4];
    uint16_t version;
    uint16_t partCount;
    uint32_t tableBytes;
};
struct TableEntry {
    uint8_t category;
    uint8_t index;
    uint16_t pad;
    uint32_t offset;
    uint16_t quadCount;
    uint16_t discCount;
    uint16_t backQuads;
    uint16_t backDiscs;
    int16_t ax0, ay0, ax1, ay1;
    int16_t x0, y0, x1, y1;
};
#pragma pack(pop)

static_assert(sizeof(TableEntry) == 32, "part table entry must match the baker");
static_assert(sizeof(MiiParts::Quad) == 18, "quad record must match the baker");
static_assert(sizeof(MiiParts::Disc) == 8, "disc record must match the baker");

} // namespace

bool MiiParts::load(const char* path)
{
    exit();

    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        LOG("mii parts: cannot open %s", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < static_cast<long>(sizeof(FileHeader))) {
        std::fclose(f);
        LOG("mii parts: %s is too small to be a part file", path);
        return false;
    }
    m_blob.resize(static_cast<size_t>(size));
    size_t got = std::fread(m_blob.data(), 1, m_blob.size(), f);
    std::fclose(f);
    if (got != m_blob.size()) {
        LOG("mii parts: short read on %s", path);
        m_blob.clear();
        return false;
    }

    FileHeader header {};
    std::memcpy(&header, m_blob.data(), sizeof(header));
    if (std::memcmp(header.magic, kMagic, 4) != 0 || header.version != kVersion) {
        LOG("mii parts: %s is not a v%u part file", path, kVersion);
        m_blob.clear();
        return false;
    }

    size_t tableAt = sizeof(FileHeader);
    size_t blobAt = tableAt + header.tableBytes;
    if (blobAt > m_blob.size()
        || header.tableBytes != header.partCount * sizeof(TableEntry)) {
        LOG("mii parts: %s has a malformed table", path);
        m_blob.clear();
        return false;
    }

    const uint8_t* data = m_blob.data() + blobAt;
    size_t dataBytes = m_blob.size() - blobAt;

    for (uint16_t i = 0; i < header.partCount; i++) {
        TableEntry e {};
        std::memcpy(&e, m_blob.data() + tableAt + i * sizeof(TableEntry), sizeof(e));
        if (e.category >= CategoryCount)
            continue;

        size_t need = static_cast<size_t>(e.quadCount) * sizeof(Quad)
            + static_cast<size_t>(e.discCount) * sizeof(Disc);
        if (e.offset > dataBytes || need > dataBytes - e.offset) {
            LOG("mii parts: part %u runs past the end of the file", i);
            m_blob.clear();
            return false;
        }

        Part p {};
        p.quads = reinterpret_cast<const Quad*>(data + e.offset);
        p.discs = reinterpret_cast<const Disc*>(
            data + e.offset + static_cast<size_t>(e.quadCount) * sizeof(Quad));
        p.quadCount = e.quadCount;
        p.discCount = e.discCount;
        p.backQuads = e.backQuads;
        p.backDiscs = e.backDiscs;
        p.ax0 = e.ax0 / Unit;
        p.ay0 = e.ay0 / Unit;
        p.ax1 = e.ax1 / Unit;
        p.ay1 = e.ay1 / Unit;
        p.x0 = e.x0 / Unit;
        p.y0 = e.y0 / Unit;
        p.x1 = e.x1 / Unit;
        p.y1 = e.y1 / Unit;

        auto& list = m_parts[e.category];
        if (e.index >= list.size())
            list.resize(e.index + 1u);
        list[e.index] = p;
    }

    if (m_parts[Face].empty()) {
        LOG("mii parts: %s has no face shapes", path);
        m_blob.clear();
        return false;
    }

    // Median extents, used to place every part.
    for (int c = 0; c < CategoryCount; c++) {
        std::vector<float> widths;
        widths.reserve(m_parts[c].size());
        for (const Part& p : m_parts[c])
            if (p.quadCount || p.discCount)
                widths.push_back(p.width());
        if (widths.empty()) {
            m_medianWidth[c] = 1.0f;
            continue;
        }
        std::sort(widths.begin(), widths.end());
        m_medianWidth[c] = std::max(widths[widths.size() / 2], 0.001f);
    }

    std::vector<float> heights;
    for (const Part& p : m_parts[Face])
        if (p.quadCount)
            heights.push_back(p.height());
    std::sort(heights.begin(), heights.end());
    m_faceWidth = m_medianWidth[Face];
    m_faceHeight = heights.empty() ? 32.0f : std::max(heights[heights.size() / 2], 0.001f);

    m_ready = true;
    LOG("mii parts: %u parts, %zu KB (%d faces, %d hair, %d eyes, %d mouths)",
        header.partCount, m_blob.size() / 1024, count(Face), count(Hair), count(Eyes),
        count(Mouth));
    return true;
}

void MiiParts::exit()
{
    m_blob.clear();
    m_blob.shrink_to_fit();
    for (auto& list : m_parts)
        list.clear();
    m_ready = false;
}

const MiiParts::Part* MiiParts::part(Category c, int index) const
{
    if (c >= CategoryCount)
        return nullptr;
    const auto& list = m_parts[c];
    if (list.empty())
        return nullptr;
    int n = static_cast<int>(list.size());
    // The index wraps, so it always names a real slot. There is deliberately no
    // fallback for an *empty* slot: several categories keep their "none" option
    // as an empty part - bald hair, no glasses, no beard - and substituting
    // the first part for those made every one of them unreachable. Bald came out
    // wearing hairstyle 0.
    return &list[static_cast<size_t>(((index % n) + n) % n)];
}

MiiParts& miiParts()
{
    static MiiParts instance;
    return instance;
}

} // namespace nxp
