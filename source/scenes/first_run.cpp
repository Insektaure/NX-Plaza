#include "app.h"
#include "core/identity.h"
#include "core/place.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace nxp {

namespace {

// First launch.
//
// An amber bloom bleeding in from the top-left corner, then two columns with
// --space-10 between them: an 820px column of copy - the mark, the headline at
// --text-3xl, the paragraph, three numbered steps, two buttons - and a --bg-1
// panel at radius-5 holding this console's pairing code.
class FirstRunScene final : public Scene {
public:
    enum Zone : int {
        Zone_MakePass = Touch_SceneBase,
        Zone_NotNow,
    };

    void onEnter(App& app) override
    {
        m_handle = suggestedHandle();
        m_fingerprint = fingerprintBits();
    }

    void update(App& app, const Input& input, float dt) override
    {
        m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
        m_intro = std::min(1.0f, m_intro + dt * 1.6f);

        TouchTarget tap;
        bool touched = app.takeTap(tap);
        if (touched) {
            if (tap.is(Zone_MakePass))
                m_action = 0;
            else if (tap.is(Zone_NotNow))
                m_action = 1;
            else
                touched = false;
        }

        if (input.navLeft)
            m_action = 0;
        if (input.navRight)
            m_action = 1;

        if (input.accept() || touched) {
            Settings settings = app.store().settings();
            settings.firstRunDone = true;
            app.store().setSettings(settings);

            if (m_action == 0) {
                Pass pass = app.store().myPass();
                std::string value;
                if (app.textInput("The name on your pass", m_handle, 16, value, false))
                    pass.handle = value;
                if (app.textInput("A greeting, up to 60 characters", pass.greeting, 60, value))
                    pass.greeting = value;
                app.store().setMyPass(pass);
                app.store().flush();
                app.popOverlay();
                app.setTab(Tab::Passport);

                // Then the face, which is the part worth taking time over.
                App* appPtr = &app;
                app.pushOverlay(makeMiiEditorScene(pass.face(), [appPtr](const Mii& face) {
                    Pass updated = appPtr->store().myPass();
                    updated.setFace(face);
                    appPtr->store().setMyPass(updated);
                    appPtr->store().flush();
                    appPtr->sync().publishPass();
                }));
            } else {
                app.store().flush();
                app.sync().publishPass();
                app.popOverlay();
            }
            return;
        }

        if (input.pressed(HidNpadButton_Plus))
            app.requestExit();
    }

    bool coversChrome() const override { return true; }

    void draw(App& app, Renderer& r) override
    {
        r.clear(theme::bg0);

        app.hint("A", m_action == 0 ? "make my pass" : "not now");

        Rect screen = app.contentArea();
        screen.x = 0.0f;
        screen.w = Renderer::DesignWidth;

        // "left:-200px;top:-200px;width:1000px;height:1000px" amber bloom.
        r.glow(Rect { -200.0f, -200.0f, 1000.0f, 1000.0f },
            theme::accentGlow.scaleAlpha(0.26f * m_intro), 1.5f);

        // "padding:96px 128px" in the artboard; --space-8 vertically here, since
        // the control strip takes 88 off the bottom of the screen.
        Rect content = Rect { 128.0f, theme::s8, screen.w - 256.0f, screen.h - theme::s8 * 2.0f }
                           .offset(0.0f, (1.0f - m_intro) * 24.0f);

        Rect left { content.x, content.y, kCopyWidth, content.h };
        Rect right { left.right() + theme::s10, content.y,
            content.right() - left.right() - theme::s10, content.h };

        drawCopy(app, r, left);
        drawPairing(r, right);
    }

private:
    static constexpr float kCopyWidth = 820.0f;
    static constexpr float kStepDot = 56.0f;
    static constexpr float kButtonHeight = 88.0f; // padding 22px + --text-base

