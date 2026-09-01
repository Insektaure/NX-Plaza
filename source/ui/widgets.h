#pragma once

#include "gfx/renderer.h"
#include "ui/theme.h"

#include <string>
#include <utility>

namespace nxp::ui {

// Icons are drawn from primitives rather than an icon font: the six shapes the
// interface needs are cheaper to draw than a Lucide atlas is to ship.
enum class Icon {
    Inbox,
    Radar,
    Grid,
    Person,
    Sliders,
    Check,
    Chevron,
    ArrowLeft,
    ArrowRight,
    Block,
    Plus,
    Shield,
    Bell,
    Sun,
    Monitor,
    Trash,
    Info,
    Crowd,
    Star,
    Puzzle,
};

void icon(Renderer& r, const Rect& box, Icon which, Color color, float weight = 2.5f);

// Small uppercase label with wide tracking, e.g. "THE PLAZA - TODAY".
void eyebrow(Renderer& r, const Rect& box, const std::string& text,
    Color color = theme::accent);

// A card surface. `focus` in 0..1 fades the amber ring in.
//
// `topAccent` draws a colour band along the top edge (a pass's card theme).
// It is part of the card rather than the caller's job because the band has to
// be drawn *under* the surface to get its corners right: a 6px bar cannot
// carry an 18px corner radius, since the shader clamps a radius to half the
// shape's height, and drawing it directly leaves square corners poking out
// past the card's rounded ones. A fully transparent accent means none.
void card(Renderer& r, const Rect& box, float focus = 0.0f,
    Color fill = theme::bg2, float radius = theme::r4,
    Color topAccent = Color { 0.0f, 0.0f, 0.0f, 0.0f }, float accentThickness = 6.0f);

// The focus ring on its own, for rows that are not full cards.
//
// The ring is drawn *outside* `box`: a dark gap that separates it from the
// element, then the accent. So the tint has to contrast with whatever is
// behind the element, not with the element itself -- tinting it to match a
// filled button paints the ring in the page's own colour and it disappears.
// There is one focus ring in the design and it is the accent; `tint` exists
// for a surface that is not one of the app's, and almost nothing needs it.
void focusRing(Renderer& r, const Rect& box, float radius, float focus,
    Color tint = theme::accent);

// Rounded chip with centred text.
void pill(Renderer& r, const Rect& box, const std::string& text, Color fg, Color bg,
    float textSize = theme::textSm);

// The teal "new" flag from the inbox rows.
void newFlag(Renderer& r, float x, float y);

// A big number over a caption, as used for "1,284 / people met".
void statBlock(Renderer& r, const Rect& box, const std::string& value,
    const std::string& caption, Color valueColor = theme::fg1);

// A button prompt: the letter in a circle plus its action.
// Returns the width consumed so a row of hints can be laid out.
float buttonHint(Renderer& r, float x, float y, const char* letter,
    const std::string& label);

// Row of hints right-aligned inside `box`.
void buttonHints(Renderer& r, const Rect& box,
    const std::pair<const char*, std::string>* hints, int count);

// Where a figure stands on its stage, and how big.
// An inbox tile crops tighter than the encounter's
// full-height portrait - so they are arguments.
struct StageFigure {
    float shoulderWidth = 0.52f;  // fraction of the stage width
    float shoulderHeight = 0.56f; // fraction of the stage height
    float headWidth = 0.30f;
    float headBottom = 0.50f;  // the head's base, as a fraction from the floor
    // How far the figure stands off the stage's floor, as a fraction of the
    // height. The passport card lifts its figure so the name and greeting have
    // room below it - "bottom:230px".
    float floorOffset = 0.0f;
    // How much of the stage to keep clear above the head, as a fraction of the
    // height. A stage with a raised floor has less room than its height
    // suggests, and the figure is sized to what is left rather than allowed to
    // run off the top.
    //
    // It is not only the head that needs the room. Hair reaches above the skull
    // and a tall hat reaches further still - on a clipped stage they are the
    // first things sliced off, and a hat with its crown cut flat reads as a
    // bug. Roughly 0.17 clears every hairstyle and every hat in the catalogue.
    float topMargin = 0.0f;
};

// The two ends of a card theme's stage gradient, derived from its hue.
void stageGradient(uint32_t cardTheme, Color& top, Color& bottom);

// A statistic in its own surface: a display-weight number over a caption, on
// bg-1 with the card radius.
void statCard(Renderer& r, const Rect& box, const std::string& value,
    const std::string& caption, float valueSize = theme::textXl);

// One of the encounter screen's "carrying" chips: a tinted pill with a dot and
// a label. Returns the width it needs, so a row of them can be wrapped.
float chipWidth(Renderer& r, const std::string& label, float textSize = theme::textSm);
void chip(Renderer& r, const Rect& box, const std::string& label, Color tint);

// A bottom-of-screen action. `filled` is the design's primary (accent fill,
// ink text); otherwise it is an outline in stroke-3.
void actionButton(Renderer& r, const Rect& box, const std::string& label, bool filled,
    float focus);
float actionButtonWidth(Renderer& r, const std::string& label);

// A veil: the gradient the mockups lay over a portrait so it fades into the
// content beside or below it.
void veilRight(Renderer& r, const Rect& box, Color into);
void veilBottom(Renderer& r, const Rect& box, Color into,
    float radiusBottom = 0.0f);

// Settings controls.
void toggle(Renderer& r, const Rect& box, bool on, float focus);
// Separate pills, right-aligned in `box`, as the settings artboard draws them.
void segmented(Renderer& r, const Rect& box, const char* const* labels, int count,
    int selected, float focus);
// The width one of those pills needs, so a scene can put a touch zone on it.
float segmentWidth(Renderer& r, const char* label);

// Vertical scrollbar. `fraction` is the scroll position in 0..1, `visible` the
// fraction of content on screen.
void scrollbar(Renderer& r, const Rect& track, float fraction, float visible);

// Thin separator line.
void divider(Renderer& r, float x, float y, float width);

// Formats 1284 as "1,284".
std::string groupedNumber(uint32_t value);

} // namespace nxp::ui
