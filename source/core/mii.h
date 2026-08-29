#pragma once

#include <cstdint>
#include <string>

namespace nxp {

// A face, in 39 bytes.
//
// The pass a console hands out is deliberately tiny - a few hundred bytes, in
// the spirit of the 3DS's own StreetPass records - so an avatar has to be
// parameters rather than pixels. Every field here is a small index into a table
// the renderer knows, which means a face costs 78 hex characters on the wire
// and is drawn from real Mii part outlines baked into romfs: no texture, no
// atlas, and it stays sharp at any size from a 40px radar dot to a 700px
// passport card.
//
// Out-of-range values are clamped on read rather than rejected, so a face from
// a newer version of the app, or from a hostile server, still draws as a face.
struct Mii {
    static constexpr uint8_t kVersion = 8;

    // ---- shape
    uint8_t faceShape = 0;  // 0..11
    uint8_t skinTone = 2;   // 0..5
    // The format stores these as a full byte each, and so does this.
    uint8_t build = 64;     // 0..127, how wide the shoulders sit
    uint8_t height = 64;    // 0..127, how tall the figure stands
    // Drives the shirt, as it does on a real Mii. Twelve named colours.
    uint8_t favouriteColour = 0; // 0..11

    // ---- hair
    uint8_t hairStyle = 1;  // 0..131
    uint8_t hairColour = 1; // 0..7

    // ---- eyes
    uint8_t eyeStyle = 0;   // 0..59
    uint8_t eyeColour = 0;  // 0..5
    uint8_t browStyle = 0;  // 0..23
    // Brows are not always the colour of the hair - the format keeps its own
    // field, and so does this.
    uint8_t browColour = 0; // 0..7, from the hair table

    // ---- the rest of the face
    uint8_t noseStyle = 0;  // 0..17
    uint8_t mouthStyle = 0; // 0..35
    uint8_t mouthColour = 0;// 0..3
    // 0 is none - which is the artwork's own first entry, an empty part.
    uint8_t glasses = 0;      // 0..19
    uint8_t glassesColour = 0;// 0..5

    // A moustache and a beard are independent, exactly as `mustacheType` and
    // `beardType` are in the Mii format: plenty of people wear both. Folding
    // them into one field made every full beard mutually exclusive with every
    // moustache, which is not how faces work.
    uint8_t mustache = 0;   // 0..5, 0 is none
    uint8_t beard = 0;      // 0..5, 0 is none
    // One colour for both, as the format has it: its `beardColor` drives the
    // whole facial-hair tab, moustache included.
    uint8_t facialHairColour = 0; // 0..7, from the hair table

    // ---- the face's own marks
    uint8_t wrinkles = 0;   // 0..11, 0 is none
    uint8_t makeup = 0;     // 0..11, 0 is none

    uint8_t headwear = 0;   // 0..8, 0 is none

    // Bit 0: the hairstyle is mirrored - the format calls it hairDir, and it
    // is a free doubling of the hair catalogue. Bit 1: a mole. The reference
    // art has no mole in it because a mole is a dot, so this one is drawn
    // rather than baked; the format's position and scale for it are not
    // exposed, only whether there is one.
    uint8_t flags = 0;

    bool hairFlipped() const { return (flags & 1u) != 0; }
    void setHairFlipped(bool on) { flags = static_cast<uint8_t>((flags & ~1u) | (on ? 1u : 0u)); }
    bool hasMole() const { return (flags & 2u) != 0; }
    void setMole(bool on) { flags = static_cast<uint8_t>((flags & ~2u) | (on ? 2u : 0u)); }

    // ---- how each part is placed and shaped
    //
    // Every one of these is a signed step, -12..12, with 0 meaning the artwork's
    // own size and position. They are the Mii format's per-part scale, rotation
    // and offset: the thing that makes two faces built from the same parts look
    // like different people.
    //
    // There is deliberately no single "feature size" any more. The format has no
    // such control, and per-part scale does everything it did.
    int8_t eyeSpacing = 0;
    int8_t eyeHeight = 0;
    int8_t eyeScale = 0;
    int8_t eyeScaleY = 0;
    int8_t eyeRotate = 0;

