#pragma once

#include "gfx/gpu.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nxp {

// The puzzle artwork, one picture at a time.
//
// Image memory is a bump allocator with no free (mempool.cpp), and the glyph
// atlas already holds 9 of its 12 MB. Keeping every puzzle's picture resident
// would cap how many puzzles can ever exist - three at BC1 is 1.3 MB, thirty is
// 13 and does not fit at all.
//
// It never needs to. One puzzle is open at a time, so this reserves a single
// slot the size of the largest picture in the file and re-uploads into it when
// a different one is asked for. The image and its descriptor are created once
// and never change; only the pixels do. Adding puzzles then costs file size and
// nothing else.
class PictureStore {
public:
    // Where the artwork lives on the card, relative to the data directory.
    //
    // Outside romfs on purpose: the pack is delivered beside the NRO by the
    // updater rather than baked into it, so the two are replaced together and
    // neither has to carry the other. Named here because the loader and the
    // installer both need it and must not drift.
    static const char* relativePath();

    // Reads the header and index of a pack without touching the GPU, so the
    // installer can refuse a truncated download before it replaces a pack that
    // works. Returns how many pictures are in it, or -1 when it is not one.
    static int validate(const std::string& path);

    // Reads the index and reserves the slot. A missing file is not a failure:
    // it means this build ships no artwork, and the puzzle screen falls back to
    // numbered tiles.
    bool load(const std::string& path, Gpu& gpu);

    bool available() const { return m_ready; }
    bool has(const std::string& key) const { return find(key) >= 0; }

    // Asks for a picture. Cheap and idempotent when it is already the resident
    // one. Needs a command buffer for the frame, so call it from drawing.
    void request(const std::string& key);

    // True when `key` is the picture currently in the slot, i.e. when drawing
    // with it will show the right thing.
    bool resident(const std::string& key) const
    {
        return m_ready && !m_resident.empty() && m_resident == key;
    }

    void beginFrame(dk::CmdBuf cmd) { m_cmd = cmd; }

    // The upload has to be visible to the texture units before anything samples
    // it, exactly as the glyph atlas does.
    bool uploaded() const { return m_uploaded; }
    void clearUploaded() { m_uploaded = false; }

    const DkImageDescriptor& descriptor() const { return m_descriptor; }

private:
    struct Entry {
        std::string key;
        uint16_t width = 0;
        uint16_t height = 0;
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    int find(const std::string& key) const;

    std::string m_path;
    std::vector<Entry> m_entries;

    dk::Image m_image;
    dk::ImageDescriptor m_descriptor;
    MemPool::Slice m_staging;
    dk::CmdBuf m_cmd = nullptr;

    std::string m_resident;   // the key currently in the slot
    uint16_t m_slotWidth = 0; // what the slot was built for
    uint16_t m_slotHeight = 0;
    bool m_ready = false;
    bool m_uploaded = false;
};

// The one the app uses.
PictureStore& pictures();

} // namespace nxp
