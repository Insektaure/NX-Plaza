#include "ui/widgets.h"

#include "core/model.h"
#include "core/util.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nxp::ui {

void icon(Renderer& r, const Rect& box, Icon which, Color color, float weight)
{
    float cx = box.centerX();
    float cy = box.centerY();
    float s = std::min(box.w, box.h);

    switch (which) {
    case Icon::Inbox: {
        // A tray with its lip: the box, then two shoulders sloping in to a slot,
        // which is what makes it read as an inbox and not a text field.
        Rect tray { cx - s * 0.34f, cy - s * 0.28f, s * 0.68f, s * 0.56f };
        r.strokeRect(tray, theme::r1, weight, color);

        int steps = 7;
        for (int i = 0; i < steps; i++) {
            float t = static_cast<float>(i) / static_cast<float>(steps - 1);
            float y = cy - s * 0.04f;
            r.circle(tray.x + t * s * 0.20f, y - (1.0f - t) * s * 0.14f, weight * 0.55f, color);
            r.circle(tray.right() - t * s * 0.20f, y - (1.0f - t) * s * 0.14f, weight * 0.55f,
                color);
        }
        r.rect(Rect { cx - s * 0.14f, cy - s * 0.04f - weight * 0.5f, s * 0.28f, weight },
            color);
        break;
    }
    case Icon::Shield: {
        // Rounded across the top, tapering to a point.
        Rect body { cx - s * 0.28f, cy - s * 0.32f, s * 0.56f, s * 0.5f };
        r.strokeRect(body, s * 0.22f, weight, color);
        int steps = 6;
        for (int i = 0; i < steps; i++) {
            float t = static_cast<float>(i) / static_cast<float>(steps - 1);
            float y = body.bottom() - weight * 0.5f + t * s * 0.16f;
            float half = (1.0f - t) * s * 0.28f;
            r.circle(cx - half, y, weight * 0.55f, color);
            r.circle(cx + half, y, weight * 0.55f, color);
        }
        break;
    }
    case Icon::Bell: {
        float head = s * 0.30f;
        r.strokeRect(Rect { cx - head, cy - s * 0.30f, head * 2.0f, head * 2.0f }, head,
            weight, color);
        r.rect(Rect { cx - head, cy + s * 0.10f - weight * 0.5f, head * 2.0f, weight }, color);
        r.circle(cx, cy + s * 0.24f, weight * 1.4f, color);
        break;
    }
    case Icon::Sun: {
        float radius = s * 0.16f;
        r.strokeRect(Rect { cx - radius, cy - radius, radius * 2.0f, radius * 2.0f }, radius,
            weight, color);
        for (int i = 0; i < 8; i++) {
            float angle = 0.7853981f * static_cast<float>(i);
            float inner = radius + s * 0.06f;
            float outer = radius + s * 0.16f;
            for (int step = 0; step < 3; step++) {
                float t = static_cast<float>(step) / 2.0f;
                float d = inner + t * (outer - inner);
                r.circle(cx + std::cos(angle) * d, cy + std::sin(angle) * d, weight * 0.5f,
                    color);
            }
        }
        break;
    }
    case Icon::Monitor: {
        Rect screen { cx - s * 0.32f, cy - s * 0.28f, s * 0.64f, s * 0.42f };
        r.strokeRect(screen, theme::r1, weight, color);
        r.rect(Rect { cx - weight * 0.5f, screen.bottom(), weight, s * 0.10f }, color);
        r.rect(Rect { cx - s * 0.16f, screen.bottom() + s * 0.10f - weight, s * 0.32f, weight },
            color);
        break;
    }
    case Icon::Puzzle: {
        // A jigsaw piece: a rounded square with a tab on the right and a
        // socket bitten out of the top. The socket is drawn in the background
        // colour rather than subtracted, which is what the primitives here can
        // do - an icon is always over a flat surface, so it reads the same.
        float half = s * 0.30f;
        float knob = half * 0.42f;
        r.strokeRect(Rect { cx - half, cy - half, half * 2.0f, half * 2.0f }, s * 0.08f,
            weight, color);
        r.circle(cx + half, cy - half * 0.15f, knob, color);
        r.circle(cx - half * 0.15f, cy - half, knob, color);
        break;
    }
    case Icon::Bag: {
        // A shopping bag: the body, then a handle arching out of its rim.
        //
        // strokeRect with a radius of half its side is this file's stroked
        // circle, which the handle is the top half of - clipped to above the
        // rim rather than covered over, because an icon has no idea what colour
        // the surface behind it is.
        float halfW = s * 0.28f;
        float top = cy - s * 0.13f; // the handle hangs above it, so the rim sits high
        Rect body { cx - halfW, top, halfW * 2.0f, s * 0.42f };

        float handle = s * 0.17f;
        r.pushClipVertical(Rect { box.x, box.y, box.w, top - box.y });
        r.strokeRect(Rect { cx - handle, top - handle, handle * 2.0f, handle * 2.0f },
            handle, weight, color);
        r.popClip();

        r.strokeRect(body, s * 0.07f, weight, color);
        break;
    }
    case Icon::Trophy: {
        // A cup: the bowl, a handle each side, then the stem and the foot.
        //
        // The handles are halves of a stroked ring centred on the bowl's own
        // edge, clipped to the side that sticks out - so both ends of each arc
        // finish exactly on the bowl's outline and read as joined to it.
        float halfW = s * 0.22f;
        float top = cy - s * 0.26f;
        float bowl = s * 0.30f;
        Rect cupRect { cx - halfW, top, halfW * 2.0f, bowl };

        float ear = s * 0.11f;
        float earY = top + bowl * 0.40f;
        for (int side = 0; side < 2; side++) {
            float edge = side == 0 ? cupRect.x : cupRect.right();
            Rect keep = side == 0 ? Rect { box.x, box.y, edge - box.x, box.h }
                                  : Rect { edge, box.y, box.right() - edge, box.h };
            r.pushClipHorizontal(keep);
            r.strokeRect(Rect { edge - ear, earY - ear, ear * 2.0f, ear * 2.0f }, ear,
                weight, color);
            r.popClip();
        }

        r.strokeRect(cupRect, s * 0.10f, weight, color);

        // Stem and foot, both centred: a cup on a stand rather than a tub.
        r.rect(Rect { cx - weight * 0.5f, cupRect.bottom(), weight, s * 0.16f }, color);
        r.rect(Rect { cx - s * 0.15f, cupRect.bottom() + s * 0.16f, s * 0.30f, weight },
            color);
        break;
    }
    case Icon::Dice: {
        // A die showing three: the body, then pips down the diagonal. Filled
        // discs, because a 3px ring at this size is a smudge.
        float half = s * 0.30f;
        r.strokeRect(Rect { cx - half, cy - half, half * 2.0f, half * 2.0f }, s * 0.09f,
            weight, color);
        float reach = half * 0.52f;
        float pip = std::max(weight * 0.9f, s * 0.045f);
        for (int i = -1; i <= 1; i++)
            r.circle(cx + float(i) * reach, cy + float(i) * reach, pip, color);
        break;
    }
    case Icon::Star: {
        // Ten vertices at alternating radii, stroked edge by edge.
        //
        // Stroked rather than filled, which is what every other icon here is.
        // The first attempt drew triangles from the middle by passing the same
        // point for corners 0 and 3 of a band; band takes those two as the ends
        // of the stroke's *width*, so the length came out zero and it returned
        // without drawing. Nothing appeared at all.
        constexpr float kTau = 6.2831853f;
        float outer = s * 0.40f;
        float inner = outer * 0.46f;

        float px[10], py[10];
        for (int i = 0; i < 10; i++) {
            float a = kTau * float(i) / 10.0f - kTau * 0.25f;
            float rad = (i % 2) == 0 ? outer : inner;
            px[i] = cx + std::cos(a) * rad;
            py[i] = cy + std::sin(a) * rad;
        }

        for (int i = 0; i < 10; i++) {
            int j = (i + 1) % 10;
            float dx = px[j] - px[i];
            float dy = py[j] - py[i];
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-4f)
                continue;
            // Perpendicular, half a stroke wide. Corners 0 and 3 are the two
            // sides at one end, which is the order band wants.
            float nx = -dy / len * weight * 0.5f;
            float ny = dx / len * weight * 0.5f;
            const float corners[8] = { px[i] + nx, py[i] + ny, px[j] + nx, py[j] + ny,
                px[j] - nx, py[j] - ny, px[i] - nx, py[i] - ny };
            r.band(corners, color);
            // The ends are butt-cut, so a disc covers the notch at each point.
            r.circle(px[i], py[i], weight * 0.5f, color);
        }
        break;
    }
    case Icon::Crowd: {
        // Three heads and shoulders, the middle one forward. Read as a group at
        // 36px, which is all a rail icon has to do.
        auto figure = [&](float dx, float dy, float k) {
            r.circle(cx + dx, cy + dy - s * 0.16f, s * 0.11f * k, color);
            r.roundRect(Rect { cx + dx - s * 0.16f * k, cy + dy - s * 0.02f,
                           s * 0.32f * k, s * 0.20f * k },
                s * 0.09f * k, color);
        };
        figure(-s * 0.26f, -s * 0.02f, 0.85f);
        figure(s * 0.26f, -s * 0.02f, 0.85f);
        figure(0.0f, s * 0.06f, 1.0f);
        break;
    }
    case Icon::Info: {
        // A ringed lower-case i: the dot, a gap, then the stem.
        float radius = s * 0.32f;
        r.strokeRect(Rect { cx - radius, cy - radius, radius * 2.0f, radius * 2.0f }, radius,
            weight, color);
        r.circle(cx, cy - s * 0.15f, weight * 0.75f, color);
        r.rect(Rect { cx - weight * 0.5f, cy - s * 0.04f, weight, s * 0.20f }, color);
        break;
    }
    case Icon::Trash: {
        r.rect(Rect { cx - s * 0.26f, cy - s * 0.22f - weight, s * 0.52f, weight }, color);
        r.rect(Rect { cx - s * 0.08f, cy - s * 0.30f, s * 0.16f, weight }, color);
        Rect body { cx - s * 0.20f, cy - s * 0.18f, s * 0.40f, s * 0.42f };
        r.strokeRect(body, theme::r1 * 0.7f, weight, color);
        for (int i = -1; i <= 1; i += 2)
            r.rect(Rect { cx + static_cast<float>(i) * s * 0.07f - weight * 0.5f,
                       body.y + s * 0.08f, weight, body.h - s * 0.16f },
                color);
        break;
    }
    case Icon::Radar: {
        for (int i = 1; i <= 3; i++) {
            float radius = s * 0.12f * static_cast<float>(i);
            r.strokeRect(Rect { cx - radius, cy - radius, radius * 2, radius * 2 },
                radius, weight, color.scaleAlpha(1.0f - 0.18f * (i - 1)));
        }
        r.circle(cx, cy, weight * 1.1f, color);
        break;
    }
    case Icon::Grid: {
        float cell = s * 0.26f;
        float gap = s * 0.10f;
        for (int gy = 0; gy < 2; gy++) {
            for (int gx = 0; gx < 2; gx++) {
                Rect c {
                    cx - cell - gap * 0.5f + (cell + gap) * static_cast<float>(gx),
                    cy - cell - gap * 0.5f + (cell + gap) * static_cast<float>(gy),
                    cell, cell
                };
                r.strokeRect(c, theme::r1 * 0.6f, weight, color);
            }
        }
        break;
    }
    case Icon::Person: {
        float head = s * 0.16f;
        r.strokeRect(Rect { cx - head, cy - s * 0.30f, head * 2, head * 2 }, head, weight, color);
        Rect body { cx - s * 0.26f, cy + s * 0.02f, s * 0.52f, s * 0.34f };
        r.strokeRect(body, s * 0.24f, weight, color);
        break;
    }
    case Icon::Sliders: {
        for (int i = 0; i < 3; i++) {
            float y = cy + (static_cast<float>(i) - 1.0f) * s * 0.22f;
            r.rect(Rect { cx - s * 0.32f, y - weight * 0.5f, s * 0.64f, weight },
                color.scaleAlpha(0.75f));
            float knobX = cx + (i == 1 ? s * 0.14f : -s * 0.10f);
            r.circle(knobX, y, weight * 1.9f, color);
        }
        break;
    }
    case Icon::Check: {
        // Two strokes, drawn as thin rotated-ish bars approximated by steps.
        int steps = 6;
        for (int i = 0; i < steps; i++) {
            float t = static_cast<float>(i) / static_cast<float>(steps - 1);
            float x = cx - s * 0.24f + t * s * 0.20f;
            float y = cy + t * s * 0.20f - s * 0.02f;
            r.circle(x, y, weight * 0.6f, color);
        }
        for (int i = 0; i < steps + 2; i++) {
            float t = static_cast<float>(i) / static_cast<float>(steps + 1);
            float x = cx - s * 0.04f + t * s * 0.30f;
            float y = cy + s * 0.18f - t * s * 0.38f;
            r.circle(x, y, weight * 0.6f, color);
        }
        break;
    }
    case Icon::Chevron: {
        int steps = 5;
        for (int i = 0; i < steps; i++) {
            float t = static_cast<float>(i) / static_cast<float>(steps - 1);
            r.circle(cx - s * 0.06f + t * s * 0.14f, cy - s * 0.16f + t * s * 0.16f,
                weight * 0.6f, color);
            r.circle(cx - s * 0.06f + t * s * 0.14f, cy + s * 0.16f - t * s * 0.16f,
                weight * 0.6f, color);
        }
        break;
    }
    case Icon::ArrowLeft:
    case Icon::ArrowRight: {
        // A shaft with two arms swept back from the point, drawn the same way
        // the chevron is so the two sit together in a row of hints.
        float dir = which == Icon::ArrowLeft ? -1.0f : 1.0f;
        r.rect(Rect { cx - s * 0.20f, cy - weight * 0.5f, s * 0.40f, weight }, color);

        int steps = 5;
        for (int i = 0; i < steps; i++) {
            float t = static_cast<float>(i) / static_cast<float>(steps - 1);
            float ax = cx + dir * (s * 0.20f - t * s * 0.15f);
            float spread = t * s * 0.15f;
            r.circle(ax, cy - spread, weight * 0.6f, color);
            r.circle(ax, cy + spread, weight * 0.6f, color);
        }
        break;
    }
    case Icon::Block: {
        // A prohibition sign: the ring, then a bar across it from upper left to
        // lower right, stopping on the ring's inner edge at both ends.
        float radius = s * 0.32f;
        r.strokeRect(Rect { cx - radius, cy - radius, radius * 2.0f, radius * 2.0f }, radius,
            weight, color);

        float reach = std::max(radius - weight * 0.5f, 0.0f) * 0.7071f;
        int steps = 13;
        for (int i = 0; i < steps; i++) {
            float t = static_cast<float>(i) / static_cast<float>(steps - 1) * 2.0f - 1.0f;
            r.circle(cx + t * reach, cy + t * reach, weight * 0.5f, color);
        }
        break;
    }
    case Icon::Plus: {
        r.rect(Rect { cx - s * 0.22f, cy - weight * 0.5f, s * 0.44f, weight }, color);
        r.rect(Rect { cx - weight * 0.5f, cy - s * 0.22f, weight, s * 0.44f }, color);
        break;
    }
    }
}

