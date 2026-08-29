#include "gfx/renderer.h"

#include "core/log.h"
#include "core/util.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace nxp {

namespace {
    // Enough for a screen densely packed with text; the plaza tops out around
    // 2,500 quads with every card and label on screen.
    // A Mii is around 450 quads at full detail, and a busy radar draws a dozen
    // faces at once on top of the interface itself.
    constexpr uint32_t kMaxQuads = 16384;
    constexpr uint32_t kMaxVertices = kMaxQuads * 4;

    struct FrameUniforms {
        float invViewport[2];
        float padding[2];
    };

    struct DkshHeader {
        uint32_t magic;
        uint32_t headerSize;
        uint32_t controlSize;
        uint32_t codeSize;
        uint32_t programsOffset;
        uint32_t numPrograms;
    };

    bool loadShader(MemPool& pool, const char* path, dk::Shader& shader, MemPool::Slice& codeMem)
    {
        FILE* f = fopen(path, "rb");
        if (!f) {
            LOG("renderer: cannot open %s", path);
            return false;
        }

        DkshHeader header {};
        if (fread(&header, sizeof(header), 1, f) != 1) {
            fclose(f);
            return false;
        }

        void* control = malloc(header.controlSize);
        if (!control) {
            fclose(f);
            return false;
        }

        rewind(f);
        bool ok = fread(control, header.controlSize, 1, f) == 1;

        if (ok) {
            codeMem = pool.allocate(header.codeSize, DK_SHADER_CODE_ALIGNMENT);
            ok = static_cast<bool>(codeMem)
                && fread(codeMem.cpuAddr, header.codeSize, 1, f) == 1;
        }

        if (ok) {
            dk::ShaderMaker { codeMem.block, codeMem.offset }
                .setControl(control)
                .setProgramId(0)
                .initialize(shader);
        } else {
            LOG("renderer: failed to load %s", path);
        }

        free(control);
        fclose(f);
        return ok;
    }

    uint32_t toUpperAscii(uint32_t cp)
    {
        return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp;
    }

    bool isCjk(uint32_t cp)
    {
        return (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x2E80 && cp <= 0xA4CF)
            || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF)
            || (cp >= 0xFF00 && cp <= 0xFF60);
    }
}

// The attribute table has to match Renderer::Vertex exactly; it lives here
// rather than in the anonymous namespace above so offsetof can see the type.
struct RendererVertexLayout {
    float pos[2];
    float uv[2];
    uint32_t color;
    float local[2];
    float halfSize[2];
    float params[4];
};

namespace {
    constexpr std::array VertexAttribs = {
        DkVtxAttribState { 0, 0, offsetof(RendererVertexLayout, pos), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0 },
        DkVtxAttribState { 0, 0, offsetof(RendererVertexLayout, uv), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0 },
        DkVtxAttribState { 0, 0, offsetof(RendererVertexLayout, color), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0 },
        DkVtxAttribState { 0, 0, offsetof(RendererVertexLayout, local), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0 },
        DkVtxAttribState { 0, 0, offsetof(RendererVertexLayout, halfSize), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0 },
        DkVtxAttribState { 0, 0, offsetof(RendererVertexLayout, params), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0 },
    };

    constexpr std::array VertexBufferStates = {
        DkVtxBufferState { sizeof(RendererVertexLayout), 0 },
    };
}

