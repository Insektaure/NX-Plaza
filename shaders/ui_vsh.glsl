#version 460

// nx-plaza 2D UI vertex shader.
// Everything the UI draws is a quad; the fragment shader decides what kind of
// quad it is from the packed parameters, so the whole interface can be drawn
// with a single pipeline and a handful of draw calls.

layout (location = 0) in vec2 inPos;      // position, framebuffer pixels
layout (location = 1) in vec2 inUv;       // atlas / image texture coordinates
layout (location = 2) in vec4 inColor;    // RGBA8 vertex colour (interpolated -> gradients)
layout (location = 3) in vec2 inLocal;    // pixel offset from the shape centre
layout (location = 4) in vec2 inHalf;     // shape half extents, pixels
layout (location = 5) in vec4 inParams;   // x: top radius, y: mode, z: aux, w: bottom radius

layout (location = 0) noperspective out vec2 outUv;
layout (location = 1) noperspective out vec4 outColor;
layout (location = 2) noperspective out vec2 outLocal;
layout (location = 3) flat out vec2 outHalf;
layout (location = 4) flat out vec4 outParams;

layout (std140, binding = 0) uniform FrameData
{
    vec2 invViewport;   // (2 / width, 2 / height)
    vec2 padding;
} u;

void main()
{
    // Framebuffer pixels -> clip space, top-left origin.
    gl_Position = vec4(
        inPos.x * u.invViewport.x - 1.0,
        1.0 - inPos.y * u.invViewport.y,
        0.0, 1.0);

    outUv     = inUv;
    outColor  = inColor;
    outLocal  = inLocal;
    outHalf   = inHalf;
    outParams = inParams;
}