void eyebrow(Renderer& r, const Rect& box, const std::string& text, Color color)
{
    TextStyle style;
    style.size = theme::textSm;
    style.weight = FontWeight::Bold;
    style.color = color;
    style.tracking = theme::trackingWider;
    style.uppercase = true;
    r.text(box, text, style, Align::Left, VAlign::Top);
}

void focusRing(Renderer& r, const Rect& box, float radius, float focus, Color tint)
{
    if (focus <= 0.001f)
        return;

    // --focus-ring is `0 0 0 3px bg-0, 0 0 0 6px accent`, so the ring sits
    // *outside* the element: a dark gap that separates it from the surface, then
    // the amber. The token file also calls for a bloom behind it; drawn, it read
    // as a smudge around the selected row rather than as light, so the ring is
    // the line alone.
    //
    // Only the alpha animates. Deriving the geometry from `focus` as well meant
    // a 3px line moved by fractions of a pixel every frame, and against the
    // pixel grid that reads as flicker rather than as a pulse.
    float gap = theme::focusGap;
    float ring = theme::focusRing;

    r.strokeRect(box.inset(-gap), radius + gap, gap, theme::bg0.scaleAlpha(focus));
    r.strokeRect(box.inset(-(gap + ring)), radius + gap + ring, ring, tint.scaleAlpha(focus));
}