bool Renderer::init(Gpu& gpu, Font& font)
{
    static_assert(sizeof(Renderer::Vertex) == sizeof(RendererVertexLayout),
        "vertex layout and attribute table have drifted apart");

    m_gpu = &gpu;
    m_font = &font;

    if (!loadShader(gpu.codePool(), "romfs:/shaders/ui_vsh.dksh", m_vertexShader, m_vertexShaderCode))
        return false;
    if (!loadShader(gpu.codePool(), "romfs:/shaders/ui_fsh.dksh", m_fragmentShader, m_fragmentShaderCode))
        return false;

    m_vertexMem = gpu.dataPool().allocate(
        kMaxVertices * sizeof(Vertex) * Gpu::NumFrames, alignof(Vertex));
    m_uniformMem = gpu.dataPool().allocate(sizeof(FrameUniforms), DK_UNIFORM_BUF_ALIGNMENT);
    m_imageDescriptorMem = gpu.dataPool().allocate(sizeof(DkImageDescriptor) * 2,
        DK_IMAGE_DESCRIPTOR_ALIGNMENT);
    m_samplerDescriptorMem = gpu.dataPool().allocate(sizeof(DkSamplerDescriptor) * 2,
        DK_SAMPLER_DESCRIPTOR_ALIGNMENT);

    if (!m_vertexMem || !m_uniformMem || !m_imageDescriptorMem || !m_samplerDescriptorMem) {
        LOG("renderer: out of data pool memory");
        return false;
    }

    // Publish the glyph atlas and its sampler once; nothing else needs a
    // descriptor, so both slots point at the atlas and unit 1 is a safe no-op.
    {
        dk::UniqueCmdBuf setup = dk::CmdBufMaker { gpu.device() }.create();
        MemPool::Slice mem = gpu.dataPool().allocate(4096, DK_CMDMEM_ALIGNMENT);
        if (!mem)
            return false;
        setup.addMemory(mem.block, mem.offset, mem.size);

        dk::Sampler sampler;
        sampler.setFilter(DkFilter_Linear, DkFilter_Linear);
        sampler.setWrapMode(DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge);
        dk::SamplerDescriptor samplerDescriptor;
        samplerDescriptor.initialize(sampler);

        DkImageDescriptor images[2] = { font.descriptor(), font.descriptor() };
        DkSamplerDescriptor samplers[2] = { samplerDescriptor, samplerDescriptor };

        setup.pushData(m_imageDescriptorMem.gpuAddr, images, sizeof(images));
        setup.pushData(m_samplerDescriptorMem.gpuAddr, samplers, sizeof(samplers));

        gpu.queue().submitCommands(setup.finishList());
        gpu.queue().waitIdle();
    }

    m_vertices.reserve(kMaxVertices);
    m_batches.reserve(64);
    m_ready = true;
    return true;
}

void Renderer::exit()
{
    m_ready = false;
    m_vertices.clear();
    m_batches.clear();
}

void Renderer::beginFrame(dk::CmdBuf cmd)
{
    m_cmd = cmd;
    m_scale = static_cast<float>(m_gpu->height()) / DesignHeight;

    m_vertices.clear();
    m_batches.clear();
    m_clipStack.clear();

    m_font->beginFrame(cmd, m_gpu->frameIndex());

    FrameUniforms uniforms {};
    uniforms.invViewport[0] = 2.0f / static_cast<float>(m_gpu->width());
    uniforms.invViewport[1] = 2.0f / static_cast<float>(m_gpu->height());

    dk::RasterizerState rasterizer;
    rasterizer.setCullMode(DkFace_None);

    dk::ColorState color;
    color.setBlendEnable(0, true);

    dk::ColorWriteState colorWrite;
    dk::DepthStencilState depthStencil;
    depthStencil.setDepthTestEnable(false);
    depthStencil.setDepthWriteEnable(false);

    dk::BlendState blend; // defaults to src-alpha over one-minus-src-alpha

    cmd.bindShaders(DkStageFlag_GraphicsMask, { &m_vertexShader, &m_fragmentShader });
    cmd.bindUniformBuffer(DkStage_Vertex, 0, m_uniformMem.gpuAddr, m_uniformMem.size);
    cmd.pushConstants(m_uniformMem.gpuAddr, m_uniformMem.size, 0, sizeof(uniforms), &uniforms);

    cmd.bindImageDescriptorSet(m_imageDescriptorMem.gpuAddr, 2);
    cmd.bindSamplerDescriptorSet(m_samplerDescriptorMem.gpuAddr, 2);
    cmd.bindTextures(DkStage_Fragment, 0,
        { dkMakeTextureHandle(0, 0), dkMakeTextureHandle(1, 1) });

    cmd.bindRasterizerState(rasterizer);
    cmd.bindColorState(color);
    cmd.bindColorWriteState(colorWrite);
    cmd.bindDepthStencilState(depthStencil);
    cmd.bindBlendStates(0, { blend });

    uint32_t frameOffset = m_gpu->frameIndex() * kMaxVertices * sizeof(Vertex);
    cmd.bindVtxBuffer(0, m_vertexMem.gpuAddr + frameOffset, kMaxVertices * sizeof(Vertex));
    cmd.bindVtxAttribState(VertexAttribs);
    cmd.bindVtxBufferState(VertexBufferStates);
}

