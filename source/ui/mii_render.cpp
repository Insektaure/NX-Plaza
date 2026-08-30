#include "ui/mii_render.h"

#include "core/mii_parts.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>

namespace nxp::ui {

namespace {

// ---------------------------------------------------------------- palettes
//
// The Mii format's own colour tables. Half of why a Mii reads as a Mii is the
// palette: the skins are warm and desaturated, the hair runs black-brown-auburn
// -blond with a single grey, and the eyes are muted enough that a blue one never
// looks like a light bulb.

// In the format's own order, not sorted light to dark. Sorting them was a
// silent deviation: it made this app's skin tone 1 a different colour from the
// format's skin tone 1, which anything reading or writing a real Mii would get
// wrong. The same applies to the hair and eye tables below.
constexpr Color kSkin[MiiPartCounts::skinTone] = {
    Color::hex(0xFFD3AD), Color::hex(0xFEB66B), Color::hex(0xDE7942),
    Color::hex(0xFFAA8C), Color::hex(0xAD5129), Color::hex(0x632C18),
};

constexpr Color kHair[MiiPartCounts::hairColour] = {
    Color::hex(0x000000), Color::hex(0x402010), Color::hex(0x5C180A), Color::hex(0x7C3A14),
    Color::hex(0x787880), Color::hex(0x4E3E11), Color::hex(0x875917), Color::hex(0xD0A049),
};

constexpr Color kEye[MiiPartCounts::eyeColour] = {
    Color::hex(0x000000), Color::hex(0x717372), Color::hex(0x663C2C),
    Color::hex(0x686537), Color::hex(0x4B58A8), Color::hex(0x387059),
};

// The format's own glasses colours.
constexpr Color kGlasses[MiiPartCounts::glassesColour] = {
    Color::hex(0x000000), Color::hex(0x5D391A), Color::hex(0xA01612),
    Color::hex(0x2E3969), Color::hex(0xA4601E), Color::hex(0x766F67),
};

constexpr Color kMouth[MiiPartCounts::mouthColour] = {
    Color::hex(0xD04401), Color::hex(0xF30100), Color::hex(0xFD393A), Color::hex(0xF58862),
    Color::hex(0x1F1D1D),
};

// The Mii format's twelve favourite colours. This is the shirt: a real Mii's
// shirt is its favourite colour, not something derived from its hair.
constexpr Color kFavourite[MiiPartCounts::favouriteColour] = {
    Color::hex(0xD21E14), Color::hex(0xFF6E19), Color::hex(0xFFD820), Color::hex(0x78D220),
    Color::hex(0x007830), Color::hex(0x0A48BC), Color::hex(0x3CAADE), Color::hex(0xF55A7D),
    Color::hex(0x7328AD), Color::hex(0x483818), Color::hex(0xE0E0E0), Color::hex(0x181814),
};

Color shade(Color base, float amount)
{
    Color target = amount < 0.0f ? Color::hex(0xFFFFFF) : Color::hex(0x000000);
    return base.mix(target, std::fabs(amount));
}

// ------------------------------------------------------------------ layout
//
// Where each feature sits on the head, as fractions of the head box. These are
// the only numbers in the file that are a judgement rather than data: the shapes
// themselves are the artwork's.

struct Placement {
    float offsetX; // from the centre line, of the head width; 0 = centred
    float centreY; // of the head height
    float width;   // the category's typical width, of the head width
    // Whether the artwork is one side of a pair. An eye and an eyebrow are
    // drawn once per side; a pair of glasses, a nose and a mouth are one part
    // each. This flag is what decides, so the table cannot disagree with the
    // drawing - which is how a whole pair of glasses ended up over each eye.
    bool mirrored;
};

// Indexed by MiiParts::Category. Face, hair and hats are drawn in the
// artwork's own frame instead, so their entries are unused.
constexpr Placement kPlace[MiiParts::CategoryCount] = {
    { 0.000f, 0.500f, 1.000f, false }, // face   -- artwork frame
    { 0.000f, 0.500f, 1.000f, false }, // hair   -- artwork frame
    { 0.189f, 0.536f, 0.215f, true },  // eyes
    { 0.225f, 0.397f, 0.250f, true },  // eyebrows
    { 0.000f, 0.681f, 0.135f, false }, // nose
    { 0.000f, 0.770f, 0.230f, false }, // mouth
    // A pair, bridge and all, in one part: unlike an eye it is drawn once and
    // centred. Mirroring it the way the eyes are mirrored puts a whole pair of
    // glasses over each eye.
    { 0.000f, 0.536f, 0.660f, false }, // glasses
    { 0.000f, 0.700f, 0.300f, false }, // moustache
    { 0.000f, 0.830f, 0.230f, false }, // goatee
    { 0.000f, 0.500f, 1.000f, false }, // hat      -- artwork frame
    { 0.000f, 0.500f, 1.000f, false }, // makeup   -- artwork frame
    { 0.000f, 0.500f, 1.000f, false }, // wrinkles -- artwork frame
};

bool inArtworkFrame(MiiParts::Category c)
{
    // These are all drawn around a head in the source art, so they share the
    // face's transform. Give one its own and it slides off: a fringe lands on
    // the forehead, and - the reason facial hair is here - a full beard gets
    // shrunk to the size of a chin tuft and parked on the chin. The artwork
    // already says how big a beard is and where it sits.
    return c == MiiParts::Face || c == MiiParts::Hair || c == MiiParts::Hat
        || c == MiiParts::Mustache || c == MiiParts::Goatee
        || c == MiiParts::Makeup || c == MiiParts::Wrinkles;
}

// -------------------------------------------------------------------- rig

// Maps the artwork's 52-unit canvas onto a head box on screen.
struct Rig {
    const MiiParts* parts = nullptr;
    const Mii* mii = nullptr;
    Rect box;
    // The chosen face shape's own outline, in artwork units. Everything drawn
    // in the artwork frame is mapped from the head it was authored against
    // onto this one.
    const MiiParts::Part* face = nullptr;
    // Where that outline actually lands on screen. Features are placed against
    // this rather than against the head box: the twelve shapes are not the same
    // size, and a mouth at a fixed fraction of the box creeps toward the chin on
    // a short face and away from it on a tall one.
    Rect faceRect;
    float faceW = 26.0f;
    float faceH = 32.0f;
    Color slot[MiiParts::SlotCount];
    float opacity = 1.0f;
};

void resolveColours(Rig& rig, const Mii& mii, float opacity)
{
    Color skin = kSkin[mii.skinTone % MiiPartCounts::skinTone];
    Color hair = kHair[mii.hairColour % MiiPartCounts::hairColour];
    Color eye = kEye[mii.eyeColour % MiiPartCounts::eyeColour];
    Color mouth = kMouth[mii.mouthColour % MiiPartCounts::mouthColour];
    Color hat = kFavourite[(mii.favouriteColour + mii.headwear)
        % MiiPartCounts::favouriteColour];

    auto put = [&](MiiParts::Slot s, Color c) { rig.slot[s] = c.withAlpha(opacity); };
    put(MiiParts::SlotSkin, skin);
    put(MiiParts::SlotSkinDark, shade(skin, 0.22f));
    put(MiiParts::SlotHair, hair);
    put(MiiParts::SlotHairDark, shade(hair, 0.35f));
    put(MiiParts::SlotEye, eye);
    put(MiiParts::SlotWhite, Color::hex(0xFFFFFF));
    put(MiiParts::SlotDark, Color::hex(0x1A1A1A));
    put(MiiParts::SlotMouth, mouth);
    put(MiiParts::SlotMouthDark, shade(mouth, 0.45f));
    put(MiiParts::SlotFrame, kGlasses[mii.glassesColour % MiiPartCounts::glassesColour]);
    // A lens is not glass here, it is a tint over the skin behind it.
    put(MiiParts::SlotLens, skin.mix(Color::hex(0xFFFFFF), 0.45f));
    put(MiiParts::SlotHat, hat);
    put(MiiParts::SlotHatDark, shade(hat, 0.22f));
    // Blush and shadow. Some of these parts cover a whole cheek or the whole
    // eye socket, and the source draws several of them as gradients that flatten
    // to one flat colour here - at full strength that reads as paint rather
    // than makeup, so it is mixed most of the way back to the skin and left
    // slightly transparent on top of that.
    rig.slot[MiiParts::SlotMakeup]
        = skin.mix(Color::hex(0xD9506B), 0.34f).withAlpha(opacity * 0.72f);
    rig.opacity = opacity;
}

// Artwork point -> screen, for whatever frame this category uses.
//
// Scale is per axis and rotation is about the part's own centre, because the
// Mii format gives every part its own: an eye has a width, a height and a tilt,
// and two faces built from the same parts are told apart by exactly that.
struct Xform {
    float sx = 1.0f, sy = 1.0f;
    float cx = 0.0f, cy = 0.0f;
    float rot = 0.0f;
    bool mirror = false;

