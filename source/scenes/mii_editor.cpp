#include "app.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {

// The Mii maker.
class MiiEditorScene final : public Scene {
public:
    enum Zone : int {
        Zone_Row = Touch_SceneBase,
        Zone_Shuffle,
        Zone_Done,
    };

    MiiEditorScene(const Mii& start, std::function<void(const Mii&)> onDone)
        : m_mii(start)
        , m_original(start)
        , m_onDone(std::move(onDone))
    {
    }

    bool coversChrome() const override { return true; }

    void update(App& app, const Input& input, float dt) override
    {
        m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

        const Touch& touch = input.touch;
        if (touch.pressed)
            m_brakedTap = m_scroll.absorbPress();
        if (touch.down && touch.dragged && m_listArea.contains(touch.startX, touch.startY)) {
            m_scroll.drag(-touch.dy, dt);
            m_dragging = true;
        } else if (m_dragging && !touch.down) {
            m_scroll.release();
            m_dragging = false;
        }
        m_scroll.update(dt);

        TouchTarget tap;
        if (!m_brakedTap && app.takeTap(tap)) {
            if (tap.is(Zone_Row) && tap.index < partCount()) {
                // A tap on a row steps it, which is what a tap on a stepper
                // means everywhere else in the app.
                m_focus = tap.index;
                step(1);
                return;
            }
            if (tap.is(Zone_Shuffle)) {
                m_mii = Mii::random();
                return;
            }
            if (tap.is(Zone_Done)) {
                finish(app);
                return;
            }
        }

        if (input.back()) {
            // Backing out keeps the face you arrived with.
            m_mii = m_original;
            app.popOverlay();
            return;
        }

        if (input.navDown)
            m_focus = std::min(m_focus + 1, partCount() - 1);
        if (input.navUp)
            m_focus = std::max(m_focus - 1, 0);
        if (input.navDown || input.navUp)
            reveal();

        if (input.navRight)
            step(1);
        if (input.navLeft)
            step(-1);

        // The long lists are long: a hundred and thirty-two hairstyles is a lot
        // of taps at one a time, so the shoulders move in tens.
        if (input.pressed(HidNpadButton_R))
            step(jump(m_focus));
        if (input.pressed(HidNpadButton_L))
            step(-jump(m_focus));

        if (input.pressed(HidNpadButton_X))
            m_mii = Mii::random();

        if (input.pressed(HidNpadButton_Y)) {
            // The files screen is pushed over this one, so the editor keeps its
            // state: a face loaded there lands in the editor to be adjusted, and
            // is still only saved to the pass when this screen is finished.
            MiiEditorScene* self = this;
            app.pushOverlay(makeMiiFilesScene(m_mii, app.store().myPass().handle,
                [self](const Mii& loaded) { self->m_mii = loaded; }));
            return;
        }

        if (input.accept())
            finish(app);
    }

    void draw(App& app, Renderer& r) override
    {
        r.clear(theme::bg0);

        app.hint("A", "done");
        app.hint("B", "cancel");
        app.hint("X", "shuffle");
        app.hint("Y", "save / load");
        if (jump(m_focus) > 1)
            app.hint("L/R", "jump ten");

        Rect area = app.contentArea();
        Rect content { area.x + theme::edge, area.y + theme::edgeTop,
            area.w - theme::edge * 2.0f, area.h - theme::edgeTop * 2.0f };

        Rect left { content.x, content.y, kPreviewWidth, content.h };
        Rect right { left.right() + theme::s9, content.y,
            content.right() - left.right() - theme::s9, content.h };

        drawPreview(app, r, left);
        drawParts(app, r, right);
    }

private:
    static constexpr float kPreviewWidth = 560.0f;
    static constexpr float kRowHeight = 104.0f;