void Renderer::clear(Color color)
{
    m_cmd.clearColor(0, DkColorMask_RGBA, color.r, color.g, color.b, color.a);
}

DkScissor Renderer::currentScissor() const
{
    Rect r = m_clipStack.empty() ? viewport() : m_clipStack.back();
    Rect fb = toFb(r);

    int32_t x0 = static_cast<int32_t>(fb.x);
    int32_t y0 = static_cast<int32_t>(fb.y);
    int32_t x1 = static_cast<int32_t>(fb.right() + 0.5f);
    int32_t y1 = static_cast<int32_t>(fb.bottom() + 0.5f);

    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, static_cast<int32_t>(m_gpu->width()));
    y1 = std::min(y1, static_cast<int32_t>(m_gpu->height()));

    DkScissor scissor {};
    scissor.x = static_cast<uint32_t>(x0);
    scissor.y = static_cast<uint32_t>(y0);
    scissor.width = static_cast<uint32_t>(std::max(x1 - x0, 0));
    scissor.height = static_cast<uint32_t>(std::max(y1 - y0, 0));
    return scissor;
}

void Renderer::closeBatch()
{
    if (m_batches.empty())
        return;
    Batch& batch = m_batches.back();
    batch.count = static_cast<uint32_t>(m_vertices.size()) - batch.first;
}

Rect Renderer::clipRect() const
{
    return m_clipStack.empty() ? viewport() : m_clipStack.back();
}

void Renderer::pushClipVertical(const Rect& r)
{
    // A scroll viewport only ever needs to cut content off at its top and
    // bottom edge. Clipping it sideways as well would shave the few pixels a
    // focus ring or a card shadow legitimately occupies next to a row.
    pushClip(Rect { 0.0f, r.y, DesignWidth, r.h });
}

void Renderer::pushClipHorizontal(const Rect& r)
{
    pushClip(Rect { r.x, 0.0f, r.w, DesignHeight });
}

void Renderer::pushClip(const Rect& r)
{
    Rect clipped = m_clipStack.empty() ? r : r.intersect(m_clipStack.back());
    closeBatch();
    m_clipStack.push_back(clipped);
    m_batches.push_back(Batch { static_cast<uint32_t>(m_vertices.size()), 0, currentScissor() });
}

void Renderer::popClip()
{
    if (m_clipStack.empty())
        return;
    closeBatch();
    m_clipStack.pop_back();
    m_batches.push_back(Batch { static_cast<uint32_t>(m_vertices.size()), 0, currentScissor() });
}