    void drawCopy(App& app, Renderer& r, const Rect& box)
    {
        // The mark: an 88px lantern with an 80px bloom, then the wordmark.
        float markY = box.y + 44.0f;
        r.glow(Rect { box.x - 36.0f, markY - 80.0f, 160.0f, 160.0f },
            theme::accentGlow.scaleAlpha(0.5f), 1.5f);
        r.circle(box.x + 44.0f, markY, 44.0f, theme::mark);
        r.circle(box.x + 34.0f, markY - 12.0f, 17.0f, theme::accentSoft);

        TextStyle brand;
        brand.size = theme::text2xl;
        brand.weight = FontWeight::Bold;
        brand.color = theme::fg1;
        brand.tracking = theme::trackingTight;
        r.text(Rect { box.x + 88.0f + theme::s5, markY - 44.0f, box.w, 88.0f }, kAppName,
            brand, Align::Left, VAlign::Middle);

        TextStyle title;
        title.size = theme::text3xl;
        title.weight = FontWeight::Bold;
        title.color = theme::fg1;
        title.tracking = theme::trackingTight;
        title.leading = theme::leadingTight;
        float titleY = markY + 44.0f + theme::s6;
        // Short on purpose. The box holds two lines at this size.
        float titleUsed = r.textWrapped(Rect { box.x, titleY, 760.0f, title.size * 2.4f },
            "Leave it open.", title, 2);

        TextStyle body;
        body.size = theme::textBase;
        body.color = theme::fg2;
        body.leading = theme::leadingNormal;
        float bodyY = titleY + titleUsed + theme::s6;
        float bodyUsed = r.textWrapped(Rect { box.x, bodyY, 700.0f, 200.0f },
            "While this is open, your console swaps a small pass with the others that are "
            "open too - a face, a greeting, whatever you chose to carry. Who you meet is "
            "whoever is awake within the reach you set.",
            body, 4);

        // Three steps: a 56px numbered dot, the first in accent.
        const char* steps[3] = {
            "Make your pass - a face, a greeting, one thing to trade",
            "Leave it open; it checks in on its own",
            "Open the plaza and see who you crossed",
        };

        float y = bodyY + bodyUsed + theme::s6;
        for (int i = 0; i < 3; i++) {
            Rect dot { box.x, y, kStepDot, kStepDot };
            r.circle(dot.centerX(), dot.centerY(), kStepDot * 0.5f,
                i == 0 ? theme::accent : theme::bg3);

            TextStyle index;
            index.size = theme::textBase;
            index.weight = FontWeight::Bold;
            index.color = i == 0 ? theme::bg0 : theme::fg2;
            r.text(dot, format("%d", i + 1), index, Align::Center, VAlign::Middle);

            TextStyle step;
            step.size = theme::textBase;
            step.color = i == 0 ? theme::fg1 : theme::fg2;
            r.text(Rect { dot.right() + theme::s5, y, box.w - kStepDot - theme::s5, kStepDot },
                steps[i], step, Align::Left, VAlign::Middle);

            y += kStepDot + theme::s4;
        }

        // Two buttons: padding 22px 48px / 22px 40px, radius-2.
        float buttonY = y + theme::s5 - theme::s4;
        float primaryWidth = ui::actionButtonWidth(r, "Make my pass") + 16.0f;
        float secondaryWidth = ui::actionButtonWidth(r, "Not now");

        Rect primary { box.x, buttonY, primaryWidth, kButtonHeight };
        Rect secondary { primary.right() + theme::s4, buttonY, secondaryWidth, kButtonHeight };

        app.touchZone(primary, Zone_MakePass);
        app.touchZone(secondary, Zone_NotNow);

        float pulse = 0.7f + 0.3f * m_pulse;
        ui::actionButton(r, primary, "Make my pass", true,
            (m_action == 0 || app.touchHeld(Zone_MakePass)) ? pulse : 0.0f);
        ui::actionButton(r, secondary, "Not now", false,
            (m_action == 1 || app.touchHeld(Zone_NotNow)) ? pulse : 0.0f);
    }

