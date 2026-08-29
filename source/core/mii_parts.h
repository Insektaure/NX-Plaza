#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nxp {

// The Mii artwork, read from romfs once at startup.
//
// A part is a list of quads and discs in the artwork's own 52-unit coordinate
// space, already reduced from outlines to shapes that the 2D renderer can draw
// directly.
//
// Every quad carries a colour *slot* rather than a colour. The artwork is drawn
// in an editor's icon palette, so a fill says "this is the iris" and the Mii
// says what colour an iris is.
class MiiParts {
public:
    enum Category : uint8_t {
        Face = 0, Hair, Eyes, Eyebrows, Nose, Mouth, Glasses, Mustache, Goatee,
        Hat, Makeup, Wrinkles, CategoryCount
    };

    enum Slot : uint8_t {
        SlotSkin = 0, SlotSkinDark, SlotHair, SlotHairDark, SlotEye, SlotWhite,
        SlotDark, SlotMouth, SlotMouthDark, SlotFrame, SlotLens, SlotHat,
        SlotHatDark, SlotMakeup, SlotCount
    };

    // Artwork units, fixed point: 16 to the unit, 52 units to the canvas.
    static constexpr float Unit = 16.0f;

#pragma pack(push, 1)
    // A filled path arrives as bands with horizontal tops and bottoms; a
    // stroked one as segments with two parallel slanted sides. They are
    // antialiased along different edges, so they stay distinguishable.
    enum Kind : uint8_t { Trapezoid = 0, Band = 1 };

    struct Quad {
        int16_t xy[8]; // four corners, in the order the baker writes them
        uint8_t slot;
        uint8_t kind;
    };
    struct Disc {
        int16_t cx, cy, r;
        uint8_t slot;
        uint8_t pad;
    };
#pragma pack(pop)

    struct Part {
        const Quad* quads = nullptr;
        const Disc* discs = nullptr;
        uint16_t quadCount = 0;
        uint16_t discCount = 0;
        // How many of those come before the face plane. A hairstyle's first
        // pieces go behind the head; everything else is drawn in front of it.
        uint16_t backQuads = 0;
        uint16_t backDiscs = 0;
        // The placeholder head this part was drawn against, in artwork units.
        // A beard is authored for one particular chin; this is that chin, so a
        // part can be moved onto whichever face shape it is actually going on.
        // Zero-width when the part has no head to anchor to.
        float ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
        bool anchored() const { return ax1 > ax0 && ay1 > ay0; }

        float x0 = 0, y0 = 0, x1 = 0, y1 = 0; //< bounds, artwork units
        float width() const { return x1 - x0; }
        float height() const { return y1 - y0; }
        float midX() const { return (x0 + x1) * 0.5f; }
        float midY() const { return (y0 + y1) * 0.5f; }
    };

    bool load(const char* path);
    void exit();
    bool ready() const { return m_ready; }

    int count(Category c) const
    {
        return c < CategoryCount ? static_cast<int>(m_parts[c].size()) : 0;
    }

    // Never null once loaded: the index wraps, so a Mii carrying a part number
    // from a newer console still draws something rather than nothing.
    const Part* part(Category c, int index) const;

    // The typical width of a category's artwork. Parts are placed relative to
    // this rather than to their own bounds, so that a wide-eyed style stays
    // wider than a narrow one instead of every style being stretched to fit.
    float medianWidth(Category c) const { return m_medianWidth[c]; }

    // The head's own proportions, taken from the face artwork.
    float faceWidth() const { return m_faceWidth; }
    float faceHeight() const { return m_faceHeight; }

private:
    std::vector<uint8_t> m_blob;
    std::vector<Part> m_parts[CategoryCount];
    float m_medianWidth[CategoryCount] = {};
    float m_faceWidth = 26.0f;
    float m_faceHeight = 32.0f;
    bool m_ready = false;
};

// The one instance, filled in at startup.
MiiParts& miiParts();

} // namespace nxp