void Renderer::quad(const Rect& fbShape, const Color colors[4], float radiusTop,
    float radiusBottom, Mode mode, float aux, const float uv[4], float fbPad)
{
    if (fbShape.w <= 0.0f || fbShape.h <= 0.0f)
        return;
    if (m_vertices.size() + 4 > kMaxVertices)
        return;

    if (m_batches.empty())
        m_batches.push_back(Batch { 0, 0, currentScissor() });

    // halfSize and local describe the shape; the geometry is the shape grown by
    // fbPad, around the same centre.
    float hw = fbShape.w * 0.5f;
    float hh = fbShape.h * 0.5f;
    float cx = fbShape.x + hw;
    float cy = fbShape.y + hh;

    Rect fb = fbShape.inset(-fbPad);

    const float xs[4] = { fb.x, fb.right(), fb.right(), fb.x };
    const float ys[4] = { fb.y, fb.y, fb.bottom(), fb.bottom() };
    const float us[4] = { uv[0], uv[2], uv[2], uv[0] };
    const float vs[4] = { uv[1], uv[1], uv[3], uv[3] };

    for (int i = 0; i < 4; i++) {
        Vertex v {};
        v.pos[0] = xs[i];
        v.pos[1] = ys[i];
        v.uv[0] = us[i];
        v.uv[1] = vs[i];
        v.color = colors[i].packed();
        v.local[0] = xs[i] - cx;
        v.local[1] = ys[i] - cy;
        v.halfSize[0] = hw;
        v.halfSize[1] = hh;
        v.params[0] = radiusTop;
        v.params[1] = static_cast<float>(mode);
        v.params[2] = aux;
        v.params[3] = radiusBottom;
        m_vertices.push_back(v);
    }
}

void Renderer::quadUniform(const Rect& designRect, Color color, float radiusTop,
    float radiusBottom, Mode mode, float aux, float fbPad)
{
    const Color colors[4] = { color, color, color, color };
    const float uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    quad(toFb(designRect), colors, radiusTop * m_scale, radiusBottom * m_scale, mode, aux, uv,
        fbPad);
}

// One framebuffer pixel, the width of the shader's antialiasing ramp.
static constexpr float kEdgePad = 1.0f;

void Renderer::rect(const Rect& r, Color color)
{
    quadUniform(r, color, 0.0f, 0.0f, Mode_Fill, 0.0f, kEdgePad);
}

void Renderer::roundRect(const Rect& r, float radius, Color color)
{
    quadUniform(r, color, radius, radius, Mode_Fill, 0.0f, kEdgePad);
}

void Renderer::roundRect(const Rect& r, float radiusTop, float radiusBottom, Color color)
{
    quadUniform(r, color, radiusTop, radiusBottom, Mode_Fill, 0.0f, kEdgePad);
}

void Renderer::gradientRect(const Rect& r, Color top, Color bottom, float radius)
{
    const Color colors[4] = { top, top, bottom, bottom };
    const float uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    quad(toFb(r), colors, radius * m_scale, radius * m_scale, Mode_Fill, 0.0f, uv, kEdgePad);
}

void Renderer::gradientRect(const Rect& r, Color top, Color bottom, float radiusTop,
    float radiusBottom)
{
    const Color colors[4] = { top, top, bottom, bottom };
    const float uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    quad(toFb(r), colors, radiusTop * m_scale, radiusBottom * m_scale, Mode_Fill, 0.0f, uv,
        kEdgePad);
}

void Renderer::gradientRectH(const Rect& r, Color left, Color right, float radius)
{
    const Color colors[4] = { left, right, right, left };
    const float uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    quad(toFb(r), colors, radius * m_scale, radius * m_scale, Mode_Fill, 0.0f, uv, kEdgePad);
}

