#pragma once

#include "gfx/font.h"
#include "gfx/gpu.h"
#include "gfx/types.h"

#include <string>
#include <vector>

namespace nxp {

struct TextStyle {
    float size = 26.0f; // design-space pixels; theme::textBase
    FontWeight weight = FontWeight::Regular;
    // Always set this from the palette. The default is a mid grey rather than
    // a theme colour - gfx does not know the ui layer exists - chosen so
    // that forgetting it is visible on both a light and a dark surface
    // instead of silently invisible on one of them.
    Color color = Color::hex(0x808080);
    // Letter spacing in em, matching the design's --tracking-* tokens. Being
    // relative is the point: the same token has to look right on an 18px meta
    // label and an 88px hero title.
    float tracking = 0.0f;
    // Line height as a multiple of `size`, matching --leading-*. 0 means the
    // face's own metrics.
    float leading = 0.0f;
    bool uppercase = false; // ASCII only, for the eyebrow labels
};

// Immediate-mode 2D renderer on top of deko3d.
//
// Everything - panels, gradients, glows, focus rings, text - is a quad
// carrying its own shape parameters, so the whole interface is one pipeline
// and (unless something clips) a single draw call. Coordinates are given in a
// fixed 1920x1080 design space and scaled to the real framebuffer, which is
// what lets the same layout code serve docked and handheld.
class Renderer {
public:
    static constexpr float DesignWidth = 1920.0f;
    static constexpr float DesignHeight = 1080.0f;

    bool init(Gpu& gpu, Font& font);
    void exit();

    // Records the frame's state setup into `cmd`.
    void beginFrame(dk::CmdBuf cmd);
    // Uploads the vertices and emits the draw calls.
    void endFrame();

    float scale() const { return m_scale; }
    Rect viewport() const { return Rect { 0, 0, DesignWidth, DesignHeight }; }

    // ------------------------------------------------------------- shapes

    void clear(Color color);
    void rect(const Rect& r, Color color);
    void roundRect(const Rect& r, float radius, Color color);
    // Independent top and bottom corner radii.
    void roundRect(const Rect& r, float radiusTop, float radiusBottom, Color color);
    // Vertical gradient; corners rounded by `radius`.
    void gradientRect(const Rect& r, Color top, Color bottom, float radius = 0.0f);
    // Independent top and bottom corner radii, for a gradient that fills a
    // rounded card completely, or only the top of one.
    void gradientRect(const Rect& r, Color top, Color bottom, float radiusTop,
        float radiusBottom);
    // Horizontal gradient. The mockups use these as veils, fading a portrait
    // into the content beside it.
    void gradientRectH(const Rect& r, Color left, Color right, float radius = 0.0f);
    void strokeRect(const Rect& r, float radius, float thickness, Color color);

    // Radial falloff from the centre of `r`. `falloff` > 1 tightens the pool.
    void glow(const Rect& r, Color color, float falloff = 2.0f);
    void circle(float cx, float cy, float radius, Color color);

    // A filled ellipse, optionally turned about its own centre.
    //
    // A rounded rectangle cannot do this: pushing its radius to half the height
    // gives a stadium, which has straight sides. Faces do not. Everything round
    // on a Mii - the head, an eye, an ear, a nose, a hair cap - is one of
    // these, which is why the primitive exists.
    void ellipse(float cx, float cy, float rx, float ry, Color color, float radians = 0.0f);


    // A horizontal band bounded by two straight, possibly slanted, sides.
    //
    // This is what the Mii artwork is made of. A real outline sliced into bands
    // at every one of its vertices is reproduced exactly by a stack of these,
    // and they tile without seams because neighbours share their horizontal
    // edges - which is also why only the slanted sides are antialiased.
    void trapezoid(float yTop, float xTopLeft, float xTopRight, float yBottom,
        float xBottomLeft, float xBottomRight, Color color);

    // A quad whose two long sides are parallel: one segment of a stroke.
    //
    // `corners` is x,y for four corners in the order the baker writes them, the
    // first and last sharing one end of the segment. Only the long sides are
    // antialiased - the ends are butt-cut, and the discs drawn at each join
    // cover them.
    void band(const float corners[8], Color color);

    // --------------------------------------------------------------- text

    // Draws one line with its em box starting at `yTop`. Returns the width.
    float text(float x, float yTop, const std::string& utf8, const TextStyle& style);
    // Draws one line positioned inside `box`.
    float text(const Rect& box, const std::string& utf8, const TextStyle& style,
        Align align = Align::Left, VAlign valign = VAlign::Top);
    float measure(const std::string& utf8, const TextStyle& style);

