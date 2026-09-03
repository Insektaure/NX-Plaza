#include "ui/plaza_scroll.h"

#include "ui/theme.h"

#include <cmath>

namespace nxp::ui {

void plazaBackdrop(Renderer& r, float camera, float horizon)
{
    Rect sky { 0.0f, 0.0f, Renderer::DesignWidth, horizon };
    r.gradientRect(sky, theme::plazaTop, theme::plazaMid);

    // Far hills, a fifth of the speed.
    float far = camera * 0.22f;
    for (int i = -1; i < 8; i++) {
        float cx = float(i) * 520.0f - std::fmod(far, 520.0f);
        float w = 620.0f;
        float h = 190.0f + float((i + 8) % 3) * 46.0f;
        r.ellipse(cx, horizon, w * 0.5f, h, theme::plazaBottom, 0.0f);
    }

    // Lamp posts, half speed, so there is something with an edge to it to
    // measure the motion against.
    float mid = camera * 0.55f;
    for (int i = -1; i < 12; i++) {
        float x = float(i) * 340.0f - std::fmod(mid, 340.0f);
        Rect post { x, horizon - 210.0f, 8.0f, 210.0f };
        r.rect(post, theme::bg2);
        r.glow(Rect { post.centerX() - 34.0f, post.y - 34.0f, 68.0f, 68.0f },
            theme::accentGlow.scaleAlpha(0.35f), 1.8f);
        r.circle(post.centerX(), post.y, 13.0f, theme::mark);
    }
}

void plazaGround(Renderer& r, float camera, float horizon)
{
    r.rect(Rect { 0.0f, horizon, Renderer::DesignWidth,
               Renderer::DesignHeight - horizon },
        theme::bg1);
    r.rect(Rect { 0.0f, horizon, Renderer::DesignWidth, theme::stroke },
        theme::stroke1);

    // Two rows of dashes at different depths: one just under the horizon, one
    // near the front, so the ground has some floor to it rather than being a
    // flat colour with things standing on it.
    for (int row = 0; row < 2; row++) {
        float y = horizon + 90.0f + float(row) * 230.0f;
        float pitch = 160.0f + float(row) * 80.0f;
        for (int i = -1; i < 26; i++) {
            float x = float(i) * pitch - std::fmod(camera, pitch);
            r.rect(Rect { x, y, 60.0f + float(row) * 20.0f, 4.0f }, theme::bg3);
        }
    }
}

} // namespace nxp::ui