void Renderer::quadRotated(const Rect& designRect, Color color, Mode mode,
    const float params[4], float radians, float fbPad)
{
    Rect fb = toFb(designRect);
    if (fb.w <= 0.0f || fb.h <= 0.0f)
        return;
    if (m_vertices.size() + 4 > kMaxVertices)
        return;

    if (m_batches.empty())
        m_batches.push_back(Batch { 0, 0, currentScissor() });

    float hw = fb.w * 0.5f;
    float hh = fb.h * 0.5f;
    float cx = fb.x + hw;
    float cy = fb.y + hh;

    // Corner offsets in the shape's own frame, grown by the antialiasing pad.
    float px = hw + fbPad;
    float py = hh + fbPad;
    const float lx[4] = { -px, px, px, -px };
    const float ly[4] = { -py, -py, py, py };

    float c = std::cos(radians);
    float s = std::sin(radians);
    uint32_t packed = color.packed();

    for (int i = 0; i < 4; i++) {
        Vertex v {};
        v.pos[0] = cx + lx[i] * c - ly[i] * s;
        v.pos[1] = cy + lx[i] * s + ly[i] * c;
        v.uv[0] = 0.0f;
        v.uv[1] = 0.0f;
        v.color = packed;
        v.local[0] = lx[i];
        v.local[1] = ly[i];
        v.halfSize[0] = hw;
        v.halfSize[1] = hh;
        v.params[0] = params[0];
        v.params[1] = static_cast<float>(mode);
        v.params[2] = params[2];
        v.params[3] = params[3];
        m_vertices.push_back(v);
    }
}

void Renderer::ellipse(float cx, float cy, float rx, float ry, Color color, float radians)
{
    const float params[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    quadRotated(Rect { cx - rx, cy - ry, rx * 2.0f, ry * 2.0f }, color, Mode_Ellipse, params,
        radians, kEdgePad);
}

void Renderer::strokeRect(const Rect& r, float radius, float thickness, Color color)
{
    // The shader's stroke band is centred on the rounded-rect edge, so a naive
    // stroke spills half its width outside `r`. Inset by that half instead: a
    // border then lies entirely inside the rect it belongs to, which is what
    // every caller means and what keeps a clipped list from shaving it off.
    float half = thickness * 0.5f;
    Rect band = r.inset(half);
    float inner = std::max(radius - half, 0.0f);
    // The band straddles `band`'s edge by half the line width in each
    // direction, so the geometry has to reach that far out plus the ramp.
    quadUniform(band, color, inner, inner, Mode_Stroke, thickness * m_scale,
        half * m_scale + kEdgePad);
}

void Renderer::trapezoid(float yTop, float xTopLeft, float xTopRight, float yBottom,
    float xBottomLeft, float xBottomRight, Color color)
{
    if (yBottom <= yTop)
        return;
    if (m_vertices.size() + 4 > kMaxVertices)
        return;

    // Framebuffer space, and no vertical padding: the band has to end exactly
    // where the next one begins or the join shows.
    float y0 = yTop * m_scale;
    float y1 = yBottom * m_scale;
    float tl = xTopLeft * m_scale;
    float tr = xTopRight * m_scale;
    float bl = xBottomLeft * m_scale;
    float br = xBottomRight * m_scale;

    float left = std::min(tl, bl) - kEdgePad;
    float right = std::max(tr, br) + kEdgePad;
    if (right <= left)
        return;

    if (m_batches.empty())
        m_batches.push_back(Batch { 0, 0, currentScissor() });

    float cx = (left + right) * 0.5f;
    float cy = (y0 + y1) * 0.5f;
    float dy = y1 - y0;

    // Each side as x = m*y + k in the quad's own frame.
    float ml = (bl - tl) / dy;
    float kl = (tl - cx) - ml * (y0 - cy);
    float mr = (br - tr) / dy;
    float kr = (tr - cx) - mr * (y0 - cy);

    const float xs[4] = { left, right, right, left };
    const float ys[4] = { y0, y0, y1, y1 };
    uint32_t packed = color.packed();

    for (int i = 0; i < 4; i++) {
        Vertex v {};
        v.pos[0] = xs[i];
        v.pos[1] = ys[i];
        v.uv[0] = 0.0f;
        v.uv[1] = 0.0f;
        v.color = packed;
        v.local[0] = xs[i] - cx;
        v.local[1] = ys[i] - cy;
        v.halfSize[0] = kr; // the right side's offset; halfSize.y is unused here
        v.halfSize[1] = 0.0f;
        v.params[0] = ml;
        v.params[1] = static_cast<float>(Mode_Trapezoid);
        v.params[2] = kl;
        v.params[3] = mr;
        m_vertices.push_back(v);
    }
}

void Renderer::band(const float corners[8], Color color)
{
    if (m_vertices.size() + 4 > kMaxVertices)
        return;

    float x[4], y[4];
    for (int i = 0; i < 4; i++) {
        x[i] = corners[i * 2] * m_scale;
        y[i] = corners[i * 2 + 1] * m_scale;
    }

    // The normal to the long sides runs from corner 3 to corner 0.
    float nx = x[0] - x[3];
    float ny = y[0] - y[3];
    float len = std::sqrt(nx * nx + ny * ny);
    if (len < 1e-4f)
        return;
    nx /= len;
    ny /= len;

    float cx = (x[0] + x[1] + x[2] + x[3]) * 0.25f;
    float cy = (y[0] + y[1] + y[2] + y[3]) * 0.25f;

    if (m_batches.empty())
        m_batches.push_back(Batch { 0, 0, currentScissor() });

    // Grow the geometry across the band only, so the antialiasing ramp has
    // fragments to land in without the segment growing longer than it is.
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < 4; i++) {
        float d = (x[i] - cx) * nx + (y[i] - cy) * ny;
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }

    uint32_t packed = color.packed();
    for (int i = 0; i < 4; i++) {
        float lx = x[i] - cx;
        float ly = y[i] - cy;
        float d = lx * nx + ly * ny;
        float push = (d >= 0.0f ? kEdgePad : -kEdgePad);

        Vertex v {};
        v.pos[0] = x[i] + nx * push;
        v.pos[1] = y[i] + ny * push;
        v.uv[0] = 0.0f;
        v.uv[1] = 0.0f;
        v.color = packed;
        v.local[0] = lx + nx * push;
        v.local[1] = ly + ny * push;
        v.halfSize[0] = hi; // the far side's offset
        v.halfSize[1] = 0.0f;
        v.params[0] = nx;
        v.params[1] = static_cast<float>(Mode_Band);
        v.params[2] = ny;
        v.params[3] = lo; // the near side's offset
        m_vertices.push_back(v);
    }
}

