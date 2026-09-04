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
//
// The lamps are arguments because the two games move past them at very
// different speeds. The defaults are the race's - a post every 340px with a
// full bloom, which at its pace crosses the screen well under once a second.
// The dash goes three times faster, where the same lamps swept a bright
// pattern across the whole field 2.4 times a second and made people ill, so it
// asks for them sparser and dimmer.
//
// A pitch of zero draws no lamps at all: the lantern wheel wants the plaza's
// sky and hills behind it and nothing else, because a row of lit posts behind
// a ring of lit lanterns is one glowing circle too many.
void plazaBackdrop(Renderer& r, float camera, float horizon, float lampPitch = 340.0f,
    float lampGlow = 0.35f);

// The ground the runners are on: a flat band with stripes that run at full
// camera speed, which is what actually reads as speed.
void plazaGround(Renderer& r, float camera, float horizon);

} // namespace nxp::ui