// How far a focused surface grows, per edge.
//
// .focusable is `transform: scale(1.06)`, i.e. 3% per edge. Proportional
// growth is right for a card and silly for a 1240px-wide settings row, which
// would visibly widen past its neighbours, so it is capped in pixels.
static Rect grownForFocus(const Rect& box, float focus, float rate)
{
    if (focus <= 0.001f)
        return box;
    float dx = std::min(box.w * rate, 14.0f) * focus;
    float dy = std::min(box.h * rate, 14.0f) * focus;
    return box.inset(-dx, -dy);
}

void card(Renderer& r, const Rect& box, float focus, Color fill, float radius,
    Color topAccent, float accentThickness)
{
    // A focused card lifts towards the viewer, as the design's .focusable does.
    Rect grown = grownForFocus(box, focus, theme::focusGrow);
    if (grown.w != box.w || grown.h != box.h) {
        card(r, grown, 0.0f, fill, radius, topAccent, accentThickness);
        focusRing(r, grown, radius, focus);
        return;
    }

    // A card is separated from what is behind it by its fill and its hairline,
    // not by a cast shadow. The stack of blurred rounded rects that used to sit
    // under every row read as a smudge rather than as lift.
    Color surface = fill.mix(theme::bg3, 0.55f * focus);

    if (topAccent.a > 0.0f) {
        // Tall enough that its corner radius is not clamped, then covered by
        // the surface down to `accentThickness`.
        r.roundRect(Rect { box.x, box.y, box.w, radius * 2.0f }, radius, 0.0f, topAccent);
        r.roundRect(Rect { box.x, box.y + accentThickness, box.w, box.h - accentThickness },
            0.0f, radius, surface);
    } else {
        r.roundRect(box, radius, surface);
    }

    r.strokeRect(box, radius, theme::stroke, theme::stroke1.mix(theme::stroke2, focus));
    focusRing(r, box, radius, focus);
}