    // One editable part: what it is called, how many options it has, and where
    // it lives in the Mii.
    struct Part {
        const char* label;
        int count;
        uint8_t Mii::*value;   // set for the index fields
        int8_t Mii::*offset;   // set instead for the placement fields
        int limit;             // +/- range, for the placement fields
        uint8_t bit = 0;       // set instead for the yes/no flags
    };

    static const Part* partTable(int& count)
    {
        static const Part kParts[] = {
            { "Face shape", MiiPartCounts::faceShape, &Mii::faceShape, nullptr, 0 },
            { "Skin tone", MiiPartCounts::skinTone, &Mii::skinTone, nullptr, 0 },
            { "Hair", MiiPartCounts::hairStyle, &Mii::hairStyle, nullptr, 0 },
            { "Hair colour", MiiPartCounts::hairColour, &Mii::hairColour, nullptr, 0 },
            { "Eyes", MiiPartCounts::eyeStyle, &Mii::eyeStyle, nullptr, 0 },
            { "Eye colour", MiiPartCounts::eyeColour, &Mii::eyeColour, nullptr, 0 },
            { "Eyebrows", MiiPartCounts::browStyle, &Mii::browStyle, nullptr, 0 },
            { "Eyebrow colour", MiiPartCounts::browColour, &Mii::browColour, nullptr, 0 },
            { "Nose", MiiPartCounts::noseStyle, &Mii::noseStyle, nullptr, 0 },
            { "Mouth", MiiPartCounts::mouthStyle, &Mii::mouthStyle, nullptr, 0 },
            { "Lip colour", MiiPartCounts::mouthColour, &Mii::mouthColour, nullptr, 0 },
            { "Glasses", MiiPartCounts::glasses, &Mii::glasses, nullptr, 0 },
            { "Glasses colour", MiiPartCounts::glassesColour, &Mii::glassesColour, nullptr, 0 },
            { "Moustache", MiiPartCounts::mustache, &Mii::mustache, nullptr, 0 },
            { "Beard", MiiPartCounts::beard, &Mii::beard, nullptr, 0 },
            { "Facial hair colour", MiiPartCounts::facialHairColour,
              &Mii::facialHairColour, nullptr, 0 },
            { "Wrinkles", MiiPartCounts::wrinkles, &Mii::wrinkles, nullptr, 0 },
            { "Makeup", MiiPartCounts::makeup, &Mii::makeup, nullptr, 0 },
            { "Mole", 2, nullptr, nullptr, 0, 2 },
            { "Hair flipped", 2, nullptr, nullptr, 0, 1 },
            { "Headwear", MiiPartCounts::headwear, &Mii::headwear, nullptr, 0 },
            { "Build", MiiPartCounts::build, &Mii::build, nullptr, 0 },
            { "Height", MiiPartCounts::height, &Mii::height, nullptr, 0 },
            { "Favourite colour", MiiPartCounts::favouriteColour,
              &Mii::favouriteColour, nullptr, 0 },
            { "Eye spacing", 0, nullptr, &Mii::eyeSpacing, Mii::kStep },
            { "Eye height", 0, nullptr, &Mii::eyeHeight, Mii::kStep },
            { "Eye width", 0, nullptr, &Mii::eyeScale, Mii::kStep },
            { "Eye height scale", 0, nullptr, &Mii::eyeScaleY, Mii::kStep },
            { "Eye tilt", 0, nullptr, &Mii::eyeRotate, Mii::kStep },

            { "Eyebrow spacing", 0, nullptr, &Mii::browSpacing, Mii::kStep },
            { "Eyebrow height", 0, nullptr, &Mii::browHeight, Mii::kStep },
            { "Eyebrow width", 0, nullptr, &Mii::browScale, Mii::kStep },
            { "Eyebrow thickness", 0, nullptr, &Mii::browScaleY, Mii::kStep },
            { "Eyebrow tilt", 0, nullptr, &Mii::browRotate, Mii::kStep },

            { "Nose height", 0, nullptr, &Mii::noseHeight, Mii::kStep },
            { "Nose size", 0, nullptr, &Mii::noseScale, Mii::kStep },

            { "Mouth height", 0, nullptr, &Mii::mouthHeight, Mii::kStep },
            { "Mouth width", 0, nullptr, &Mii::mouthScale, Mii::kStep },
            { "Mouth thickness", 0, nullptr, &Mii::mouthScaleY, Mii::kStep },

            { "Moustache height", 0, nullptr, &Mii::mustacheHeight, Mii::kStep },
            { "Moustache size", 0, nullptr, &Mii::mustacheScale, Mii::kStep },

            { "Beard height", 0, nullptr, &Mii::beardHeight, Mii::kStep },
            { "Beard size", 0, nullptr, &Mii::beardScale, Mii::kStep },

            { "Glasses height", 0, nullptr, &Mii::glassesHeight, Mii::kStep },
            { "Glasses size", 0, nullptr, &Mii::glassesScale, Mii::kStep },

            { "Mole across", 0, nullptr, &Mii::moleX, Mii::kStep },
            { "Mole down", 0, nullptr, &Mii::moleY, Mii::kStep },
            { "Mole size", 0, nullptr, &Mii::moleScale, Mii::kStep },
        };
        count = static_cast<int>(sizeof(kParts) / sizeof(kParts[0]));
        return kParts;
    }