void Renderer::glow(const Rect& r, Color color, float falloff)
{
    quadUniform(r, color, 0.0f, 0.0f, Mode_Glow, falloff);
}

void Renderer::circle(float cx, float cy, float radius, Color color)
{
    Rect r { cx - radius, cy - radius, radius * 2.0f, radius * 2.0f };
    quadUniform(r, color, radius, radius, Mode_Fill, 0.0f, kEdgePad);
}

float Renderer::lineHeight(const TextStyle& style)
{
    if (style.leading > 0.0f)
        return style.size * style.leading;

    int px = static_cast<int>(style.size * m_scale + 0.5f);
    return m_font->metrics(px).lineHeight / m_scale;
}

float Renderer::text(float x, float yTop, const std::string& utf8, const TextStyle& style)
{
    int px = static_cast<int>(style.size * m_scale + 0.5f);
    FontMetrics metrics = m_font->metrics(px);

    float penX = x * m_scale;
    float baseline = yTop * m_scale + metrics.ascender;
    float startX = penX;
    float tracking = style.tracking * static_cast<float>(px);

    const char* p = utf8.data();
    const char* end = p + utf8.size();
    const Glyph* previous = nullptr;
    uint32_t cp;

    while (utf8Next(p, end, cp)) {
        if (cp == '\n')
            break;
        if (style.uppercase)
            cp = toUpperAscii(cp);

        const Glyph* glyph = m_font->glyph(cp, px, style.weight);
        if (!glyph)
            continue;

        if (previous)
            penX += m_font->kerning(*previous, *glyph, px);

        if (!glyph->blank) {
            Rect fb {
                std::floor(penX + glyph->left + 0.5f),
                std::floor(baseline + glyph->top + 0.5f),
                glyph->width,
                glyph->height
            };
            const Color colors[4] = { style.color, style.color, style.color, style.color };
            const float uv[4] = { glyph->u0, glyph->v0, glyph->u1, glyph->v1 };
            quad(fb, colors, 0.0f, 0.0f, Mode_Text, 0.0f, uv);
        }

        penX += glyph->advance + tracking;
        previous = glyph;
    }

    return (penX - startX) / m_scale;
}

