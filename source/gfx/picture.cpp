#include "gfx/picture.h"

#include "core/log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nxp {

namespace {
    // Written by tools/bake_puzzles.py. Header then a flat index, so the
    // console reads 156 bytes at startup and one picture's worth when a puzzle
    // is opened, instead of the whole file either time.
    constexpr char kMagic[4] = { 'N', 'X', 'P', 'I' };
    constexpr uint16_t kVersion = 1;
    constexpr uint32_t kKeyLen = 32;
    constexpr uint32_t kHeaderSize = 12;
    constexpr uint32_t kEntrySize = 48;
    constexpr uint8_t kFormatBc1 = 0;

    uint16_t readU16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
    uint32_t readU32(const uint8_t* p)
    {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16)
            | (uint32_t(p[3]) << 24);
    }

    PictureStore g_pictures;
}

PictureStore& pictures() { return g_pictures; }

const char* PictureStore::relativePath() { return "data/assets/pictures.bin"; }

int PictureStore::validate(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return -1;

    uint8_t head[kHeaderSize];
    bool ok = fread(head, 1, sizeof(head), f) == sizeof(head)
        && memcmp(head, kMagic, sizeof(kMagic)) == 0 && readU16(head + 4) == kVersion;
    if (!ok) {
        fclose(f);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    uint32_t count = readU16(head + 6);
    int usable = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t rec[kEntrySize];
        if (fseek(f, long(kHeaderSize + i * kEntrySize), SEEK_SET) != 0
            || fread(rec, 1, sizeof(rec), f) != sizeof(rec)) {
            fclose(f);
            return -1; // the index itself is short: the file is truncated
        }
        uint32_t offset = readU32(rec + kKeyLen + 8);
        uint32_t size = readU32(rec + kKeyLen + 12);
        // Every payload has to be inside the file. This is the check that
        // catches a download that stopped halfway, which otherwise looks
        // perfectly well formed right up until something reads past the end.
        if (size == 0 || uint64_t(offset) + size > uint64_t(total)) {
            fclose(f);
            return -1;
        }
        if (rec[kKeyLen + 4] == kFormatBc1)
            usable++;
    }
    fclose(f);
    return usable;
}

bool PictureStore::load(const std::string& path, Gpu& gpu)
{
    m_path = path;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        LOG("pictures: no %s; puzzles will show numbered tiles", path.c_str());
        return false;
    }

    uint8_t head[kHeaderSize];
    if (fread(head, 1, sizeof(head), f) != sizeof(head)
        || memcmp(head, kMagic, sizeof(kMagic)) != 0) {
        LOG("pictures: %s is not a picture file", path.c_str());
        fclose(f);
        return false;
    }
    uint16_t version = readU16(head + 4);
    if (version != kVersion) {
        // Refused rather than guessed at: a file from another version means
        // something here has moved, and drawing whatever bytes are at the
        // offsets would put garbage on the screen.
        LOG("pictures: %s is version %u, this build reads %u", path.c_str(),
            unsigned(version), unsigned(kVersion));
        fclose(f);
        return false;
    }

    uint32_t count = readU16(head + 6);
    uint32_t widest = 0;
    uint32_t tallest = 0;
    uint32_t biggest = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t rec[kEntrySize];
        if (fread(rec, 1, sizeof(rec), f) != sizeof(rec)) {
            LOG("pictures: %s ends inside its index", path.c_str());
            break;
        }
        if (rec[kKeyLen + 4] != kFormatBc1)
            continue; // a format this build cannot sample

        Entry e;
        char key[kKeyLen + 1] = {};
        memcpy(key, rec, kKeyLen);
        e.key = key;
        e.width = readU16(rec + kKeyLen);
        e.height = readU16(rec + kKeyLen + 2);
        e.offset = readU32(rec + kKeyLen + 8);
        e.size = readU32(rec + kKeyLen + 12);
        if (e.key.empty() || e.width == 0 || e.height == 0 || e.size == 0)
            continue;

        widest = std::max<uint32_t>(widest, e.width);
        tallest = std::max<uint32_t>(tallest, e.height);
        biggest = std::max(biggest, e.size);
        m_entries.push_back(std::move(e));
    }
    fclose(f);

    if (m_entries.empty()) {
        LOG("pictures: %s holds nothing this build can read", path.c_str());
        return false;
    }

    // One slot, big enough for the largest picture in the file. Every puzzle
    // after the first is free as far as memory is concerned.
    dk::ImageLayout layout;
    dk::ImageLayoutMaker { gpu.device() }
        .setFlags(0)
        .setFormat(DkImageFormat_RGB_BC1)
        .setDimensions(widest, tallest)
        .initialize(layout);

    MemPool::Slice slice = gpu.persistentImagePool().allocate(
        static_cast<uint32_t>(layout.getSize()), layout.getAlignment());
    if (!slice) {
        LOG("pictures: no room for a %ux%u slot", unsigned(widest), unsigned(tallest));
        m_entries.clear();
        return false;
    }
    m_image.initialize(layout, slice.block, slice.offset);
    m_descriptor.initialize(m_image);

    m_staging = gpu.dataPool().allocate(biggest, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    if (!m_staging) {
        LOG("pictures: no room for %u bytes of staging", unsigned(biggest));
        m_entries.clear();
        return false;
    }

    m_slotWidth = uint16_t(widest);
    m_slotHeight = uint16_t(tallest);
    m_ready = true;
    LOG("pictures: %zu of them, one %ux%u slot (%u KB image, %u KB staging)",
        m_entries.size(), unsigned(widest), unsigned(tallest),
        unsigned(layout.getSize() / 1024), unsigned(biggest / 1024));
    return true;
}

int PictureStore::find(const std::string& key) const
{
    for (size_t i = 0; i < m_entries.size(); i++) {
        if (m_entries[i].key == key)
            return static_cast<int>(i);
    }
    return -1;
}

void PictureStore::request(const std::string& key)
{
    if (!m_ready || !m_cmd || key.empty() || m_resident == key)
        return;

    int index = find(key);
    if (index < 0)
        return;
    const Entry& e = m_entries[size_t(index)];
    if (e.size > m_staging.size || !m_staging.cpuAddr)
        return;

    FILE* f = fopen(m_path.c_str(), "rb");
    if (!f) {
        LOG("pictures: %s went away", m_path.c_str());
        return;
    }
    bool ok = fseek(f, long(e.offset), SEEK_SET) == 0
        && fread(m_staging.cpuAddr, 1, e.size, f) == e.size;
    fclose(f);
    if (!ok) {
        LOG("pictures: could not read '%s'", key.c_str());
        return;
    }

    // Straight over whatever was there. The copy is recorded on the same queue
    // as the drawing, so it is ordered behind the frames that sampled the old
    // picture - there is nothing to wait for and nothing to double buffer.
    dk::ImageView view { m_image };
    m_cmd.copyBufferToImage({ m_staging.gpuAddr, 0, 0 }, view,
        { 0, 0, 0, e.width, e.height, 1 });

    m_resident = key;
    m_uploaded = true;
}

} // namespace nxp
