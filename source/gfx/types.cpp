#include "gfx/types.h"

namespace nxp {

Color Color::hsl(float hueDegrees, float s, float l, float alpha)
{
    float h = std::fmod(hueDegrees, 360.0f);
    if (h < 0)
        h += 360.0f;
    h /= 60.0f;

    float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    float m = l - c * 0.5f;

    float rr = 0, gg = 0, bb = 0;
    switch (static_cast<int>(h)) {
    case 0: rr = c; gg = x; break;
    case 1: rr = x; gg = c; break;
    case 2: gg = c; bb = x; break;
    case 3: gg = x; bb = c; break;
    case 4: rr = x; bb = c; break;
    default: rr = c; bb = x; break;
    }

    return Color { rr + m, gg + m, bb + m, alpha };
}

} // namespace nxp
