#include "gfx/font.h"

#include "core/log.h"

#include FT_OUTLINE_H

#include <algorithm>
#include <cstring>

namespace nxp {

namespace {
    constexpr uint32_t kPadding = 1;

    uint64_t cacheKey(uint32_t codepoint, int pixelSize, FontWeight weight)
    {
        return static_cast<uint64_t>(codepoint)
            | (static_cast<uint64_t>(pixelSize & 0x3FF) << 32)
            | (static_cast<uint64_t>(weight) << 44);
    }

    // Fallback order. Standard already covers Latin, kana and most kanji; the
    // rest fill in the gaps for imported greetings and Nintendo's own glyphs.
    constexpr PlSharedFontType kFontOrder[] = {
        PlSharedFontType_Standard,
        PlSharedFontType_ChineseSimplified,
        PlSharedFontType_ChineseTraditional,
        PlSharedFontType_KO,
        PlSharedFontType_NintendoExt,
    };
}

bool Font::init(Gpu& gpu)
{
    m_gpu = &gpu;

    if (FT_Init_FreeType(&m_library) != 0) {
        LOG("font: FT_Init_FreeType failed");
        return false;
    }

    for (PlSharedFontType type : kFontOrder) {
        PlFontData data {};
        if (R_FAILED(plGetSharedFontByType(&data, type)))
            continue;

        Face f;
        if (FT_New_Memory_Face(m_library, static_cast<FT_Byte*>(data.address),
                static_cast<FT_Long>(data.size), 0, &f.face)
            != 0) {
            continue;
        }
        m_faces.push_back(f);
        if (m_faces.size() >= kMaxFaces)
            break;
    }

    if (m_faces.empty()) {
        LOG("font: no shared fonts available");
        return false;
    }

    // One R8 atlas, filled lazily.
    dk::ImageLayout layout;
    dk::ImageLayoutMaker { gpu.device() }
        .setFlags(0)
        .setFormat(DkImageFormat_R8_Unorm)
        .setDimensions(kAtlasSize, kAtlasSize)
        .initialize(layout);

    MemPool::Slice slice = gpu.persistentImagePool().allocate(
        static_cast<uint32_t>(layout.getSize()), layout.getAlignment());
    if (!slice) {
        LOG("font: no room for the glyph atlas");
        return false;
    }

    m_image.initialize(layout, slice.block, slice.offset);
    m_descriptor.initialize(m_image);

    m_staging = gpu.dataPool().allocate(kStagingPerFrame * Gpu::NumFrames,
        DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    if (!m_staging) {
        LOG("font: no room for glyph staging memory");
        return false;
    }

    LOG("font: %zu faces, %ux%u atlas", m_faces.size(), kAtlasSize, kAtlasSize);
    return true;
}

void Font::exit()
{
    for (Face& f : m_faces) {
        if (f.face)
            FT_Done_Face(f.face);
    }
    m_faces.clear();

    if (m_library) {
        FT_Done_FreeType(m_library);
        m_library = nullptr;
    }
    m_cache.clear();
    m_metrics.clear();
}

void Font::beginFrame(dk::CmdBuf cmd, unsigned frameIndex)
{
    m_cmd = cmd;
    m_frameIndex = frameIndex;
    m_stagingUsed = 0;
    m_atlasDirty = false;
}

bool Font::setSize(int faceIndex, int pixelSize)
{
    Face& f = m_faces[faceIndex];
    if (f.loadedSize == pixelSize)
        return true;
    if (FT_Set_Pixel_Sizes(f.face, 0, static_cast<FT_UInt>(pixelSize)) != 0)
        return false;
    f.loadedSize = pixelSize;
    return true;
}

int Font::faceForCodepoint(uint32_t codepoint, uint32_t& glyphIndexOut)
{
    for (size_t i = 0; i < m_faces.size(); i++) {
        FT_UInt index = FT_Get_Char_Index(m_faces[i].face, codepoint);
        if (index != 0) {
            glyphIndexOut = index;
            return static_cast<int>(i);
        }
    }
    glyphIndexOut = 0;
    return -1;
}

bool Font::packRect(uint32_t w, uint32_t h, uint32_t& x, uint32_t& y)
{
    if (m_atlasFull || w > kAtlasSize || h > kAtlasSize)
        return false;

    if (m_penX + w + kPadding > kAtlasSize) {
        m_penX = kPadding;
        m_penY += m_rowHeight + kPadding;
        m_rowHeight = 0;
    }

    if (m_penY + h + kPadding > kAtlasSize) {
        m_atlasFull = true;
        LOG("font: glyph atlas is full after %zu glyphs", m_cache.size());
        return false;
    }

    x = m_penX;
    y = m_penY;
    m_penX += w + kPadding;
    m_rowHeight = std::max(m_rowHeight, h);
    return true;
}

bool Font::rasterize(uint32_t codepoint, int pixelSize, FontWeight weight, Glyph& out)
{
    // Rasterising means recording a copy into the frame's command buffer, so
    // a measurement taken outside a frame can look up metrics but not glyphs.
    if (!m_cmd)
        return false;

    uint32_t glyphIndex = 0;
    int faceIndex = faceForCodepoint(codepoint, glyphIndex);
    if (faceIndex < 0)
        return false;
    if (!setSize(faceIndex, pixelSize))
        return false;

    FT_Face face = m_faces[faceIndex].face;
    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) != 0)
        return false;