    void apply(float u, float v, float& x, float& y) const
    {
        float px = (mirror ? -u : u) * sx;
        float py = v * sy;
        // Mirrored parts turn the other way, so a pair of eyes tilts outward
        // rather than both leaning the same direction.
        float angle = mirror ? -rot : rot;
        float c = std::cos(angle);
        float s = std::sin(angle);
        x = cx + px * c - py * s;
        y = cy + px * s + py * c;
    }
};

// A placement step as a multiplier, an angle, or a fraction of the head.
float stepScale(int8_t v) { return 1.0f + static_cast<float>(v) * 0.045f; }
float stepAngle(int8_t v) { return static_cast<float>(v) * 0.05f; }
float stepShift(int8_t v) { return static_cast<float>(v) * 0.009f; }

Xform makeXform(const Rig& rig, MiiParts::Category cat, int index, bool mirror)
{
    Xform t {};
    t.mirror = mirror;

    if (inArtworkFrame(cat)) {
        // Every one of these was drawn around a head, and the twelve face shapes
        // are not the same head: the chin sits over four canvas units lower on
        // the longest than on the shortest, which is an eighth of the face. Map
        // the head the part was authored against onto the head it is going on,
        // and a beard lands on the chin instead of below it.
        t.sx = rig.box.w / rig.faceW;
        t.sy = rig.box.h / rig.faceH;
        t.cx = rig.box.centerX();
        t.cy = rig.box.centerY();

        const MiiParts::Part* part = rig.parts->part(cat, index);
        if (part && part->anchored() && rig.face && rig.face->anchored()) {
            float anchorW = part->ax1 - part->ax0;
            float anchorH = part->ay1 - part->ay0;
            if (anchorW > 0.01f && anchorH > 0.01f) {
                t.sx *= (rig.face->ax1 - rig.face->ax0) / anchorW;
                t.sy *= (rig.face->ay1 - rig.face->ay0) / anchorH;
                // The part is measured from the canvas centre, so the offset is
                // between the two heads' centres, in the head's own scale.
                t.cx += (rig.box.w / rig.faceW)
                    * ((rig.face->ax0 + rig.face->ax1) - (part->ax0 + part->ax1)) * 0.5f;
                t.cy += (rig.box.h / rig.faceH)
                    * ((rig.face->ay0 + rig.face->ay1) - (part->ay0 + part->ay1)) * 0.5f;
            }
        }

        // Facial hair is the only thing in this frame that is adjustable.
        if (cat == MiiParts::Mustache || cat == MiiParts::Goatee) {
            bool beard = cat == MiiParts::Goatee;
            float lift = beard ? stepShift(rig.mii->beardHeight)
                               : stepShift(rig.mii->mustacheHeight);
            float grow = beard ? stepScale(rig.mii->beardScale)
                               : stepScale(rig.mii->mustacheScale);

            // Scale about the part's own centre, not the head's. Everything in
            // this frame is measured from the canvas centre, so a plain multiply
            // would swing whiskers away from the face as they grow - a beard at
            // full size would slide a tenth of a head down the chin.
            if (part) {
                float ownY = part->midY() - 26.0f;
                t.cy += ownY * (1.0f - grow) * t.sy;
            }
            t.cy += rig.box.h * lift;
            t.sx *= grow;
            t.sy *= grow;
        }
        return t;
    }

    const Placement& p = kPlace[cat];
    float median = rig.parts->medianWidth(cat);

    // The face's own placement, on top of the layout. These are the editor's
    // spacing and height rows: a face where every feature sits on the same
    // grid is a face that looks like everyone else's.
    const Mii& mii = *rig.mii;
    float offsetX = p.offsetX;
    float centreY = p.centreY;
    float growX = 1.0f;
    float growY = 1.0f;

    switch (cat) {
    case MiiParts::Eyes:
        offsetX += stepShift(mii.eyeSpacing);
        centreY += stepShift(mii.eyeHeight);
        growX = stepScale(mii.eyeScale);
        growY = stepScale(mii.eyeScaleY);
        t.rot = stepAngle(mii.eyeRotate);
        break;
    case MiiParts::Eyebrows:
        // Brows ride the eye line, then take their own spacing and height on
        // top of it, which is what the format's separate fields mean.
        offsetX += stepShift(mii.eyeSpacing) + stepShift(mii.browSpacing);
        centreY += stepShift(mii.eyeHeight) + stepShift(mii.browHeight);
        growX = stepScale(mii.browScale);
        growY = stepScale(mii.browScaleY);
        t.rot = stepAngle(mii.browRotate);
        break;
    case MiiParts::Glasses:
        // Glasses sit on the eyes, so they follow them, and take their own
        // height and size on top.
        centreY += stepShift(mii.eyeHeight) + stepShift(mii.glassesHeight);
        growX = growY = stepScale(mii.glassesScale);
        break;
    case MiiParts::Nose:
        centreY += stepShift(mii.noseHeight);
        growX = growY = stepScale(mii.noseScale);
        break;
    case MiiParts::Mouth:
        centreY += stepShift(mii.mouthHeight);
        growX = stepScale(mii.mouthScale);
        growY = stepScale(mii.mouthScaleY);
        break;
    default:
        break;
    }

    const Rect& face = rig.faceRect;
    float base = face.w * p.width / median;
    t.sx = base * growX;
    t.sy = base * growY;
    t.cx = face.centerX() + face.w * (mirror ? -offsetX : offsetX);
    t.cy = face.y + face.h * centreY;
    return t;
}

// Artwork coordinates of the point a part is positioned by.
void partOrigin(MiiParts::Category cat, const MiiParts::Part& part, float& ox, float& oy)
{
    if (inArtworkFrame(cat)) {
        ox = 26.0f; // the canvas centre: the frame the head is drawn in
        oy = 26.0f;
    } else {
        ox = part.midX();
        oy = part.midY();
    }
}

// A part's colour for one of its slots. Artwork from a newer baker could name
// a slot this build has no colour for; those draw dark rather than reading off
// the end of the table.
Color slotColour(const Rig& rig, uint8_t slot)
{
    return rig.slot[slot < MiiParts::SlotCount ? slot
                                               : uint8_t { MiiParts::SlotDark }];
}

void drawPart(Renderer& r, const Rig& rig, MiiParts::Category cat, int index,
    bool mirror, bool back);

// One of the face's features, mirrored or not according to its artwork.
void drawFeature(Renderer& r, const Rig& rig, MiiParts::Category cat, int index)
{
    drawPart(r, rig, cat, index, false, false);
    if (kPlace[cat].mirrored)
        drawPart(r, rig, cat, index, true, false);
}

void drawPart(Renderer& r, const Rig& rig, MiiParts::Category cat, int index,
    bool mirror, bool back)
{
    const MiiParts::Part* part = rig.parts->part(cat, index);
    if (!part)
        return;

    Xform t = makeXform(rig, cat, index, mirror);
    float ox, oy;
    partOrigin(cat, *part, ox, oy);

    uint16_t q0 = back ? 0 : part->backQuads;
    uint16_t q1 = back ? part->backQuads : part->quadCount;
    uint16_t d0 = back ? 0 : part->backDiscs;
    uint16_t d1 = back ? part->backDiscs : part->discCount;

    constexpr float kUnit = MiiParts::Unit;

    for (uint16_t i = q0; i < q1; i++) {
        const MiiParts::Quad& q = part->quads[i];
        float xs[4], ys[4];
        for (int k = 0; k < 4; k++)
            t.apply(q.xy[k * 2] / kUnit - ox, q.xy[k * 2 + 1] / kUnit - oy, xs[k], ys[k]);
        Color colour = slotColour(rig, q.slot);

        // A rotated part's bands are no longer horizontal, so they have to go
        // through the general quad path rather than the trapezoid one.
        if (q.kind == MiiParts::Trapezoid && std::fabs(t.rot) < 1e-4f) {
            // Corners are top-left, top-right, bottom-right, bottom-left in the
            // artwork. Mirroring swaps left for right, so order them here rather
            // than trusting the sign.
            float top = ys[0];
            float bottom = ys[2];
            float tl = std::min(xs[0], xs[1]);
            float tr = std::max(xs[0], xs[1]);
            float bl = std::min(xs[3], xs[2]);
            float br = std::max(xs[3], xs[2]);
            if (bottom < top) {
                std::swap(top, bottom);
                std::swap(tl, bl);
                std::swap(tr, br);
            }
            r.trapezoid(top, tl, tr, bottom, bl, br, colour);
        } else {
            const float corners[8] = { xs[0], ys[0], xs[1], ys[1],
                xs[2], ys[2], xs[3], ys[3] };
            r.band(corners, colour);
        }
    }

    for (uint16_t i = d0; i < d1; i++) {
        const MiiParts::Disc& d = part->discs[i];
        float cx, cy;
        t.apply(d.cx / kUnit - ox, d.cy / kUnit - oy, cx, cy);
        float rx = std::fabs(t.sx) * (d.r / kUnit);
        float ry = std::fabs(t.sy) * (d.r / kUnit);
        // Below half a pixel a join adds nothing the band it caps has not
        // already covered.
        if (rx < 0.5f && ry < 0.5f)
            continue;
        r.ellipse(cx, cy, rx, ry, slotColour(rig, d.slot), t.mirror ? -t.rot : t.rot);
    }
}

// How much of a face is worth drawing at this size.
//
// A radar dot is forty pixels across, which makes an eye about eight. The real
// eye artwork is a hundred primitives - sclera, iris, pupil, lid, lashes -
// and at eight pixels every one of them lands on the same three. Spending that
// on a dozen dots is how a crowd scene runs out of vertex buffer, so the small
// sizes drop what has stopped reading and stand in for the rest.
enum class Detail { Tiny, Small, Full };

Detail detailFor(float headWidth)
{
    if (headWidth < 52.0f)
        return Detail::Tiny;
    if (headWidth < 104.0f)
        return Detail::Small;
    return Detail::Full;
}

// Eyes and a mouth for a face too small to draw them properly: four quads
// instead of two hundred and fifty, and at this size indistinguishable.
void drawTinyFeatures(Renderer& r, const Rig& rig, const Mii& mii)
{
    const Rect& box = rig.box;
    Color dark = rig.slot[MiiParts::SlotDark];

    float eyeR = box.w * 0.052f;
    float eyeY = box.y + box.h * 0.520f;
    for (int side = 0; side < 2; side++) {
        float sign = side == 0 ? -1.0f : 1.0f;
        r.ellipse(box.centerX() + sign * box.w * 0.200f, eyeY, eyeR, eyeR * 1.15f, dark);
    }

    float mouthW = box.w * 0.115f;
    r.roundRect(Rect { box.centerX() - mouthW, box.y + box.h * 0.765f, mouthW * 2.0f,
                    std::max(box.h * 0.018f, 1.0f) },
        box.h * 0.009f, rig.slot[MiiParts::SlotMouth]);
    (void)mii;
}

} // namespace

// ------------------------------------------------------------------ public

Color miiGlasses(uint8_t colour)
{
    return kGlasses[colour % MiiPartCounts::glassesColour];
}

Color miiFavourite(uint8_t colour)
{
    return kFavourite[colour % MiiPartCounts::favouriteColour];
}

Color miiSkin(uint8_t tone) { return kSkin[tone % MiiPartCounts::skinTone]; }
Color miiHair(uint8_t colour) { return kHair[colour % MiiPartCounts::hairColour]; }
Color miiEye(uint8_t colour) { return kEye[colour % MiiPartCounts::eyeColour]; }
Color miiMouth(uint8_t colour) { return kMouth[colour % MiiPartCounts::mouthColour]; }

Color miiShirt(const Mii& mii)
{
    return kFavourite[mii.favouriteColour % MiiPartCounts::favouriteColour];
}

void miiHead(Renderer& r, const Rect& box, const Mii& mii, float opacity)
{
    const MiiParts& parts = miiParts();
    if (!parts.ready() || opacity <= 0.0f)
        return;

    // Keep the artwork's own head proportions; a head stretched to a square box
    // is the difference between a Mii and a pancake.
    float aspect = parts.faceHeight() / parts.faceWidth();
    float height = std::min(box.h, box.w * aspect);
    float width = height / aspect;

    Rig rig {};
    rig.parts = &parts;
    rig.mii = &mii;
    rig.face = parts.part(MiiParts::Face, mii.faceShape);
    rig.box = Rect { box.centerX() - width * 0.5f, box.bottom() - height, width, height };
    rig.faceW = parts.faceWidth();
    rig.faceH = parts.faceHeight();

    // Where the chosen face shape lands, so everything on it can be placed
    // against the face itself rather than against the box it was fitted into.
    // For the median shape the two are the same; for the others they are not.
    // This has to come after the box and the medians it is derived from.
    rig.faceRect = rig.box;
    if (rig.face && rig.face->anchored()) {
        float sx = rig.box.w / rig.faceW;
        float sy = rig.box.h / rig.faceH;
        rig.faceRect = Rect {
            rig.box.centerX() + (rig.face->ax0 - 26.0f) * sx,
            rig.box.centerY() + (rig.face->ay0 - 26.0f) * sy,
            (rig.face->ax1 - rig.face->ax0) * sx,
            (rig.face->ay1 - rig.face->ay0) * sy,
        };
    }
    resolveColours(rig, mii, opacity);

    Detail detail = detailFor(width);
    using C = MiiParts::Category;

    bool flip = mii.hairFlipped();
    drawPart(r, rig, C::Hair, mii.hairStyle, flip, true); // behind the head
    drawPart(r, rig, C::Face, mii.faceShape, false, false);

    if (detail == Detail::Full) {
        // Marks on the skin, under everything the face is made of.
        drawPart(r, rig, C::Wrinkles, mii.wrinkles, false, false);
        drawPart(r, rig, C::Makeup, mii.makeup, false, false);
    }

    if (detail == Detail::Tiny) {
        drawTinyFeatures(r, rig, mii);
    } else {
        // Brows are not always the colour of the hair - the Mii format keeps a
        // separate field for them and so does the pass. The artwork names the
        // hair slots, exactly as the facial hair does, so the slots are swapped
        // for the duration and put back: a part names a slot, not a colour.
        //
        // Without this the field was stored, sent on the wire and offered in the
        // editor, and changing it did nothing at all.
        {
            Color hairWas = rig.slot[MiiParts::SlotHair];
            Color hairDarkWas = rig.slot[MiiParts::SlotHairDark];
            Color brows = kHair[mii.browColour % MiiPartCounts::browColour]
                              .withAlpha(opacity);
            rig.slot[MiiParts::SlotHair] = brows;
            rig.slot[MiiParts::SlotHairDark] = shade(brows, 0.35f).withAlpha(opacity);

            drawFeature(r, rig, C::Eyebrows, mii.browStyle);

            rig.slot[MiiParts::SlotHair] = hairWas;
            rig.slot[MiiParts::SlotHairDark] = hairDarkWas;
        }

        drawFeature(r, rig, C::Eyes, mii.eyeStyle);
        if (detail == Detail::Full)
            drawFeature(r, rig, C::Nose, mii.noseStyle);
        drawFeature(r, rig, C::Mouth, mii.mouthStyle);
    }

    if (detail == Detail::Full) {
        // Independent, so both can be worn. The beard goes on first: a
        // moustache sits over it, not under it.
        // Facial hair has its own colour, so the hair slots are swapped for the
        // duration and put back: the parts name a slot, not a colour.
        Color hairWas = rig.slot[MiiParts::SlotHair];
        Color hairDarkWas = rig.slot[MiiParts::SlotHairDark];
        Color whiskers = kHair[mii.facialHairColour % MiiPartCounts::facialHairColour]
                             .withAlpha(opacity);
        rig.slot[MiiParts::SlotHair] = whiskers;
        rig.slot[MiiParts::SlotHairDark] = shade(whiskers, 0.35f).withAlpha(opacity);

        drawPart(r, rig, C::Goatee, mii.beard, false, false);
        drawPart(r, rig, C::Mustache, mii.mustache, false, false);

        rig.slot[MiiParts::SlotHair] = hairWas;
        rig.slot[MiiParts::SlotHairDark] = hairDarkWas;

        // A mole is a dot, which is why there is no artwork for one.
        if (mii.hasMole()) {
            const Rect& b = rig.faceRect;
            float size = b.w * 0.016f * stepScale(mii.moleScale);
            r.ellipse(b.centerX() - b.w * (0.175f - stepShift(mii.moleX) * 2.0f),
                b.y + b.h * (0.665f + stepShift(mii.moleY) * 2.0f), size, size,
                shade(rig.slot[MiiParts::SlotSkin], 0.55f));
        }
    }

    drawPart(r, rig, C::Hair, mii.hairStyle, flip, false); // the fringe

    if (detail != Detail::Tiny && mii.glasses > 0) {
        // Index 0 in the artwork is already "no glasses", so the value is the
        // index and needs no shifting.
        drawFeature(r, rig, C::Glasses, mii.glasses);
    }
    if (mii.headwear > 0) {
        drawPart(r, rig, C::Hat, mii.headwear - 1, false, true);
        drawPart(r, rig, C::Hat, mii.headwear - 1, false, false);
    }
}

void miiFigure(Renderer& r, const Rect& box, const Mii& mii, float opacity, bool spotlight)
{
    if (spotlight) {
        Rect pool { box.centerX() - box.w * 0.75f, box.bottom() - 14.0f, box.w * 1.5f, 26.0f };
        r.glow(pool, theme::accentGlow.scaleAlpha(0.9f * opacity), 1.8f);
    }

    const MiiParts& parts = miiParts();
    float aspect = parts.ready() ? parts.faceHeight() / parts.faceWidth() : 1.24f;

    // Height and build, as the format means them: a taller Mii carries a smaller
    // head for its body, a heavier one wider shoulders. Both are a full byte, so
    // they are read as a fraction rather than as a handful of steps.
    float tall = static_cast<float>(mii.height) / 127.0f;
    float wide = static_cast<float>(mii.build) / 127.0f;

    float headHeight = box.h * (0.66f - tall * 0.10f);
    float headWidth = std::min(box.w, headHeight / aspect);

    float shoulderWidth = box.w * (0.78f + wide * 0.30f);
    float shoulderHeight = box.h * 0.34f;

    Color skin = kSkin[mii.skinTone % MiiPartCounts::skinTone].withAlpha(opacity);
    Color shirt = miiShirt(mii).withAlpha(opacity);

    // A neck first, so the head has something to stand on.
    float neck = headWidth * 0.28f;
    r.roundRect(Rect { box.centerX() - neck * 0.5f, box.y + headHeight - box.h * 0.05f, neck,
                    box.h * 0.16f },
        neck * 0.22f, shade(skin, 0.16f).withAlpha(opacity));

    Rect shoulders { box.centerX() - shoulderWidth * 0.5f, box.bottom() - shoulderHeight,
        shoulderWidth, shoulderHeight };
    r.roundRect(shoulders, shoulderWidth * 0.26f, box.h * 0.03f, shirt);

    // A collar, cut out of the shirt the way a crew neck is.
    r.ellipse(box.centerX(), shoulders.y + box.h * 0.005f, neck * 0.80f, box.h * 0.035f,
        shade(shirt, 0.22f).withAlpha(opacity));

    miiHead(r, Rect { box.centerX() - headWidth * 0.5f, box.y, headWidth, headHeight }, mii,
        opacity);
}

void miiStage(Renderer& r, const Rect& stage, const Mii& mii, uint32_t cardTheme,
    const StageFigure& shape, float radiusTop, float radiusBottom)
{
    Color top, bottom;
    stageGradient(cardTheme, top, bottom);
    r.gradientRect(stage, top, bottom, radiusTop, radiusBottom);

    // A raised floor eats into the height the figure has to live in, so the
    // figure is sized to the gap between the floor and the top rather than to
    // the stage. Without the clamp the passport's 230px floor pushed the head
    // clean off the top of the card.
    float floor = stage.bottom() - stage.h * shape.floorOffset;
    float ceiling = stage.y + stage.h * shape.topMargin;
    float room = std::max(floor - ceiling, 1.0f);

    float height = std::min(stage.h * (shape.shoulderHeight + 0.34f), room);
    float width = std::min(stage.w * (shape.shoulderWidth + 0.24f), height * 0.62f);

    miiFigure(r, Rect { stage.centerX() - width * 0.5f, floor - height, width, height }, mii);
}

} // namespace nxp::ui