    // Word-wraps inside `box.w`. Returns the height consumed. `maxLines` of 0
    // means unlimited; the last line is ellipsised when it overflows.
    float textWrapped(const Rect& box, const std::string& utf8, const TextStyle& style,
        int maxLines = 0, Align align = Align::Left);

    // The height textWrapped would consume, without drawing anything. Lets a
    // panel size itself to its contents before it draws the surface behind
    // them.
    float measureWrapped(float width, const std::string& utf8, const TextStyle& style,
        int maxLines = 0);

    // Truncates with a trailing ellipsis so the result fits `maxWidth`.
    std::string ellipsize(const std::string& utf8, const TextStyle& style, float maxWidth);

    float lineHeight(const TextStyle& style);

    // --------------------------------------------------------------- clip

    void pushClip(const Rect& r);
    // Clips only top and bottom, leaving the full width available. This is
    // what a scrolling list wants: clipping to the row width instead would
    // crop the focus ring and the card shadow at the sides.
    void pushClipVertical(const Rect& r);
    // Clips only left and right. What a horizontal carousel wants, for the
    // same reason in the other axis.
    void pushClipHorizontal(const Rect& r);
    void popClip();

    // The clip in force, or the whole viewport when the stack is empty. Touch
    // zones are intersected with it, so a row scrolled half out of a list
    // cannot be tapped on the half that is not visible.
    Rect clipRect() const;

    // Draw-call count of the previous frame, for the debug overlay.
    unsigned lastDrawCalls() const { return m_lastDrawCalls; }
    unsigned lastQuads() const { return m_lastQuads; }

private:
    struct Vertex {
        float pos[2];
        float uv[2];
        uint32_t color;
        float local[2];
        float halfSize[2];
        float params[4];
    };

    struct Batch {
        uint32_t first = 0;
        uint32_t count = 0;
        DkScissor scissor {};
    };

    enum Mode : int {
        Mode_Fill = 0,
        Mode_Text = 1,
        Mode_Image = 2,
        Mode_Glow = 3,
        Mode_Stroke = 4,
        Mode_Ellipse = 5,
        Mode_Trapezoid = 6,
        Mode_Band = 7,
    };

    // One quad in framebuffer pixels. `colors` is TL, TR, BR, BL.
    //
    // `fbPad` grows the *geometry* without growing the shape: the signed
    // distance field still describes `fbShape`, but there are fragments for
    // `fbPad` pixels around it. Without that margin the shader has nowhere to
    // draw the outer half of a stroke or the outer half of an edge's
    // antialiasing ramp, and both are simply lost.
    void quad(const Rect& fbShape, const Color colors[4], float radiusTop, float radiusBottom,
        Mode mode, float aux, const float uv[4], float fbPad = 0.0f);
    void quadUniform(const Rect& designRect, Color color, float radiusTop, float radiusBottom,
        Mode mode, float aux, float fbPad = 0.0f);
    // Emits a quad turned by `radians` about its own centre.
    //
    // Costs nothing extra in the shader: the corners are rotated while `local`
    // is left in the shape's own frame, so the distance field still describes an
    // upright shape - seen through a rotated window. `params` is passed to the
    // shader as-is, so any length in it must already be in framebuffer pixels.
    void quadRotated(const Rect& designRect, Color color, Mode mode, const float params[4],
        float radians, float fbPad);

    float layoutWrapped(const Rect& box, const std::string& utf8, const TextStyle& style,
        int maxLines, Align align, bool emit);

    Rect toFb(const Rect& r) const
    {
        return Rect { r.x * m_scale, r.y * m_scale, r.w * m_scale, r.h * m_scale };
    }

    DkScissor currentScissor() const;
    void closeBatch();

    Gpu* m_gpu = nullptr;
    Font* m_font = nullptr;

    MemPool::Slice m_vertexMem;
    MemPool::Slice m_uniformMem;
    MemPool::Slice m_imageDescriptorMem;
    MemPool::Slice m_samplerDescriptorMem;

    dk::Shader m_vertexShader;
    dk::Shader m_fragmentShader;
    MemPool::Slice m_vertexShaderCode;
    MemPool::Slice m_fragmentShaderCode;

    dk::CmdBuf m_cmd = nullptr;

    std::vector<Vertex> m_vertices;
    std::vector<Batch> m_batches;
    std::vector<Rect> m_clipStack;

    float m_scale = 1.0f;
    unsigned m_lastDrawCalls = 0;
    unsigned m_lastQuads = 0;
    bool m_ready = false;
};

} // namespace nxp
