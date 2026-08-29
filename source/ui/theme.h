#pragma once

#include "gfx/types.h"

#include <cstdint>

namespace nxp::theme {

enum class Mode : int {
    Light = 0,   // the default
    Dark = 1,
    System = 2,  // follow the console's own light/dark setting
    Count
};

// A pass's card theme
struct CardTheme {
    const char* name;
    Color tint;
    Color glow;
};

// Every colour the interface can draw, plus the few scalars that move with it.
struct Palette {
    // Surfaces. In the dark palette these run from the canvas upwards, exactly
    // as the token file describes them; in light they run the other way, since
    // a raised surface on paper is lighter and a focused one is darker.
    Color bg0;   // canvas, behind everything
    Color bg1;   // default surface
    Color bg2;   // raised card / row
    Color bg3;   // hovered surface
    Color bg4;   // focused surface, top sheet
    Color scrim; // --bg-overlay, the modal veil

    // The plaza's hero band
    Color plazaTop;
    Color plazaMid;
    Color plazaBottom;

    Color stroke1; // hairline
    Color stroke2; // divider
    Color stroke3; // button border

    Color fg1; // primary text, slightly warm white
    Color fg2; // secondary text, meta
    Color fg3; // tertiary, timestamps
    Color fg4; // disabled, hairline labels

    // The amber "lumen". `accent` is the one that carries text and icons, so in
    // the light palette it is a darker amber, solved against the canvas rather
    // than eyeballed. Anything light by nature - the mark, the glows, the
    // lantern's core - must not use it, or it turns muddy brown on paper.
    Color accent;
    Color accentHi;   // hover / focus
    Color accentLo;   // pressed
    Color accentTint; // subtle wash
    Color accentGlow; // the warm light itself: spotlights and lanterns
    Color mark;       // the app's lantern: true amber in both themes
    Color accentSoft; // the lantern's lit core

    Color teal; // used sparingly for live / new / progress
    Color tealTint;
    Color success;
    Color danger;
    Color dangerTint;
    Color info;

    Color shadow; // --shadow-2. Transcribed for completeness; the
                  // interface separates surfaces with hairlines instead.

    // Procedural portraits
    float figureSaturation;
    float figureBody;
    float figureHead;
    float figureVariation;

    // A pass's "photo" stage: the tinted gradient behind a silhouette, as on
    // the inbox tiles and the passport card.
    float stageSaturation;
    float stageTopLightness;
    float stageBottomLightness;

    CardTheme cards[6];
};

// ------------------------------------------------------------------- colour

extern const Color& bg0;
extern const Color& bg1;
extern const Color& bg2;
extern const Color& bg3;
extern const Color& bg4;
extern const Color& scrim;

extern const Color& plazaTop;
extern const Color& plazaMid;
extern const Color& plazaBottom;

extern const Color& stroke1;
extern const Color& stroke2;
extern const Color& stroke3;

extern const Color& fg1;
extern const Color& fg2;
extern const Color& fg3;
extern const Color& fg4;

extern const Color& accent;
extern const Color& accentHi;
extern const Color& accentLo;
extern const Color& accentTint;
extern const Color& accentGlow;
extern const Color& mark;
extern const Color& accentSoft;

extern const Color& teal;
extern const Color& tealTint;
extern const Color& success;
extern const Color& danger;
extern const Color& dangerTint;
extern const Color& info;

extern const Color& shadow;

// The palette in force, for the handful of places that need its scalars.
const Palette& palette();

const CardTheme& cardTheme(uint32_t index);

// -------------------------------------------------------------------- mode

Mode mode();
Mode resolvedMode(); // only ever Light or Dark
void setMode(Mode mode);

// Re-reads the console's own light/dark setting. Only does anything in System
// mode; call it when the app regains focus, since that is when the system
// setting can have changed behind our back.
void refreshSystemMode();

const char* modeName(Mode mode);

// --------------------------------------------------------------------- type
//
// --text-xs .. --text-4xl. "Min readable size on TV: 24px", so anything below
// --text-base is for metadata only.

constexpr float textXs = 18.0f;
constexpr float textSm = 22.0f;
constexpr float textBase = 26.0f;
constexpr float textMd = 30.0f;
constexpr float textLg = 36.0f;
constexpr float textXl = 48.0f;
constexpr float text2xl = 64.0f;
constexpr float text3xl = 88.0f;
constexpr float text4xl = 128.0f;

// Line height multipliers, applied to the font size.
constexpr float leadingTight = 1.05f;
constexpr float leadingSnug = 1.18f;
constexpr float leadingNormal = 1.4f;
constexpr float leadingLoose = 1.6f;

// Letter spacing in em, so it scales with the size it is used at.
constexpr float trackingTight = -0.025f;
constexpr float trackingNormal = 0.0f;
constexpr float trackingWide = 0.06f;  // small caps labels
constexpr float trackingWider = 0.14f; // eyebrow

// ------------------------------------------------------------------ spacing

constexpr float s1 = 4.0f;
constexpr float s2 = 8.0f;
constexpr float s3 = 12.0f;
constexpr float s4 = 16.0f;
constexpr float s5 = 24.0f;
constexpr float s6 = 32.0f;
constexpr float s7 = 48.0f;
constexpr float s8 = 64.0f;
constexpr float s9 = 96.0f;
constexpr float s10 = 128.0f;

// -------------------------------------------------------------------- shape

constexpr float r0 = 0.0f;
constexpr float r1 = 6.0f;  // chips
constexpr float r2 = 12.0f; // buttons, inputs
constexpr float r3 = 18.0f; // cards
constexpr float r4 = 28.0f; // sheets
constexpr float r5 = 40.0f; // hero
constexpr float pill = 999.0f;

// Hairlines and dividers
constexpr float stroke = 2.0f;

// -------------------------------------------------------------------- focus
//
// --focus-ring is `0 0 0 3px bg-0, 0 0 0 6px accent` plus a bloom, and
// .focusable scales to 1.06. The ring sits *outside* the element: a dark gap,
// then the amber, then the glow.

constexpr float focusGap = 3.0f;
constexpr float focusRing = 3.0f;
constexpr float focusGrow = 0.03f;      // half of the 1.06 scale, per edge
constexpr float focusGrowTight = 0.02f; // .focusable-tight, 1.04

// How far outside its own box a focused surface draws: the scale-up (which
// ui::card caps at 14px) plus the ring.
//
// A clip around a scrolling list has to leave this much room, or the first
// item's ring is shaved off the moment it takes focus - which is exactly what
// happened to the first card in the plaza.
constexpr float focusRoom = 14.0f + focusGap + focusRing;

// ------------------------------------------------------------------- layout

constexpr float railWidth = 112.0f;   // --sidebar-collapsed
constexpr float railExpanded = 360.0f;// --sidebar-expanded
constexpr float edge = 64.0f;         // --safe-edge, TV title-safe
constexpr float edgeTop = 48.0f;      // --safe-edge-top
constexpr float plazaHeight = 520.0f; // the hero band in mockup 1a

} // namespace nxp::theme