    // The pairing panel: --bg-1 at radius-5, the code in mono on --bg-2, and a
    // fingerprint of the console id where the artboard has a QR placeholder.
    void drawPairing(Renderer& r, const Rect& box)
    {
        Rect panel { box.x, box.y + 40.0f, box.w, box.h - 120.0f };
        r.roundRect(panel, theme::r5, theme::bg1);

        TextStyle eyebrow;
        eyebrow.size = theme::textSm;
        eyebrow.weight = FontWeight::Bold;
        eyebrow.color = theme::fg3;
        eyebrow.tracking = theme::trackingWider;
        eyebrow.uppercase = true;

        float y = panel.y + theme::s9;
        r.text(Rect { panel.x, y, panel.w, 40.0f }, "pair this console", eyebrow,
            Align::Center, VAlign::Top);

        // The code, letter-spaced, on its own --bg-2 plate.
        TextStyle code;
        code.size = theme::text2xl;
        code.weight = FontWeight::Medium;
        code.color = theme::fg1;
        code.tracking = 0.18f;

        std::string text = identity().shortCode();
        y += theme::textSm * theme::leadingNormal + theme::s6;
        float plateWidth = r.measure(text, code) + theme::s6 * 2.0f;
        Rect plate { panel.centerX() - plateWidth * 0.5f, y, plateWidth,
            code.size * theme::leadingSnug + theme::s5 * 2.0f };
        r.roundRect(plate, theme::r3, theme::bg2);
        r.text(plate, text, code, Align::Center, VAlign::Middle);

        // "width:240px;height:240px" - the artboard's QR placeholder. A real
        // scannable code needs an encoder; this is an honest picture of the id
        // instead, which two people can still compare at a glance.
        y = plate.bottom() + theme::s6;
        float side = std::min(240.0f, panel.w - theme::s7 * 2.0f);
        Rect grid { panel.centerX() - side * 0.5f, y, side, side };
        r.roundRect(grid, theme::r3, theme::bg2);

        float cell = grid.w / 11.0f;
        for (int gy = 0; gy < 9; gy++) {
            for (int gx = 0; gx < 9; gx++) {
                int bit = gy * 9 + gx;
                bool on = (m_fingerprint[static_cast<size_t>(bit / 8)] >> (bit % 8)) & 1;
                if (!on)
                    continue;
                Rect dot { grid.x + cell * (static_cast<float>(gx) + 1.0f) + 2.0f,
                    grid.y + cell * (static_cast<float>(gy) + 1.0f) + 2.0f, cell - 4.0f,
                    cell - 4.0f };
                bool corner = (gx < 2 || gx > 6) && (gy < 2 || gy > 6);
                r.roundRect(dot, theme::r1 * 0.6f, corner ? theme::accent : theme::fg3);
            }
        }

        TextStyle caption;
        caption.size = theme::textSm;
        caption.color = theme::fg3;
        caption.leading = theme::leadingNormal;
        r.textWrapped(Rect { panel.centerX() - 180.0f, grid.bottom() + theme::s6, 360.0f,
                          130.0f },
            "A picture of this console's id. It lives on the SD card, is tied to nothing "
            "about the hardware, and you can throw it away.",
            caption, 4, Align::Center);
    }

    static std::array<uint8_t, 11> fingerprintBits()
    {
        std::array<uint8_t, 11> bits {};
        uint8_t digest[32];
        sha256Over({ "nx-plaza/fingerprint", identity().id }, digest);
        for (size_t i = 0; i < bits.size(); i++)
            bits[i] = digest[i];
        return bits;
    }

    std::string m_handle;
    std::array<uint8_t, 11> m_fingerprint {};
    int m_action = 0;
    float m_pulse = 0.0f;
    float m_intro = 0.0f;
};

} // namespace

std::unique_ptr<Scene> makeFirstRunScene()
{
    return std::unique_ptr<Scene>(new FirstRunScene());
}

} // namespace nxp