    static const Part& part(int index)
    {
        int count = 0;
        const Part* table = partTable(count);
        return table[std::min(std::max(index, 0), count - 1)];
    }

    // Straight from the table, so adding a row cannot leave the cursor unable
    // to reach it.
    static int partCount()
    {
        int count = 0;
        partTable(count);
        return count;
    }

    // How far a shoulder press moves in this row. Rows with only a handful of
    // options step one at a time, so the two controls never disagree.
    int jump(int index) const
    {
        const Part& p = part(index);
        return p.value && p.count > 24 ? 10 : 1;
    }

    void step(int delta)
    {
        const Part& p = part(m_focus);
        if (p.bit) {
            m_mii.flags ^= p.bit; // two states, so any step is a toggle
            return;
        }
        if (p.value) {
            int value = static_cast<int>(m_mii.*(p.value)) + delta;
            value = (value % p.count + p.count) % p.count;
            m_mii.*(p.value) = static_cast<uint8_t>(value);
        } else {
            int value = static_cast<int>(m_mii.*(p.offset)) + delta;
            m_mii.*(p.offset) = static_cast<int8_t>(
                std::min(std::max(value, -p.limit), p.limit));
        }
    }

    std::string valueLabel(int index) const
    {
        const Part& p = part(index);
        if (p.bit)
            return (m_mii.flags & p.bit) ? "yes" : "no";
        if (p.offset)
            return format("%+d", static_cast<int>(m_mii.*(p.offset)));

        uint8_t value = m_mii.*(p.value);
        // The parts whose first entry is an absence say so. Hair is not among
        // them: every hairstyle in the catalogue is a style, and the bald one
        // has an index like any other.
        if (value == 0
            && (p.value == &Mii::glasses || p.value == &Mii::mustache
                || p.value == &Mii::beard || p.value == &Mii::wrinkles
                || p.value == &Mii::makeup || p.value == &Mii::headwear))
            return "none";
        return format("%d of %d", value + 1, p.count);
    }

    void reveal()
    {
        m_scroll.centerOn(static_cast<float>(m_focus) * (kRowHeight + theme::s3)
            + kRowHeight * 0.5f);
    }

    void finish(App& app)
    {
        if (m_onDone)
            m_onDone(m_mii);
        app.popOverlay();
    }

