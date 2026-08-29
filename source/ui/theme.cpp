#include "ui/theme.h"

#include <switch.h>

namespace nxp::theme {

namespace {

const Palette kDark = {
    /* bg0   */ Color::hex(0x08080A),
    /* bg1   */ Color::hex(0x0F0F12),
    /* bg2   */ Color::hex(0x17171C),
    /* bg3   */ Color::hex(0x1F1F26),
    /* bg4   */ Color::hex(0x2A2A33),
    /* scrim */ Color::hex(0x08080A, 0.72f),

    /* plazaTop    */ Color::hex(0x1A1520),
    /* plazaMid    */ Color::hex(0x121016),
    /* plazaBottom */ Color::hex(0x08080A),

    /* stroke1 */ Color::hex(0xFFFFFF, 0.06f),
    /* stroke2 */ Color::hex(0xFFFFFF, 0.10f),
    /* stroke3 */ Color::hex(0xFFFFFF, 0.18f),

    /* fg1 */ Color::hex(0xFAFAF7),
    /* fg2 */ Color::hex(0xB6B6BD),
    /* fg3 */ Color::hex(0x7C7C85),
    /* fg4 */ Color::hex(0x4D4D55),

    /* accent     */ Color::hex(0xF5A524),
    /* accentHi   */ Color::hex(0xFFB938),
    /* accentLo   */ Color::hex(0xD88E14),
    /* accentTint */ Color::hex(0xF5A524, 0.14f),
    /* accentGlow */ Color::hex(0xF5A524, 0.55f),
    /* mark       */ Color::hex(0xF5A524),
    /* accentSoft */ Color::hex(0xFFE7B0),

    /* teal       */ Color::hex(0x4AD3C8),
    /* tealTint   */ Color::hex(0x4AD3C8, 0.14f),
    /* success    */ Color::hex(0x36D399),
    /* danger     */ Color::hex(0xF87171),
    /* dangerTint */ Color::hex(0xF87171, 0.14f),
    /* info       */ Color::hex(0x7AAEFF),

    // --shadow-2 at 0.55 would be invisible on a near-black canvas; this is
    // enough to seat a sheet without turning into a smear.
    /* shadow */ Color::hex(0x000000, 0.45f),

    /* figureSaturation */ 0.14f,
    /* figureBody       */ 0.20f,
    /* figureHead       */ 0.26f,
    /* figureVariation  */ 0.025f, // lighter, i.e. away from the near-black

    // #2A2233, #1D2A2A, #2A2119 and friends.
    /* stageSaturation      */ 0.21f,
    /* stageTopLightness    */ 0.155f,
    /* stageBottomLightness */ 0.105f,

    // Drawn from the design's content-type tints; the names are the app's.
    /* cards */ {
        { "Amber lantern", Color::hex(0xF5A524), Color::hex(0xF5A524, 0.30f) },
        { "Dusk market", Color::hex(0xC084FC), Color::hex(0xC084FC, 0.26f) },
        { "Tidepool", Color::hex(0x4AD3C8), Color::hex(0x4AD3C8, 0.26f) },
        { "Paper lantern", Color::hex(0xFBBF24), Color::hex(0xFBBF24, 0.24f) },
        { "Rose market", Color::hex(0xF472B6), Color::hex(0xF472B6, 0.24f) },
        { "Night express", Color::hex(0x94A3B8), Color::hex(0x94A3B8, 0.24f) },
    },
};

// Not an inversion. The surfaces reverse direction (a raised card is lighter,
// a focused one darker), and every colour that carries text was re-solved by
// walking the dark hue down in lightness - with its saturation capped, so it
// reads as ink rather than neon - until it cleared 4.5:1 against the canvas.
// The greys were chosen to reproduce the dark palette's own contrast
// relationships: 16:1, 8.4:1, 4.8:1, 2.4:1.
const Palette kLight = {
    /* bg0   */ Color::hex(0xF4F3EE),
    /* bg1   */ Color::hex(0xFAF9F5),
    /* bg2   */ Color::hex(0xFFFFFF),
    /* bg3   */ Color::hex(0xF0EEE7),
    /* bg4   */ Color::hex(0xE4E1D8),
    /* scrim */ Color::hex(0x1A1712, 0.46f),

    /* plazaTop    */ Color::hex(0xEBE1D0),
    /* plazaMid    */ Color::hex(0xF2EDE3),
    /* plazaBottom */ Color::hex(0xF4F3EE),

    /* stroke1 */ Color::hex(0x17171C, 0.08f),
    /* stroke2 */ Color::hex(0x17171C, 0.14f),
    /* stroke3 */ Color::hex(0x17171C, 0.22f),

    /* fg1 */ Color::hex(0x17171C),
    /* fg2 */ Color::hex(0x46464F),
    /* fg3 */ Color::hex(0x6B6B74),
    /* fg4 */ Color::hex(0x9E9EA6),

    /* accent     */ Color::hex(0x916318), // 4.74:1 on the canvas
    /* accentHi   */ Color::hex(0xB47D1F),
    /* accentLo   */ Color::hex(0x6E4A12),
    /* accentTint */ Color::hex(0xF5A524, 0.20f),
    /* accentGlow */ Color::hex(0xF5A524, 0.42f),
    /* mark       */ Color::hex(0xF5A524),
    /* accentSoft */ Color::hex(0xFFD98A),

    /* teal       */ Color::hex(0x1D766F),
    /* tealTint   */ Color::hex(0x1D766F, 0.14f),
    /* success    */ Color::hex(0x1B7956),
    /* danger     */ Color::hex(0xD22222),
    /* dangerTint */ Color::hex(0xD22222, 0.10f),
    /* info       */ Color::hex(0x2369D7),

    /* shadow */ Color::hex(0x1A1712, 0.12f),

    /* figureSaturation */ 0.10f,
    /* figureBody       */ 0.62f,
    /* figureHead       */ 0.68f,
    /* figureVariation  */ -0.025f, // darker, i.e. away from the paper

    /* stageSaturation      */ 0.30f,
    /* stageTopLightness    */ 0.90f,
    /* stageBottomLightness */ 0.965f,

    /* cards */ {
        { "Amber lantern", Color::hex(0x9E6C1A), Color::hex(0xF5A524, 0.30f) },
        { "Dusk market", Color::hex(0x9A51E3), Color::hex(0xC084FC, 0.30f) },
        { "Tidepool", Color::hex(0x20837B), Color::hex(0x4AD3C8, 0.30f) },
        { "Paper lantern", Color::hex(0x927018), Color::hex(0xFBBF24, 0.30f) },
        { "Rose market", Color::hex(0xDC2987), Color::hex(0xF472B6, 0.26f) },
        { "Night express", Color::hex(0x647997), Color::hex(0x94A3B8, 0.26f) },
    },
};

// The palette every token below points into. Light by default: the app opens
// in daylight unless the user says otherwise.
Palette g_current = kLight;
Mode g_mode = Mode::Light;

// The console's own theme, for System mode. Falls back to light, matching our
// own default, if the setting cannot be read.
Mode systemMode()
{
    ColorSetId colorSet = ColorSetId_Light;
    if (R_FAILED(setsysGetColorSetId(&colorSet)))
        return Mode::Light;
    return colorSet == ColorSetId_Dark ? Mode::Dark : Mode::Light;
}

} // namespace

// Bound to subobjects of g_current, so overwriting g_current is what makes a
// theme change visible. These are constant-initialised (a reference binding is
// just an address), so no other translation unit can read them too early.
const Color& bg0 = g_current.bg0;
const Color& bg1 = g_current.bg1;
const Color& bg2 = g_current.bg2;
const Color& bg3 = g_current.bg3;
const Color& bg4 = g_current.bg4;
const Color& scrim = g_current.scrim;

const Color& plazaTop = g_current.plazaTop;
const Color& plazaMid = g_current.plazaMid;
const Color& plazaBottom = g_current.plazaBottom;

const Color& stroke1 = g_current.stroke1;
const Color& stroke2 = g_current.stroke2;
const Color& stroke3 = g_current.stroke3;

const Color& fg1 = g_current.fg1;
const Color& fg2 = g_current.fg2;
const Color& fg3 = g_current.fg3;
const Color& fg4 = g_current.fg4;

const Color& accent = g_current.accent;
const Color& accentHi = g_current.accentHi;
const Color& accentLo = g_current.accentLo;
const Color& accentTint = g_current.accentTint;
const Color& accentGlow = g_current.accentGlow;
const Color& mark = g_current.mark;
const Color& accentSoft = g_current.accentSoft;

const Color& teal = g_current.teal;
const Color& tealTint = g_current.tealTint;
const Color& success = g_current.success;
const Color& danger = g_current.danger;
const Color& dangerTint = g_current.dangerTint;
const Color& info = g_current.info;

const Color& shadow = g_current.shadow;

const Palette& palette()
{
    return g_current;
}

const CardTheme& cardTheme(uint32_t index)
{
    return g_current.cards[index % 6];
}

Mode mode()
{
    return g_mode;
}

Mode resolvedMode()
{
    if (g_mode == Mode::System)
        return systemMode();
    return g_mode == Mode::Dark ? Mode::Dark : Mode::Light;
}

void setMode(Mode requested)
{
    if (requested < Mode::Light || requested >= Mode::Count)
        requested = Mode::Light;

    g_mode = requested;
    g_current = resolvedMode() == Mode::Dark ? kDark : kLight;
}

void refreshSystemMode()
{
    if (g_mode == Mode::System)
        setMode(Mode::System);
}

const char* modeName(Mode which)
{
    switch (which) {
    case Mode::Dark:
        return "Dark";
    case Mode::System:
        return "Match console";
    case Mode::Light:
    default:
        return "Light";
    }
}

} // namespace nxp::theme