void pill(Renderer& r, const Rect& box, const std::string& text, Color fg, Color bg,
    float textSize)
{
    r.roundRect(box, box.h * 0.5f, bg);
    TextStyle style;
    style.size = textSize;
    style.weight = FontWeight::Medium;
    style.color = fg;
    r.text(box, text, style, Align::Center, VAlign::Middle);
}

void newFlag(Renderer& r, float x, float y)
{
    Rect box { x, y, 88.0f, 36.0f };
    pill(r, box, "new", theme::bg0, theme::teal, theme::textXs);
}

void statBlock(Renderer& r, const Rect& box, const std::string& value,
    const std::string& caption, Color valueColor)
{
    TextStyle big;
    big.size = theme::textXl;
    big.weight = FontWeight::Bold;
    big.color = valueColor;
    big.tracking = theme::trackingTight;

    TextStyle small;
    small.size = theme::textSm;
    small.color = theme::fg3;

    r.text(box.x, box.y, value, big);
    r.text(box.x, box.y + theme::textXl * theme::leadingSnug, caption, small);
}

// A hint whose button is a direction is drawn as an arrow rather than spelled
// out: "Left" does not fit in a button circle, and an arrow is what the thing
// under the thumb actually looks like.
static bool directionIcon(const char* letter, Icon& out)
{
    if (std::strcmp(letter, "Left") == 0) {
        out = Icon::ArrowLeft;
        return true;
    }
    if (std::strcmp(letter, "Right") == 0) {
        out = Icon::ArrowRight;
        return true;
    }
    return false;
}

