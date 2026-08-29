#pragma once

#include "gfx/gpu.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace nxp {

enum class FontWeight : uint8_t {
    Regular = 0,
    Medium = 1, // lightly emboldened
    Bold = 2,
};

// One cached glyph in the atlas.
struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // atlas texture coordinates
    float left = 0, top = 0;              // offset from the pen, top-left origin
    float width = 0, height = 0;          // size in pixels
    float advance = 0;
    uint32_t index = 0; // FreeType glyph index, for kerning
    int face = -1;      // which fallback face it came from
    bool blank = false; // spaces and friends: advance only, nothing to draw
};

struct FontMetrics {
    float ascender = 0;
    float descender = 0;
    float lineHeight = 0;
};

// Glyph cache backed by the console's own shared fonts.
//
// The Switch ships the fonts every game uses in a shared memory block, so the
// homebrew does not have to embed a typeface (and inherits CJK coverage for
// free). FreeType rasterises on demand into one R8 atlas; new glyphs are
// copied into it inside the frame's command buffer, before any draw that
// samples them.
class Font {
public:
    bool init(Gpu& gpu);
    void exit();

    // Called once per frame before the UI is built.
    void beginFrame(dk::CmdBuf cmd, unsigned frameIndex);

    // Returns nullptr only when the codepoint is missing from every face and
    // there is no replacement to draw.
    const Glyph* glyph(uint32_t codepoint, int pixelSize, FontWeight weight);

    // Horizontal kerning between two glyphs of the same face, in pixels.
    float kerning(const Glyph& a, const Glyph& b, int pixelSize);

    FontMetrics metrics(int pixelSize);

    const DkImageDescriptor& descriptor() const { return m_descriptor; }

    // True when glyphs were copied into the atlas during this frame, which
    // means the texture cache has to be invalidated before anything samples it.
    bool atlasDirty() const { return m_atlasDirty; }

    // Diagnostics for the Settings > About panel.
    size_t cachedGlyphs() const { return m_cache.size(); }
    float atlasFill() const;

private:
    // 3072 squared, not 2048: the design's ramp runs from 18px to 88px, and
    // three weights of it in both dock modes needs about 5 M pixels. A 2048
    // atlas holds 4.19 M, and running out means text quietly stops appearing.
    static constexpr uint32_t kAtlasSize = 3072;
    static constexpr uint32_t kMaxFaces = 5;
    static constexpr uint32_t kStagingPerFrame = 512 * 1024;

    struct Face {
        FT_Face face = nullptr;
        int loadedSize = -1;
    };

    bool setSize(int faceIndex, int pixelSize);
    int faceForCodepoint(uint32_t codepoint, uint32_t& glyphIndexOut);
    bool rasterize(uint32_t codepoint, int pixelSize, FontWeight weight, Glyph& out);
    bool packRect(uint32_t w, uint32_t h, uint32_t& x, uint32_t& y);

    Gpu* m_gpu = nullptr;
    FT_Library m_library = nullptr;
    std::vector<Face> m_faces;

    dk::Image m_image;
    dk::ImageDescriptor m_descriptor;

    MemPool::Slice m_staging;
    uint32_t m_stagingUsed = 0;
    unsigned m_frameIndex = 0;
    dk::CmdBuf m_cmd = nullptr;

    uint32_t m_penX = 1, m_penY = 1, m_rowHeight = 0;
    bool m_atlasFull = false;
    bool m_atlasDirty = false;

    std::unordered_map<uint64_t, Glyph> m_cache;
    std::unordered_map<uint32_t, FontMetrics> m_metrics;
};

} // namespace nxp
