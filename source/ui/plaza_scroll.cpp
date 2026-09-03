#include "ui/plaza_scroll.h"

#include "ui/theme.h"

#include <cmath>

namespace nxp::ui {

void plazaBackdrop(Renderer& r, float camera, float horizon, float lampPitch,
    float lampGlow)
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
    // measure the motion against. How many and how bright is the caller's
    // business: see the header.
    float mid = camera * 0.55f;
    int count = int(Renderer::DesignWidth / lampPitch) + 3;
    for (int i = -1; i < count; i++) {
        float x = float(i) * lampPitch - std::fmod(mid, lampPitch);
        Rect post { x, horizon - 210.0f, 8.0f, 210.0f };
        r.rect(post, theme::bg2);
        r.glow(Rect { post.centerX() - 34.0f, post.y - 34.0f, 68.0f, 68.0f },
            theme::accentGlow.scaleAlpha(lampGlow), 1.8f);
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

    // Marks on the floor, so the ground is a floor and not a flat colour with
    // things standing on it.
    //
    // One row, sparse, and deliberately unevenly spaced. Two even rows at a
    // 160px and a 240px pitch strobed at 9.4 Hz and 6.2 Hz at full speed and
    // beat against each other, which is a recipe for making somebody ill: a
    // regular pattern crossing the whole field is the strongest cue there is
    // for the illusion of self-motion. A 420px pitch is 3.6 Hz at worst, and
    // jittering each mark by up to half a pitch breaks the periodicity that
    // does the damage - the eye reads texture rather than a metronome.
    constexpr float kPitch = 420.0f;
    float y = horizon + 210.0f;
    int first = int(std::floor(camera / kPitch)) - 1;
    for (int i = first; i < first + 8; i++) {
        // A cheap hash of the mark's own index, so a mark keeps its offset as
        // it crosses instead of jittering while it moves.
        uint32_t h = uint32_t(i * 2654435761u);
        float jitter = float(h % 211u);
        float w = 44.0f + float((h >> 8) % 40u);
        float x = float(i) * kPitch + jitter - camera;
        r.rect(Rect { x, y, w, 4.0f }, theme::bg2);
    }
}

} // namespace nxp::ui