float buttonHint(Renderer& r, float x, float y, const char* letter,
    const std::string& label)
{
    float radius = 21.0f;
    float cy = y + radius;

    r.circle(x + radius, cy, radius, theme::bg4);
    r.strokeRect(Rect { x, y, radius * 2, radius * 2 }, radius, theme::stroke, theme::stroke3);

    Rect face { x, y, radius * 2, radius * 2 };
    Icon arrow;
    if (directionIcon(letter, arrow)) {
        icon(r, face, arrow, theme::fg1, 3.0f);
    } else {
        TextStyle glyph;
        glyph.size = theme::textSm;
        glyph.weight = FontWeight::Bold;
        glyph.color = theme::fg1;

        // The circle is sized for one character. "L/R" and "ZL" are not one
        // character, so anything wider is stepped down until it fits instead of
        // spilling over the rim.
        float room = radius * 2.0f - 12.0f;
        float width = r.measure(letter, glyph);
        if (width > room && width > 0.0f)
            glyph.size *= room / width;

        r.text(face, letter, glyph, Align::Center, VAlign::Middle);
    }

    TextStyle text;
    text.size = theme::textSm;
    text.color = theme::fg3;
    float labelX = x + radius * 2 + theme::s2;
    float width = r.text(Rect { labelX, y, 400.0f, radius * 2 }, label, text,
        Align::Left, VAlign::Middle);

    return radius * 2 + theme::s2 + width;
}