    int8_t browSpacing = 0;
    int8_t browHeight = 0;
    int8_t browScale = 0;
    int8_t browScaleY = 0;
    int8_t browRotate = 0;

    int8_t noseHeight = 0;
    int8_t noseScale = 0;

    int8_t mouthHeight = 0;
    int8_t mouthScale = 0;
    int8_t mouthScaleY = 0;

    int8_t mustacheHeight = 0;
    int8_t mustacheScale = 0;

    // Not in the Mii format, which gives a beard only its type. Added because
    // the beards vary a lot in how far they hang, and one size does not sit
    // well on every chin.
    int8_t beardHeight = 0;
    int8_t beardScale = 0;

    int8_t glassesHeight = 0;
    int8_t glassesScale = 0;

    int8_t moleX = 0;
    int8_t moleY = 0;
    int8_t moleScale = 0;

    // The range every one of the fields above is clamped to. Widening it costs
    // nothing but wire: the renderer reads a step as a multiplier, so it has no
    // opinion about where the ends are.
    static constexpr int kStep = 12;

    // How many placement steps there are. They are declared contiguously above,
    // starting at `eyeSpacing`, so they can be walked as one run.
    static constexpr int kStepCount = 24;

    // Five bits a step, biased by 16 into 0..31. -12..12 needs twenty-five
    // values, so five bits is the smallest that holds one; twenty-four of them
    // come to fifteen bytes with nothing left over.
    static constexpr int kStepBits = 5;
    static constexpr size_t kStepBytes = (kStepCount * kStepBits + 7) / 8;

    // 24 indices and colours, then the packed placement steps.
    static constexpr size_t kBytes = 24 + kStepBytes;

    // Pulls every field back into its range.
    void clamp();

    // `kBytes * 2` lowercase hex characters. Round-trips through fromHex.
    std::string toHex() const;

    // Parses toHex output. Returns false and leaves `out` at defaults when the
    // string is not a face this version understands.
    static bool fromHex(const std::string& hex, Mii& out);

    // A plausible face derived from a 32-bit seed, stable for that seed.
    //
    // Every pass carries a portrait seed already, so a console that has never
    // opened the editor - and every simulated console - still looks like a
    // person rather than a default.
    static Mii fromSeed(uint32_t seed);

    // A fresh random face, for the editor's shuffle.
    static Mii random();

    bool operator==(const Mii& other) const;
    bool operator!=(const Mii& other) const { return !(*this == other); }
};

// How many options each part offers, for the editor and for clamping.
//
// The style counts are the real Mii catalogue, because that is what the baked
// artwork contains: twelve face shapes, a hundred and thirty-two hairstyles,
// sixty eyes, thirty-six mouths. The colour counts are the Mii format's own
// tables. Nothing here costs anything on the wire - each field was already a
// whole byte - so the only reason the app ever offered fewer was that the
// shapes had to be written by hand.
struct MiiPartCounts {
    static constexpr int faceShape = 12;
    static constexpr int skinTone = 6;
    static constexpr int build = 128;
    static constexpr int height = 128;
    static constexpr int favouriteColour = 12;
    static constexpr int hairStyle = 132;
    static constexpr int hairColour = 8;
    static constexpr int eyeStyle = 60;
    static constexpr int eyeColour = 6;
    static constexpr int browStyle = 24;
    static constexpr int browColour = 8; // the hair table
    static constexpr int noseStyle = 18;
    static constexpr int mouthStyle = 36;
    static constexpr int mouthColour = 5;
    static constexpr int glasses = 20;   // the artwork's index 0 is "none"
    static constexpr int glassesColour = 6;
    static constexpr int mustache = 6;   // the artwork's index 0 is "none"
    static constexpr int beard = 6;      // ditto
    static constexpr int facialHairColour = 8; // the hair table
    static constexpr int wrinkles = 12;  // ditto
    static constexpr int makeup = 12;    // ditto
    static constexpr int headwear = 9;   // including "none"
};

} // namespace nxp