float Renderer::measure(const std::string& utf8, const TextStyle& style)
{
    int px = static_cast<int>(style.size * m_scale + 0.5f);
    float width = 0.0f;
    float tracking = style.tracking * static_cast<float>(px);

    const char* p = utf8.data();
    const char* end = p + utf8.size();
    const Glyph* previous = nullptr;
    uint32_t cp;

    while (utf8Next(p, end, cp)) {
        if (cp == '\n')
            break;
        if (style.uppercase)
            cp = toUpperAscii(cp);

        const Glyph* glyph = m_font->glyph(cp, px, style.weight);
        if (!glyph)
            continue;
        if (previous)
            width += m_font->kerning(*previous, *glyph, px);
        width += glyph->advance + tracking;
        previous = glyph;
    }

    return width / m_scale;
}

float Renderer::text(const Rect& box, const std::string& utf8, const TextStyle& style,
    Align align, VAlign valign)
{
    float width = align == Align::Left ? 0.0f : measure(utf8, style);

    float x = box.x;
    if (align == Align::Center)
        x = box.x + (box.w - width) * 0.5f;
    else if (align == Align::Right)
        x = box.right() - width;

    int px = static_cast<int>(style.size * m_scale + 0.5f);
    FontMetrics metrics = m_font->metrics(px);
    float emHeight = (metrics.ascender - metrics.descender) / m_scale;

    float y = box.y;
    if (valign == VAlign::Middle)
        y = box.y + (box.h - emHeight) * 0.5f;
    else if (valign == VAlign::Bottom)
        y = box.bottom() - emHeight;

    return text(x, y, utf8, style);
}

std::string Renderer::ellipsize(const std::string& utf8, const TextStyle& style, float maxWidth)
{
    if (measure(utf8, style) <= maxWidth)
        return utf8;

    static const char* kEllipsis = "\xE2\x80\xA6"; // U+2026
    float ellipsisWidth = measure(kEllipsis, style);
    float budget = maxWidth - ellipsisWidth;
    if (budget <= 0.0f)
        return kEllipsis;

    int px = static_cast<int>(style.size * m_scale + 0.5f);
    float width = 0.0f;
    const char* p = utf8.data();
    const char* end = p + utf8.size();
    const char* lastGood = p;
    uint32_t cp;

    while (p < end) {
        const char* before = p;
        if (!utf8Next(p, end, cp))
            break;
        const Glyph* glyph = m_font->glyph(style.uppercase ? toUpperAscii(cp) : cp, px, style.weight);
        if (glyph)
            width += (glyph->advance + style.tracking * static_cast<float>(px)) / m_scale;
        if (width > budget) {
            lastGood = before;
            break;
        }
        lastGood = p;
    }

    return std::string(utf8.data(), static_cast<size_t>(lastGood - utf8.data())) + kEllipsis;
}

float Renderer::textWrapped(const Rect& box, const std::string& utf8, const TextStyle& style,
    int maxLines, Align align)
{
    return layoutWrapped(box, utf8, style, maxLines, align, true);
}

float Renderer::measureWrapped(float width, const std::string& utf8, const TextStyle& style,
    int maxLines)
{
    return layoutWrapped(Rect { 0.0f, 0.0f, width, 0.0f }, utf8, style, maxLines,
        Align::Left, false);
}