    // The console ships a single weight, so heavier text is emboldened at
    // rasterisation time rather than faked by overdrawing.
    if (weight != FontWeight::Regular && face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_Pos strength = (pixelSize * 64) / (weight == FontWeight::Bold ? 22 : 40);
        FT_Outline_Embolden(&face->glyph->outline, strength);
    }

    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
        return false;

    FT_GlyphSlot slot = face->glyph;
    out.index = glyphIndex;
    out.face = faceIndex;
    out.advance = static_cast<float>(slot->advance.x) / 64.0f;
    out.left = static_cast<float>(slot->bitmap_left);
    out.top = -static_cast<float>(slot->bitmap_top); // pen-relative, y down
    out.width = static_cast<float>(slot->bitmap.width);
    out.height = static_cast<float>(slot->bitmap.rows);

    if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) {
        out.blank = true;
        return true;
    }

    // Upload with a one pixel zeroed border so linear filtering at the glyph
    // edges cannot pick up a neighbour or uninitialised memory.
    uint32_t bw = slot->bitmap.width;
    uint32_t bh = slot->bitmap.rows;
    uint32_t tw = bw + 2;
    uint32_t th = bh + 2;

    // Check the upload budget before touching the packer, so a frame that runs
    // out of staging room simply retries the glyph next frame.
    uint32_t needed = tw * th;
    uint32_t base = (m_stagingUsed + DK_IMAGE_LINEAR_STRIDE_ALIGNMENT - 1)
        & ~(DK_IMAGE_LINEAR_STRIDE_ALIGNMENT - 1);
    if (base + needed > kStagingPerFrame)
        return false;

    uint32_t x, y;
    if (!packRect(tw, th, x, y))
        return false;

    m_stagingUsed = base + needed;

    uint32_t sliceBase = m_frameIndex * kStagingPerFrame + base;
    uint8_t* dst = static_cast<uint8_t*>(m_staging.cpuAddr) + sliceBase;
    memset(dst, 0, needed);
    for (uint32_t row = 0; row < bh; row++) {
        const uint8_t* src = slot->bitmap.buffer + static_cast<int>(row) * slot->bitmap.pitch;
        memcpy(dst + (row + 1) * tw + 1, src, bw);
    }

    dk::ImageView view { m_image };
    m_cmd.copyBufferToImage({ m_staging.gpuAddr + sliceBase, tw, th }, view,
        { x, y, 0, tw, th, 1 });
    m_atlasDirty = true;

    const float inv = 1.0f / static_cast<float>(kAtlasSize);
    out.u0 = static_cast<float>(x + 1) * inv;
    out.v0 = static_cast<float>(y + 1) * inv;
    out.u1 = static_cast<float>(x + 1 + bw) * inv;
    out.v1 = static_cast<float>(y + 1 + bh) * inv;
    out.blank = false;
    return true;
}

const Glyph* Font::glyph(uint32_t codepoint, int pixelSize, FontWeight weight)
{
    if (m_faces.empty())
        return nullptr;

    pixelSize = std::min(std::max(pixelSize, 6), 200);

    uint64_t key = cacheKey(codepoint, pixelSize, weight);
    auto it = m_cache.find(key);
    if (it != m_cache.end())
        return &it->second;

    Glyph glyph;
    if (rasterize(codepoint, pixelSize, weight, glyph)) {
        auto inserted = m_cache.emplace(key, glyph);
        return &inserted.first->second;
    }

    // No face has this codepoint: substitute a space so the line keeps its
    // rhythm instead of collapsing. Any other failure (atlas or staging full)
    // is transient and must not be cached.
    uint32_t unused = 0;
    if (codepoint != ' ' && faceForCodepoint(codepoint, unused) < 0)
        return this->glyph(' ', pixelSize, weight);

    return nullptr;
}

float Font::kerning(const Glyph& a, const Glyph& b, int pixelSize)
{
    if (a.face < 0 || a.face != b.face)
        return 0.0f;
    if (!setSize(a.face, pixelSize))
        return 0.0f;

    FT_Face face = m_faces[a.face].face;
    if (!FT_HAS_KERNING(face))
        return 0.0f;

    FT_Vector delta {};
    if (FT_Get_Kerning(face, a.index, b.index, FT_KERNING_DEFAULT, &delta) != 0)
        return 0.0f;
    return static_cast<float>(delta.x) / 64.0f;
}

FontMetrics Font::metrics(int pixelSize)
{
    pixelSize = std::min(std::max(pixelSize, 6), 200);

    auto it = m_metrics.find(static_cast<uint32_t>(pixelSize));
    if (it != m_metrics.end())
        return it->second;

    FontMetrics m;
    if (!m_faces.empty() && setSize(0, pixelSize)) {
        FT_Size_Metrics& sm = m_faces[0].face->size->metrics;
        m.ascender = static_cast<float>(sm.ascender) / 64.0f;
        m.descender = static_cast<float>(sm.descender) / 64.0f;
        m.lineHeight = static_cast<float>(sm.height) / 64.0f;
    } else {
        m.ascender = pixelSize * 0.8f;
        m.descender = -pixelSize * 0.2f;
        m.lineHeight = pixelSize * 1.2f;
    }

    m_metrics.emplace(static_cast<uint32_t>(pixelSize), m);
    return m;
}

float Font::atlasFill() const
{
    float rows = static_cast<float>(m_penY + m_rowHeight);
    return std::min(rows / static_cast<float>(kAtlasSize), 1.0f);
}

} // namespace nxp
