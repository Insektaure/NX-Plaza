#pragma once

#include "gfx/renderer.h"

namespace nxp::ui {

// The plaza seen side-on, as something to move past.
//
// Sky, hills at a fifth of the speed, lamp posts at half: three layers whose
// only job is to say "moving" while the figures in front of them stay more or
// less where they are. Nothing here is art - a gradient, some ellipses, some
// rectangles with a glow on top.
//
// `camera` is the world x of the left edge of the screen, and `horizon` is
// where the ground begins. Each caller draws its own ground, because a race
// wants lanes and a run wants one strip.
void plazaBackdrop(Renderer& r, float camera, float horizon);

// The ground the runners are on: a flat band with stripes that run at full
// camera speed, which is what actually reads as speed.
void plazaGround(Renderer& r, float camera, float horizon);

} // namespace nxp::ui