void buttonHints(Renderer& r, const Rect& box,
    const std::pair<const char*, std::string>* hints, int count)
{
    // Measure first so the row can be flushed to the right edge.
    TextStyle text;
    text.size = theme::textSm;

    float total = 0.0f;
    for (int i = 0; i < count; i++)
        total += 42.0f + theme::s2 + r.measure(hints[i].second, text) + theme::s6;
    if (count > 0)
        total -= theme::s6;

    float x = box.right() - total;
    float y = box.y + (box.h - 42.0f) * 0.5f;
    for (int i = 0; i < count; i++)
        x += buttonHint(r, x, y, hints[i].first, hints[i].second) + theme::s6;
}

void stageGradient(uint32_t cardTheme, Color& top, Color& bottom)
{
    const theme::Palette& p = theme::palette();
    const theme::CardTheme& card = theme::cardTheme(cardTheme);

    // Hue of the theme's tint, so every pass gets a stage in its own colour.
    float maxC = std::max(card.tint.r, std::max(card.tint.g, card.tint.b));
    float minC = std::min(card.tint.r, std::min(card.tint.g, card.tint.b));
    float hue = 0.0f;
    if (maxC > minC) {
        float d = maxC - minC;
        if (maxC == card.tint.r)
            hue = 60.0f * std::fmod((card.tint.g - card.tint.b) / d, 6.0f);
        else if (maxC == card.tint.g)
            hue = 60.0f * ((card.tint.b - card.tint.r) / d + 2.0f);
        else
            hue = 60.0f * ((card.tint.r - card.tint.g) / d + 4.0f);
    }

    top = Color::hsl(hue, p.stageSaturation, p.stageTopLightness);
    bottom = Color::hsl(hue, p.stageSaturation * 0.8f, p.stageBottomLightness);
}

void statCard(Renderer& r, const Rect& box, const std::string& value,
    const std::string& caption, float valueSize)
{
    r.roundRect(box, theme::r3, theme::bg1);

    TextStyle number;
    number.size = valueSize;
    number.weight = FontWeight::Bold;
    number.color = theme::fg1;
    number.tracking = theme::trackingTight;

    TextStyle label;
    label.size = theme::textXs;
    label.color = theme::fg3;

    Rect inner = box.inset(theme::s5);
    r.text(inner.x, inner.y, r.ellipsize(value, number, inner.w), number);
    r.text(inner.x, inner.y + valueSize * theme::leadingSnug + 6.0f,
        r.ellipsize(caption, label, inner.w), label);
}

float chipWidth(Renderer& r, const std::string& label, float textSize)
{
    TextStyle style;
    style.size = textSize;
    style.weight = FontWeight::Medium;
    // padding 10px 18px, plus the dot and its gap
    return 18.0f + 12.0f + 10.0f + r.measure(label, style) + 18.0f;
}

void chip(Renderer& r, const Rect& box, const std::string& label, Color tint)
{
    r.roundRect(box, theme::r1, tint.withAlpha(0.12f));

    float dotX = box.x + 18.0f + 6.0f;
    r.circle(dotX, box.centerY(), 6.0f, tint);

    TextStyle style;
    style.size = theme::textSm;
    style.weight = FontWeight::Medium;
    style.color = tint;

    float textX = dotX + 6.0f + 10.0f;
    r.text(Rect { textX, box.y, box.right() - textX - 18.0f, box.h },
        r.ellipsize(label, style, box.right() - textX - 18.0f), style, Align::Left,
        VAlign::Middle);
}

float actionButtonWidth(Renderer& r, const std::string& label)
{
    TextStyle style;
    style.size = theme::textBase;
    style.weight = FontWeight::Bold;
    return 40.0f + r.measure(label, style) + 40.0f; // padding 20px 40px
}

void actionButton(Renderer& r, const Rect& box, const std::string& label, bool filled,
    float focus)
{
    if (filled) {
        r.roundRect(box, theme::r2, theme::accent);
    } else {
        r.roundRect(box, theme::r2, theme::bg1);
        r.strokeRect(box, theme::r2, theme::stroke, theme::stroke3);
    }

    TextStyle style;
    style.size = theme::textBase;
    style.weight = filled ? FontWeight::Bold : FontWeight::Medium;
    style.color = filled ? theme::bg0 : theme::fg1;
    r.text(box, label, style, Align::Center, VAlign::Middle);

    // The ring sits outside the button, on the page, so a filled button needs
    // no special tint: tinting it bg0 drew the ring in the background's own
    // colour and the focused primary action had no visible cursor at all.
    focusRing(r, box, theme::r2, focus);
}