float Renderer::layoutWrapped(const Rect& box, const std::string& utf8, const TextStyle& style,
    int maxLines, Align align, bool emit)
{
    float lh = lineHeight(style);
    if (box.w <= 0.0f)
        return 0.0f;

    // Split into atoms that may not be broken: runs of non-space characters,
    // single CJK characters (which wrap anywhere), and runs of spaces.
    struct Atom {
        std::string text;
        bool space = false;
        bool newline = false;
        bool standalone = false; // spaces and CJK: a wrap point on both sides
    };
    std::vector<Atom> atoms;

    {
        const char* p = utf8.data();
        const char* end = p + utf8.size();
        uint32_t cp;
        while (p < end) {
            const char* start = p;
            if (!utf8Next(p, end, cp))
                break;

            std::string piece(start, static_cast<size_t>(p - start));
            if (cp == '\n') {
                atoms.push_back(Atom { std::string(), false, true, true });
                continue;
            }

            bool isSpace = (cp == ' ' || cp == '\t');
            bool standalone = isSpace || isCjk(cp);
            bool startNew = atoms.empty() || standalone || atoms.back().standalone;

            if (startNew)
                atoms.push_back(Atom { piece, isSpace, false, standalone });
            else
                atoms.back().text += piece;
        }
    }

    std::vector<std::string> lines;
    std::string current;
    float currentWidth = 0.0f;

    auto flush = [&]() {
        lines.push_back(current);
        current.clear();
        currentWidth = 0.0f;
    };

    for (const Atom& atom : atoms) {
        if (atom.newline) {
            flush();
            continue;
        }

        float atomWidth = measure(atom.text, style);
        if (!current.empty() && currentWidth + atomWidth > box.w) {
            if (atom.space)
                continue; // never start a line with a space
            flush();
        }
        if (current.empty() && atom.space)
            continue;

        current += atom.text;
        currentWidth += atomWidth;
    }
    if (!current.empty())
        lines.push_back(current);

    float y = box.y;
    size_t limit = maxLines > 0 ? static_cast<size_t>(maxLines) : lines.size();
    for (size_t i = 0; i < lines.size() && i < limit; i++) {
        std::string line = lines[i];
        bool last = (i + 1 == limit) && lines.size() > limit;
        if (last)
            line = ellipsize(line + " ", style, box.w);
        if (emit)
            text(Rect { box.x, y, box.w, lh }, line, style, align, VAlign::Top);
        y += lh;
    }

    return y - box.y;
}

void Renderer::endFrame()
{
    closeBatch();

    m_lastQuads = static_cast<unsigned>(m_vertices.size() / 4);
    m_lastDrawCalls = 0;

    if (!m_vertices.empty()) {
        uint32_t frameOffset = m_gpu->frameIndex() * kMaxVertices * sizeof(Vertex);
        void* dst = static_cast<uint8_t*>(m_vertexMem.cpuAddr) + frameOffset;
        memcpy(dst, m_vertices.data(), m_vertices.size() * sizeof(Vertex));
    }

    // Glyph copies for this frame were recorded above; make sure the texture
    // cache cannot hand a draw the contents of those texels from before.
    if (m_font->atlasDirty())
        m_cmd.barrier(DkBarrier_None, DkInvalidateFlags_Image);

    bool haveScissor = false;
    DkScissor lastScissor {};

    for (const Batch& batch : m_batches) {
        if (batch.count == 0)
            continue;

        if (!haveScissor || memcmp(&lastScissor, &batch.scissor, sizeof(DkScissor)) != 0) {
            m_cmd.setScissors(0, { batch.scissor });
            lastScissor = batch.scissor;
            haveScissor = true;
        }

        m_cmd.draw(DkPrimitive_Quads, batch.count, 1, batch.first, 0);
        m_lastDrawCalls++;
    }

    if (haveScissor)
        m_cmd.setScissors(0, { { 0, 0, m_gpu->width(), m_gpu->height() } });
}

} // namespace nxp