    // The face, big, on its own stage - and a row of it at thumbnail size, so
    // you can see what it will look like on someone else's inbox.
    void drawPreview(App& app, Renderer& r, const Rect& box)
    {
        TextStyle eyebrow;
        eyebrow.size = theme::textSm;
        eyebrow.weight = FontWeight::Bold;
        eyebrow.color = theme::accent;
        eyebrow.tracking = theme::trackingWider;
        eyebrow.uppercase = true;
        r.text(box.x, box.y, "your face", eyebrow);

        TextStyle title;
        title.size = theme::text2xl;
        title.weight = FontWeight::Bold;
        title.color = theme::fg1;
        title.tracking = theme::trackingTight;
        float titleY = box.y + theme::textSm * theme::leadingNormal + theme::s2;
        r.text(box.x, titleY, "Make your Mii", title);

        Rect card { box.x, titleY + title.size * theme::leadingTight + theme::s6,
            box.w, box.h - (titleY - box.y) - title.size * theme::leadingTight - theme::s6
                - kThumbRow - theme::s6 };

        // The whole figure, not just the head. Build, height and the favourite
        // colour only show on the body, so a head-only preview left three of
        // the rows below looking inert while you edited them.
        //
        // Held still, with no sway: the preview is a thing you adjust by
        // fractions of a head, and a drifting figure makes it impossible to see
        // whether a spacing step moved anything.
        r.pushClip(card);
        ui::miiStage(r, card, m_mii, 0,
            ui::StageFigure { 0.52f, 0.58f, 0.30f, 0.52f, 0.04f, 0.17f },
            theme::r5, theme::r5);
        r.popClip();
        r.strokeRect(card, theme::r5, theme::stroke, theme::stroke2);

        // How it reads at the sizes it will actually be seen at.
        Rect strip { box.x, card.bottom() + theme::s6, box.w, kThumbRow };
        const float sizes[4] = { 44.0f, 64.0f, 88.0f, 120.0f };
        float x = strip.x;
        for (float size : sizes) {
            r.roundRect(Rect { x, strip.y + (kThumbRow - size) * 0.5f, size, size },
                theme::r2, theme::bg2);
            ui::miiHead(r, Rect { x + size * 0.08f, strip.y + (kThumbRow - size) * 0.5f
                                      + size * 0.06f,
                            size * 0.84f, size * 0.88f },
                m_mii);
            x += size + theme::s4;
        }

        TextStyle caption;
        caption.size = theme::textXs;
        caption.color = theme::fg3;
        r.text(Rect { x + theme::s2, strip.y, strip.right() - x, strip.h },
            "how others see you", caption, Align::Left, VAlign::Middle);
    }

    void drawParts(App& app, Renderer& r, const Rect& box)
    {
        m_listArea = box;

        float total = static_cast<float>(partCount()) * (kRowHeight + theme::s3);
        m_scroll.setBounds(box.h, std::max(0.0f, total - theme::s3));

        r.pushClipVertical(box.inset(0.0f, -theme::focusRoom));
        float y = box.y - m_scroll.offset();
        for (int i = 0; i < partCount(); i++) {
            Rect row { box.x, y, box.w - theme::s6, kRowHeight };
            if (row.bottom() > box.y - kRowHeight && row.y < box.bottom() + kRowHeight)
                drawRow(app, r, row, i);
            y += kRowHeight + theme::s3;
        }
        r.popClip();

        if (m_scroll.scrollable())
            ui::scrollbar(r, Rect { box.right() - 8.0f, box.y, 8.0f, box.h },
                m_scroll.progress(), m_scroll.visibleFraction());
    }

