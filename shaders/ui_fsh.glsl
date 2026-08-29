#version 460

// nx-plaza 2D UI fragment shader.
//
// Modes (inParams.y):
//   0  fill    signed-distance rounded rectangle, independent top/bottom radii
//   1  text    coverage sampled from the R8 glyph atlas
//   2  image   RGBA texture modulated by the vertex colour
//   3  glow    radial falloff, exponent in aux -- used for the plaza spotlights
//   4  stroke  rounded rectangle outline, line width in aux
//   5  ellipse signed-distance ellipse
//   6  trapez  horizontal band between two slanted lines -- a filled Mii part
//   7  band    quad between two parallel sides -- a stroked Mii part
//
// Modes 6 and 7 carry the Mii parts. That artwork is real outlines, so it
// arrives as horizontal trapezoids: within a band containing no vertex every
// edge is a straight line, and stacked bands reproduce the outline exactly.
// Only the two slanted sides need antialiasing -- the horizontal ones are
// shared with the bands above and below, and feathering those would draw a
// seam along every one.
//
// Mode 5 rounds off what the Mii parts need: a face is curves, not boxes, and a
// rounded rectangle with its radius set to half its height gives a stadium,
// whose sides are straight.

layout (location = 0) noperspective in vec2 inUv;
layout (location = 1) noperspective in vec4 inColor;
layout (location = 2) noperspective in vec2 inLocal;
layout (location = 3) flat in vec2 inHalf;
layout (location = 4) flat in vec4 inParams;

layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D glyphAtlas;
layout (binding = 1) uniform sampler2D imageTex;

/// Approximate signed distance to an ellipse, in pixels.
///
/// The exact solution is a quartic and far more than a face needs. This is the
/// usual gradient correction: the normalised distance divided by the length of
/// its own gradient, which is accurate exactly where it has to be -- within a
/// pixel of the edge, where the antialiasing ramp lives -- and only drifts deep
/// inside the shape, where every value already saturates to "covered".
float sdEllipse(vec2 p, vec2 r)
{
    r = max(r, vec2(0.0001));
    float k1 = length(p / r);
    float k2 = length(p / (r * r));
    return (k1 - 1.0) / max(k2, 0.0001);
}

float sdRoundBox(vec2 p, vec2 b, float r)
{
    r = clamp(r, 0.0, min(b.x, b.y));
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

void main()
{
    int   mode = int(inParams.y + 0.5);
    float aux  = inParams.z;
    // Top half uses params.x, bottom half params.w: lets a card round only its
    // top corners without a second draw.
    float radius = inLocal.y < 0.0 ? inParams.x : inParams.w;

    vec3  rgb   = inColor.rgb;
    float alpha = inColor.a;

    if (mode == 1)
    {
        alpha *= texture(glyphAtlas, inUv).r;
    }
    else if (mode == 2)
    {
        vec4 texel = texture(imageTex, inUv);
        rgb   *= texel.rgb;
        alpha *= texel.a;
    }
    else if (mode == 3)
    {
        float d = length(inLocal / max(inHalf, vec2(0.0001)));
        alpha *= pow(clamp(1.0 - d, 0.0, 1.0), max(aux, 0.0001));
    }
    else if (mode == 4)
    {
        float sd = abs(sdRoundBox(inLocal, inHalf, radius)) - aux * 0.5;
        alpha *= clamp(0.5 - sd, 0.0, 1.0);
    }
    else if (mode == 5)
    {
        alpha *= clamp(0.5 - sdEllipse(inLocal, inHalf), 0.0, 1.0);
    }
    else if (mode == 6)
    {
        // Each side is x = m*y + k, given in the quad's own frame. The quad's
        // top and bottom already bound the band exactly, so they are left alone.
        float xl = inParams.x * inLocal.y + inParams.z;
        float xr = inParams.w * inLocal.y + inHalf.x;
        float dl = (inLocal.x - xl) * inversesqrt(1.0 + inParams.x * inParams.x);
        float dr = (xr - inLocal.x) * inversesqrt(1.0 + inParams.w * inParams.w);
        alpha *= clamp(min(dl, dr) + 0.5, 0.0, 1.0);
    }
    else if (mode == 7)
    {
        // A stroke segment. Its two long sides are parallel, so one normal and
        // two offsets describe it; the ends are cut by the quad itself, and the
        // round joins that cover them are drawn as discs.
        float d = dot(inLocal, vec2(inParams.x, inParams.z));
        alpha *= clamp(min(d - inParams.w, inHalf.x - d) + 0.5, 0.0, 1.0);
    }
    else
    {
        float sd = sdRoundBox(inLocal, inHalf, radius);
        alpha *= clamp(0.5 - sd, 0.0, 1.0);
    }

    if (alpha <= 0.0)
        discard;

    outColor = vec4(rgb, alpha);
}
