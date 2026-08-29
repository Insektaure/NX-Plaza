#include "core/mii.h"

#include "core/util.h"

#include <algorithm>
#include <cstddef>

namespace nxp {

namespace {
    uint8_t clampPart(uint8_t value, int count)
    {
        return static_cast<uint8_t>(value % static_cast<uint8_t>(count));
    }

    int8_t clampOffset(int8_t value, int limit)
    {
        return static_cast<int8_t>(std::min(std::max(static_cast<int>(value), -limit), limit));
    }

    // A small xorshift, so a seed gives the same face on every console without
    // depending on the platform's rand().
    struct SeedStream {
        uint32_t state;

        explicit SeedStream(uint32_t seed)
            : state(seed ? seed : 0x9E3779B9u)
        {
        }

        uint32_t next()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        // 0..count-1
        uint8_t pick(int count) { return static_cast<uint8_t>(next() % static_cast<uint32_t>(count)); }

        // -limit..limit
        int8_t offset(int limit)
        {
            return static_cast<int8_t>(static_cast<int>(next() % static_cast<uint32_t>(limit * 2 + 1))
                - limit);
        }

        // True `percent` of the time.
        bool chance(uint32_t percent) { return next() % 100u < percent; }
    };

    Mii rollFace(SeedStream& rng)
    {
        Mii mii;
        mii.faceShape = rng.pick(MiiPartCounts::faceShape);
        mii.skinTone = rng.pick(MiiPartCounts::skinTone);
        // Kept away from the extremes: 0 and 127 are a stick and a barrel.
        mii.build = static_cast<uint8_t>(40 + rng.pick(48));
        mii.height = static_cast<uint8_t>(40 + rng.pick(48));
        mii.favouriteColour = rng.pick(MiiPartCounts::favouriteColour);

        // A flat pick. The catalogue's one bald entry is a style like any other,
        // and at one in a hundred and thirty-two it needs no weighting away from.
        mii.hairStyle = rng.pick(MiiPartCounts::hairStyle);
        mii.hairColour = rng.pick(MiiPartCounts::hairColour);

        mii.eyeStyle = rng.pick(MiiPartCounts::eyeStyle);
        mii.eyeColour = rng.pick(MiiPartCounts::eyeColour);
        mii.browStyle = rng.pick(MiiPartCounts::browStyle);
        // Brows usually match the hair, and occasionally do not.
        mii.browColour = rng.chance(80) ? mii.hairColour
                                        : rng.pick(MiiPartCounts::browColour);
        mii.setHairFlipped(rng.chance(50));
        mii.setMole(rng.chance(10));
        mii.noseStyle = rng.pick(MiiPartCounts::noseStyle);
        mii.mouthStyle = rng.pick(MiiPartCounts::mouthStyle);
        mii.mouthColour = rng.pick(MiiPartCounts::mouthColour);

        // Accessories are the exception, not the rule.
        mii.glasses = rng.chance(28) ? static_cast<uint8_t>(1 + rng.pick(MiiPartCounts::glasses - 1))
                                     : 0;
        mii.glassesColour = rng.pick(MiiPartCounts::glassesColour);
        // Independent rolls, so a moustache and a beard can turn up together.
        mii.mustache = rng.chance(14)
            ? static_cast<uint8_t>(1 + rng.pick(MiiPartCounts::mustache - 1))
            : 0;
        mii.beard = rng.chance(11)
            ? static_cast<uint8_t>(1 + rng.pick(MiiPartCounts::beard - 1))
            : 0;
        // Whiskers grow out of the same head as the hair. Left unrolled this
        // defaulted to 0, which gave every blond a jet-black beard. One colour
        // covers the moustache too, which is how the format has it.
        mii.facialHairColour = rng.chance(85) ? mii.hairColour
                                         : rng.pick(MiiPartCounts::facialHairColour);
        mii.wrinkles = rng.chance(12)
            ? static_cast<uint8_t>(1 + rng.pick(MiiPartCounts::wrinkles - 1))
            : 0;
        mii.makeup = rng.chance(12)
            ? static_cast<uint8_t>(1 + rng.pick(MiiPartCounts::makeup - 1))
            : 0;
        mii.headwear = rng.chance(14)
            ? static_cast<uint8_t>(1 + rng.pick(MiiPartCounts::headwear - 1))
            : 0;

        // Placement is where a generated face stops looking like a template.
        // Positions move more than scales do: a nose twice the size is a joke,
        // a nose a little lower is a person.
        mii.eyeSpacing = rng.offset(4);
        mii.eyeHeight = rng.offset(4);
        mii.eyeScale = rng.offset(3);
        mii.eyeScaleY = rng.offset(3);
        mii.eyeRotate = rng.offset(3);

        mii.browSpacing = rng.offset(3);
        mii.browHeight = rng.offset(4);
        mii.browScale = rng.offset(3);
        mii.browScaleY = rng.offset(2);
        mii.browRotate = rng.offset(3);

        mii.noseHeight = rng.offset(4);
        mii.noseScale = rng.offset(3);

        mii.mouthHeight = rng.offset(4);
        mii.mouthScale = rng.offset(3);
        mii.mouthScaleY = rng.offset(2);

        mii.mustacheHeight = rng.offset(3);
        mii.mustacheScale = rng.offset(2);
        mii.beardHeight = rng.offset(3);
        mii.beardScale = rng.offset(2);
        mii.glassesHeight = rng.offset(3);
        mii.glassesScale = rng.offset(2);
        mii.moleX = rng.offset(4);
        mii.moleY = rng.offset(4);
        mii.moleScale = rng.offset(2);
        return mii;
    }
}

void Mii::clamp()
{
    faceShape = clampPart(faceShape, MiiPartCounts::faceShape);
    skinTone = clampPart(skinTone, MiiPartCounts::skinTone);
    build = clampPart(build, MiiPartCounts::build);
    height = clampPart(height, MiiPartCounts::height);
    favouriteColour = clampPart(favouriteColour, MiiPartCounts::favouriteColour);
    hairStyle = clampPart(hairStyle, MiiPartCounts::hairStyle);
    hairColour = clampPart(hairColour, MiiPartCounts::hairColour);
    eyeStyle = clampPart(eyeStyle, MiiPartCounts::eyeStyle);
    eyeColour = clampPart(eyeColour, MiiPartCounts::eyeColour);
    browStyle = clampPart(browStyle, MiiPartCounts::browStyle);
    noseStyle = clampPart(noseStyle, MiiPartCounts::noseStyle);
    mouthStyle = clampPart(mouthStyle, MiiPartCounts::mouthStyle);
    mouthColour = clampPart(mouthColour, MiiPartCounts::mouthColour);
    browColour = clampPart(browColour, MiiPartCounts::browColour);
    glasses = clampPart(glasses, MiiPartCounts::glasses);
    glassesColour = clampPart(glassesColour, MiiPartCounts::glassesColour);
    mustache = clampPart(mustache, MiiPartCounts::mustache);
    beard = clampPart(beard, MiiPartCounts::beard);
    facialHairColour = clampPart(facialHairColour, MiiPartCounts::facialHairColour);
    wrinkles = clampPart(wrinkles, MiiPartCounts::wrinkles);
    makeup = clampPart(makeup, MiiPartCounts::makeup);
    headwear = clampPart(headwear, MiiPartCounts::headwear);
    flags &= 0x03u;

    int8_t* steps = &eyeSpacing;
    for (int i = 0; i < kStepCount; i++)
        steps[i] = clampOffset(steps[i], kStep);
}

// The placement steps are compared and clamped as one run, which is only valid
// while they stay contiguous. int8_t members declared back to back are, but this
// says so out loud rather than leaving it to be discovered.
static_assert(offsetof(Mii, moleScale) - offsetof(Mii, eyeSpacing)
        == static_cast<size_t>(Mii::kStepCount - 1),
    "the placement steps must stay contiguous and int8_t");

namespace {

// The placement steps, as a stream of five-bit fields.
constexpr uint8_t kStepBias = 16;
constexpr uint32_t kStepMask = (1u << Mii::kStepBits) - 1u;

void packSteps(const int8_t* steps, uint8_t* out)
{
    uint32_t acc = 0;
    int bits = 0;
    size_t at = 0;
    for (int i = 0; i < Mii::kStepCount; i++) {
        acc = (acc << Mii::kStepBits)
            | ((static_cast<uint32_t>(steps[i] + kStepBias)) & kStepMask);
        bits += Mii::kStepBits;
        while (bits >= 8) {
            bits -= 8;
            out[at++] = static_cast<uint8_t>(acc >> bits);
        }
    }
    // Whatever is left over is flushed high, so the tail is stable rather than
    // whatever happened to be in the accumulator.
    if (bits > 0)
        out[at] = static_cast<uint8_t>(acc << (8 - bits));
}

void unpackSteps(const uint8_t* in, int8_t* steps)
{
    uint32_t acc = 0;
    int bits = 0;
    size_t at = 0;
    for (int i = 0; i < Mii::kStepCount; i++) {
        while (bits < Mii::kStepBits) {
            acc = (acc << 8) | in[at++];
            bits += 8;
        }
        bits -= Mii::kStepBits;
        steps[i] = static_cast<int8_t>(static_cast<int>((acc >> bits) & kStepMask)
            - kStepBias);
    }
}

} // namespace

std::string Mii::toHex() const
{
    // Indices and colours take a byte each and are written out plainly; the
    // placement steps are packed five bits apiece onto the end.
    uint8_t bytes[kBytes] = {
        kVersion,
        faceShape, skinTone, build, height, favouriteColour,
        hairStyle, hairColour,
        eyeStyle, eyeColour,
        browStyle, browColour,
        noseStyle, mouthStyle, mouthColour,
        glasses, glassesColour,
        mustache, beard, facialHairColour,
        wrinkles, makeup,
        headwear, flags,
    };
    packSteps(&eyeSpacing, bytes + 24);
    return nxp::toHex(bytes, sizeof(bytes));
}

bool Mii::fromHex(const std::string& hex, Mii& out)
{
    out = Mii {};

    uint8_t bytes[kBytes] = {};
    if (!nxp::fromHex(hex, bytes, sizeof(bytes)))
        return false;
    if (bytes[0] != kVersion)
        return false;

    out.faceShape = bytes[1];
    out.skinTone = bytes[2];
    out.build = bytes[3];
    out.height = bytes[4];
    out.favouriteColour = bytes[5];
    out.hairStyle = bytes[6];
    out.hairColour = bytes[7];
    out.eyeStyle = bytes[8];
    out.eyeColour = bytes[9];
    out.browStyle = bytes[10];
    out.browColour = bytes[11];
    out.noseStyle = bytes[12];
    out.mouthStyle = bytes[13];
    out.mouthColour = bytes[14];
    out.glasses = bytes[15];
    out.glassesColour = bytes[16];
    out.mustache = bytes[17];
    out.beard = bytes[18];
    out.facialHairColour = bytes[19];
    out.wrinkles = bytes[20];
    out.makeup = bytes[21];
    out.headwear = bytes[22];
    out.flags = bytes[23];

    unpackSteps(bytes + 24, &out.eyeSpacing);

    out.clamp();
    return true;
}

Mii Mii::fromSeed(uint32_t seed)
{
    SeedStream rng(seed);
    Mii mii = rollFace(rng);
    mii.clamp();
    return mii;
}

Mii Mii::random()
{
    uint32_t seed = 0;
    randomBytes(&seed, sizeof(seed));
    return fromSeed(seed);
}

bool Mii::operator==(const Mii& other) const
{
    return faceShape == other.faceShape && skinTone == other.skinTone && build == other.build
        && hairStyle == other.hairStyle && hairColour == other.hairColour
        && eyeStyle == other.eyeStyle && eyeColour == other.eyeColour
        && browStyle == other.browStyle && noseStyle == other.noseStyle
        && mouthStyle == other.mouthStyle && mouthColour == other.mouthColour
        && browColour == other.browColour && glasses == other.glasses
        && glassesColour == other.glassesColour && mustache == other.mustache
        && beard == other.beard && wrinkles == other.wrinkles
        && favouriteColour == other.favouriteColour
        && makeup == other.makeup && flags == other.flags
        && headwear == other.headwear && height == other.height
        && facialHairColour == other.facialHairColour
        // The placement steps are contiguous in the struct, so compare them as
        // one run rather than naming twenty-two fields that will grow again.
        && std::equal(&eyeSpacing, &eyeSpacing + kStepCount, &other.eyeSpacing);
}

} // namespace nxp