    void drawRow(App& app, Renderer& r, const Rect& box, int index)
    {
        app.touchZone(box, Zone_Row, index);

        bool focused = index == m_focus;
        float focus = app.touchHeld(Zone_Row, index)
            ? 1.0f
            : (focused ? 0.7f + 0.3f * m_pulse : 0.0f);
        ui::card(r, box, focus, focused ? theme::bg2 : theme::bg1, theme::r3);

        Rect inner = box.inset(theme::s6, theme::s4);
        const Part& p = part(index);

        TextStyle label;
        label.size = theme::textBase;
        label.weight = FontWeight::Bold;
        label.color = theme::fg1;
        r.text(Rect { inner.x, inner.y, inner.w * 0.5f, inner.h }, p.label, label, Align::Left,
            VAlign::Middle);

        // A swatch where the part is a colour, the option number otherwise.
        // Every colour field belongs here.
        if (swatchPalette(p) != nullptr) {
            drawSwatches(r, inner, index, p);
            return;
        }

        TextStyle value;
        value.size = theme::textBase;
        value.weight = FontWeight::Medium;
        value.color = focused ? theme::accent : theme::fg2;
        std::string text = valueLabel(index);
        float width = r.measure(text, value);

        // Chevrons, so a row reads as something you step through.
        float arrow = 26.0f;
        ui::icon(r, Rect { inner.right() - width - theme::s6 - arrow, inner.centerY() - arrow * 0.5f,
                     arrow, arrow },
            ui::Icon::Chevron, focused ? theme::accent : theme::fg4, 2.5f);
        r.text(Rect { inner.right() - width, inner.y, width, inner.h }, text, value, Align::Left,
            VAlign::Middle);
    }

    // The palette a row's swatches come from, or null when the row is not a
    // colour at all. One place decides, so the drawing and the choice between
    // swatches and a number cannot disagree.
    using Palette = Color (*)(uint8_t);
    static Palette swatchPalette(const Part& p)
    {
        if (p.value == &Mii::skinTone)
            return ui::miiSkin;
        if (p.value == &Mii::hairColour)
            return ui::miiHair;
        // Eyebrows and facial hair are indices into the hair table, which is
        // why they share its swatches rather than having one of their own.
        if (p.value == &Mii::browColour || p.value == &Mii::facialHairColour)
            return ui::miiHair;
        if (p.value == &Mii::eyeColour)
            return ui::miiEye;
        if (p.value == &Mii::mouthColour)
            return ui::miiMouth;
        if (p.value == &Mii::glassesColour)
            return ui::miiGlasses;
        if (p.value == &Mii::favouriteColour)
            return ui::miiFavourite;
        return nullptr;
    }

    void drawSwatches(Renderer& r, const Rect& inner, int index, const Part& p)
    {
        Palette palette = swatchPalette(p);
        if (!palette)
            return;

        int count = p.count;
        float gap = 10.0f;

        // Sized to fit rather than fixed. A favourite colour has twelve
        // options where a skin tone has six, and at a fixed 40px the long rows
        // ran back underneath their own label.
        float room = inner.w * 0.5f - theme::s6;
        float size = std::min(40.0f,
            (room - static_cast<float>(count - 1) * gap) / static_cast<float>(count));

        float total = static_cast<float>(count) * size + static_cast<float>(count - 1) * gap;
        float x = inner.right() - total;
        uint8_t current = m_mii.*(p.value);

        for (int i = 0; i < count; i++) {
            Color colour = palette(static_cast<uint8_t>(i));

            Rect swatch { x, inner.centerY() - size * 0.5f, size, size };
            r.roundRect(swatch, theme::r1, colour);
            r.strokeRect(swatch, theme::r1, 1.5f, theme::stroke2);
            if (static_cast<int>(current) == i)
                r.strokeRect(swatch.inset(-5.0f), theme::r1 + 5.0f, 3.0f, theme::accent);
            x += size + gap;
        }
    }

    static constexpr float kThumbRow = 130.0f;

    Mii m_mii;
    Mii m_original;
    std::function<void(const Mii&)> m_onDone;

    int m_focus = 0;
    float m_pulse = 0.0f;

    ui::ScrollView m_scroll;
    Rect m_listArea;
    bool m_dragging = false;
    bool m_brakedTap = false;
};

} // namespace

std::unique_ptr<Scene> makeMiiEditorScene(const Mii& start, std::function<void(const Mii&)> onDone)
{
    return std::unique_ptr<Scene>(new MiiEditorScene(start, std::move(onDone)));
}

} // namespace nxp
