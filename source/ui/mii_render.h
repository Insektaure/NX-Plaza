#pragma once

#include "core/mii.h"
#include "gfx/renderer.h"
#include "ui/widgets.h"

namespace nxp::ui {

// Drawing a Mii.
//
// `miiParts()` must have loaded before any of this draws anything; a face on an
// unloaded atlas draws nothing rather than drawing wrong.

// Just the head, fitted inside `box`. `flat`, when given, replaces every
// colour the face would use, which is how a silhouette is drawn.
void miiHead(Renderer& r, const Rect& box, const Mii& mii, float opacity = 1.0f,
    const Color* flat = nullptr);

// The box to hand miiHead() when the whole head has to stay inside `box`.
//
// miiHead fits the *skull* to what it is given and draws everything above
// the skull above that, so hair reaches out of the top of the box and a
// tall hat reaches further still. On an open background nobody notices; in
// a card it looks like the Mii is climbing out. This keeps the top fifth
// clear and sits the smaller face on the same floor - the same margin
// StageFigure::topMargin exists for, for the same reason.
Rect headroom(const Rect& box);

// Head and shoulders, standing on the bottom edge of `box`.
void miiFigure(Renderer& r, const Rect& box, const Mii& mii, float opacity = 1.0f,
    bool spotlight = false, const Color* flat = nullptr);

// The same figure, every part in one colour.
//
// Drawn a little larger behind a figure it gives a rim that follows the Mii's
// own outline - hair, shoulders and all - rather than a rectangle around it.
void miiSilhouette(Renderer& r, const Rect& box, const Mii& mii, Color colour);

// A Mii on a card theme's tinted stage: the inbox tiles, the encounter
// portrait, the passport card.
// A clip is rectangular, so a stage that fills a rounded card has to round its
// own background: otherwise the gradient squares off the card's corners and
// bleeds out past them. `radiusTop` and `radiusBottom` are separate because a
// stage often fills only the top of a card.
void miiStage(Renderer& r, const Rect& stage, const Mii& mii, uint32_t cardTheme,
    const StageFigure& shape = StageFigure {}, float radiusTop = 0.0f,
    float radiusBottom = 0.0f);

// The part palettes, also used by the editor's swatches.
Color miiSkin(uint8_t tone);
Color miiHair(uint8_t colour);
Color miiEye(uint8_t colour);
Color miiMouth(uint8_t colour);
Color miiGlasses(uint8_t colour);
Color miiFavourite(uint8_t colour);
Color miiShirt(const Mii& mii);

} // namespace nxp::ui