void veilRight(Renderer& r, const Rect& box, Color into)
{
    // linear-gradient(90deg, transparent 55%, rgba(bg0,.9) 92%, bg0 100%).
    // Two bands: the long ramp, then the last stretch that goes fully opaque.
    float rampStart = box.w * 0.55f;
    float rampEnd = box.w * 0.92f;
    r.gradientRectH(Rect { box.x + rampStart, box.y, rampEnd - rampStart, box.h },
        into.withAlpha(0.0f), into.withAlpha(0.9f));
    r.gradientRectH(Rect { box.x + rampEnd, box.y, box.w - rampEnd, box.h },
        into.withAlpha(0.9f), into);
}

void veilBottom(Renderer& r, const Rect& box, Color into, float radiusBottom)
{
    // The veil reaches the bottom of the card it sits on, so it has to respect
    // that card's corners the same way the stage behind it does.
    r.gradientRect(box, into.withAlpha(0.0f), into, 0.0f, radiusBottom);
}

void toggle(Renderer& r, const Rect& box, bool on, float focus)
{
    // "width:96px;height:52px;border-radius:pill" with a 42px knob inset 5px.
    Rect track { box.right() - 96.0f, box.centerY() - 26.0f, 96.0f, 52.0f };

    r.roundRect(track, track.h * 0.5f, on ? theme::accent : theme::bg4);

    float knob = 42.0f;
    float knobX = on ? track.right() - 5.0f - knob * 0.5f : track.x + 5.0f + knob * 0.5f;
    r.circle(knobX, track.centerY(), knob * 0.5f, on ? theme::bg0 : theme::fg3);

    // An amber ring inside an amber track would not be visible.
    focusRing(r, track, track.h * 0.5f, focus);
}

float segmentWidth(Renderer& r, const char* label)
{
    TextStyle style;
    style.size = theme::textSm;
    style.weight = FontWeight::Bold;
    return 22.0f + r.measure(label, style) + 22.0f; // padding 12px 22px
}

void segmented(Renderer& r, const Rect& box, const char* const* labels, int count,
    int selected, float focus)
{
    // Three separate pills, gap 10: "padding:12px 22px;border-radius:radius-2",
    // the chosen one filled with accent over ink, the rest bg-4 over fg-4.
    float gap = 10.0f;
    float total = 0.0f;
    for (int i = 0; i < count; i++)
        total += segmentWidth(r, labels[i]) + (i ? gap : 0.0f);

    float x = box.right() - total;
    float height = std::min(box.h, theme::textSm * theme::leadingNormal + 24.0f);
    float y = box.centerY() - height * 0.5f;

    for (int i = 0; i < count; i++) {
        float width = segmentWidth(r, labels[i]);
        Rect pill { x, y, width, height };
        bool chosen = i == selected;

        r.roundRect(pill, theme::r2, chosen ? theme::accent : theme::bg4);

        TextStyle style;
        style.size = theme::textSm;
        style.weight = chosen ? FontWeight::Bold : FontWeight::Regular;
        style.color = chosen ? theme::bg0 : theme::fg4;
        r.text(pill, labels[i], style, Align::Center, VAlign::Middle);

        if (chosen)
            focusRing(r, pill, theme::r2, focus);

        x += width + gap;
    }
}

void scrollbar(Renderer& r, const Rect& track, float fraction, float visible)
{
    if (visible >= 1.0f)
        return;

    r.roundRect(track, track.w * 0.5f, theme::stroke1);

    float thumbHeight = std::max(track.h * visible, 64.0f);
    float travel = track.h - thumbHeight;
    Rect thumb { track.x, track.y + travel * std::min(std::max(fraction, 0.0f), 1.0f),
        track.w, thumbHeight };
    r.roundRect(thumb, track.w * 0.5f, theme::fg4);
}

void divider(Renderer& r, float x, float y, float width)
{
    r.rect(Rect { x, y, width, theme::stroke }, theme::stroke1);
}

std::string groupedNumber(uint32_t value)
{
    std::string digits = format("%u", value);
    std::string out;
    int count = 0;
    for (size_t i = digits.size(); i > 0; i--) {
        out.insert(out.begin(), digits[i - 1]);
        if (++count % 3 == 0 && i > 1)
            out.insert(out.begin(), ',');
    }
    return out;
}

} // namespace nxp::ui
