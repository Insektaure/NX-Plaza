#include "app.h"
#include "core/store.h"
#include "core/util.h"
#include "core/wallet.h"
#include "scenes/scene.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace nxp {

namespace {

    // One thick stroke at any angle: a band with a disc at each end, the way
    // the star and the runner icons are built.
    void stroke(Renderer& r, float x1, float y1, float x2, float y2, float thick,
        Color ink)
    {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f)
            return;
        float nx = -dy / len * thick * 0.5f;
        float ny = dx / len * thick * 0.5f;
        const float corners[8] = { x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx,
            y2 - ny, x1 - nx, y1 - ny };
        r.band(corners, ink);
        r.circle(x1, y1, thick * 0.5f, ink);
        r.circle(x2, y2, thick * 0.5f, ink);
    }

    uint32_t randomBelow(uint32_t n)
    {
        uint32_t bits = 0;
        randomBytes(&bits, sizeof(bits));
        return n == 0 ? 0 : bits % n;
    }

    // A one armed bandit, in two machines.
    //
    // Three reels either way. The three symbol machine pays small and often -
    // a triple every ninth spin - and the five symbol one pays rarely and big,
    // a triple every twenty-fifth. Same five coin stake for both, so the choice
    // is what kind of evening you want rather than what you can afford:
    //
    //     three symbols: (40 + 25 + 15 + 18*2) / 27  = 4.30 against 5
    //     five symbols:  (120 + 80 + 50 + 30 + 30
    //                     + 12*10 + 48*2) / 125      = 4.21 against 5
    //
    // which is fourteen and sixteen per cent to the plaza. The paytable down
    // the right is drawn from the very arrays the payout is read out of, so the
    // board on the wall cannot promise something the machine does not pay.
    //
    // As everywhere else, the reels are decided before they move: three numbers
    // out of randomBytes, and the spin is played backwards from them.
    class SlotsScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Spin = Touch_SceneBase,
            Zone_Stake,
            Zone_Machine,
            Zone_Back,
        };

        bool coversChrome() const override { return true; }

        // Two and a bit seconds with coins already in the slot.
        bool blocksExit() const override { return m_phase == Phase_Spin; }

        void onEnter(App& app) override
        {
            (void)app;
            m_phase = Phase_Ready;
            m_five = false;
            m_staked = false;
            m_paid = 0;
            m_button = 0;
            m_clock = 0.0f;
            for (int i = 0; i < kReels; i++) {
                m_reel[i] = int(randomBelow(uint32_t(symbols())));
                m_pos[i] = float(m_reel[i]);
                m_from[i] = m_pos[i];
                m_sweep[i] = 0.0f;
            }
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            m_clock += dt;

            if (m_phase == Phase_Spin) {
                for (int i = 0; i < kReels; i++) {
                    float u = std::min(m_clock / stopAt(i), 1.0f);
                    float back = 1.0f - u;
                    m_pos[i] = m_from[i] + m_sweep[i] * (1.0f - back * back * back);
                }
                if (m_clock >= stopAt(kReels - 1) + kHold)
                    settle(app);
                return;
            }

            if (Wallet::get().balance() < kStake)
                m_button = 0;

            TouchTarget tap;
            if (app.takeTap(tap)) {
                if (tap.is(Zone_Back)) {
                    app.popOverlay();
                    return;
                }
                if (tap.is(Zone_Machine)) {
                    switchMachine();
                } else if (tap.is(Zone_Spin)) {
                    m_button = 0;
                    begin(app, false);
                } else if (tap.is(Zone_Stake)) {
                    m_button = 1;
                    begin(app, true);
                }
                return;
            }
            if (input.back()) {
                app.popOverlay();
                return;
            }
            if (input.pressed(HidNpadButton_X)) {
                switchMachine();
                return;
            }
            if (input.navLeft)
                m_button = 0;
            if (input.navRight && Wallet::get().balance() >= kStake)
                m_button = 1;
            if (input.accept())
                begin(app, m_button == 1);
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);
            drawRoom(r);
            drawCabinet(app, r);
            drawPaytable(r);

            switch (m_phase) {
            case Phase_Ready:
                drawReady(app, r);
                break;
            case Phase_Spin:
                app.hint("B", "-");
                break;
            case Phase_Done:
                drawResult(app, r);
                break;
            }
        }

    private:
        enum Phase : int {
            Phase_Ready = 0,
            Phase_Spin,
            Phase_Done,
        };

        // Reel order, and the order the paytable lists them in.
        enum Symbol : int {
            Sym_Sun = 0,
            Sym_Moon,
            Sym_Bell,
            Sym_Seven,
            Sym_Bar,
            Sym_Count,
        };

        static constexpr int kReels = 3;
        static constexpr uint32_t kStake = 5;
        // What three of a kind pays, per symbol, per machine. The three symbol
        // machine only ever uses the first three.
        static constexpr uint32_t kTripleThree[Sym_Count] = { 40, 25, 15, 0, 0 };
        static constexpr uint32_t kTripleFive[Sym_Count] = { 30, 30, 50, 120, 80 };
        static constexpr uint32_t kPair = 2;
        // Two sevens and nothing else: the near miss with its own line, and the
        // detail that makes a five symbol machine feel like a machine.
        static constexpr uint32_t kTwoSevens = 10;

        // Reels stop left to right, a third of a second apart.
        static constexpr float kStop = 1.0f;
        static constexpr float kStagger = 0.35f;
        static constexpr float kHold = 0.6f;
        static constexpr float kTurns = 6.0f; // whole times round before it lands

        // The cabinet on its pedestal: body 160 to 560, then a 210px stand down
        // to a base plate at 770 - which is well onto the carpet that starts at
        // 700, so the machine is planted on the floor rather than perched at
        // the line where the wall meets it.
        static constexpr float kCabX = 380.0f;
        static constexpr float kCabY = 160.0f;
        static constexpr float kCabW = 660.0f;
        static constexpr float kCabH = 400.0f;
        static constexpr float kBaseY = 770.0f; // where the foot meets the floor
        static constexpr float kCell = 170.0f;  // one symbol's slot on a reel
        static constexpr float kWinY = 282.0f;  // the window's top
        static constexpr float kWinH = 168.0f;

        int symbols() const { return m_five ? Sym_Count : 3; }

        const uint32_t* triples() const
        {
            return m_five ? kTripleFive : kTripleThree;
        }

        float stopAt(int reel) const { return kStop + kStagger * float(reel); }

        void switchMachine()
        {
            m_five = !m_five;
            m_phase = Phase_Ready;
            m_paid = 0;
            m_staked = false;
            // The old reels may name symbols this machine does not have.
            for (int i = 0; i < kReels; i++) {
                m_reel[i] = int(randomBelow(uint32_t(symbols())));
                m_pos[i] = float(m_reel[i]);
                m_from[i] = m_pos[i];
            }
        }

        // What a line pays, gross. The only place the rules live.
        uint32_t payout(int a, int b, int c) const
        {
            if (a == b && b == c)
                return triples()[size_t(a)];
            if (m_five) {
                int sevens = (a == Sym_Seven) + (b == Sym_Seven) + (c == Sym_Seven);
                if (sevens == 2)
                    return kTwoSevens;
            }
            if (a == b || b == c || a == c)
                return kPair;
            return 0;
        }

        uint32_t landedPayout() const
        {
            return payout(m_reel[0], m_reel[1], m_reel[2]);
        }

        void begin(App& app, bool staked)
        {
            Wallet& wallet = Wallet::get();
            if (staked) {
                if (!wallet.spend(kStake)) {
                    app.toast(format("%u coins a spin", unsigned(kStake)),
                        "Ten arrive on each new day you open the app.");
                    return;
                }
                // On the card before the reels move, so walking out on a bad
                // spin still costs the stake.
                wallet.flush();
            }
            m_staked = staked;
            m_paid = 0;
            m_button = 0;

            int n = symbols();
            for (int i = 0; i < kReels; i++) {
                m_reel[i] = int(randomBelow(uint32_t(n)));
                m_from[i] = m_pos[i];
                // Six times round, then however far on this reel has to travel
                // to bring its own symbol to the window.
                float target = float(m_reel[i]);
                float delta = std::fmod(target - m_from[i], float(n));
                if (delta < 0.0f)
                    delta += float(n);
                m_sweep[i] = kTurns * float(n) + delta;
            }
            m_phase = Phase_Spin;
            m_clock = 0.0f;
        }

        void settle(App& app)
        {
            m_phase = Phase_Done;
            for (int i = 0; i < kReels; i++)
                m_pos[i] = float(m_reel[i]);

            if (!m_staked)
                return;
            uint32_t won = landedPayout();
            if (won == 0)
                return;
            Wallet& wallet = Wallet::get();
            wallet.award(won);
            wallet.flush();
            m_paid = won;
            (void)app;
        }

        // ----------------------------------------------------------- symbols

        // Drawn here rather than as icons because the moon is a crescent, and a
        // crescent is a disc with a bite taken out of it in whatever colour is
        // behind it - which only the caller knows.
        void drawSymbol(Renderer& r, const Rect& box, int symbol, Color ink,
            Color behind) const
        {
            switch (symbol) {
            case Sym_Sun:
                ui::icon(r, box, ui::Icon::Sun, ink, 3.0f);
                break;
            case Sym_Moon: {
                float radius = std::min(box.w, box.h) * 0.34f;
                r.circle(box.centerX(), box.centerY(), radius, ink);
                r.circle(box.centerX() + radius * 0.46f,
                    box.centerY() - radius * 0.16f, radius * 0.82f, behind);
                break;
            }
            case Sym_Bell:
                ui::icon(r, box, ui::Icon::Bell, ink, 3.0f);
                break;
            case Sym_Seven: {
                // Drawn, not typed: a slot seven is a shape - a thick top rail
                // and a diagonal falling off it - and a letter set in the
                // interface font would be the one symbol on the reel that came
                // from a different world than the others.
                float s = std::min(box.w, box.h);
                float cx = box.centerX();
                float cy = box.centerY();
                float thick = s * 0.15f;
                stroke(r, cx - s * 0.24f, cy - s * 0.28f, cx + s * 0.26f,
                    cy - s * 0.28f, thick, ink);
                stroke(r, cx + s * 0.24f, cy - s * 0.26f, cx - s * 0.04f,
                    cy + s * 0.32f, thick, ink);
                break;
            }
            case Sym_Bar:
            default: {
                // The triple bar, which is what a bar is on a reel.
                float s = std::min(box.w, box.h);
                float w = s * 0.62f;
                float h = s * 0.15f;
                float gap = s * 0.07f;
                for (int i = -1; i <= 1; i++) {
                    r.roundRect(Rect { box.centerX() - w * 0.5f,
                                   box.centerY() + float(i) * (h + gap) - h * 0.5f, w,
                                   h },
                        h * 0.34f, ink);
                }
                break;
            }
            }
        }

        static const char* symbolName(int symbol)
        {
            switch (symbol) {
            case Sym_Sun:
                return "suns";
            case Sym_Moon:
                return "moons";
            case Sym_Bell:
                return "bells";
            case Sym_Seven:
                return "sevens";
            default:
                return "bars";
            }
        }

        // ---------------------------------------------------------- painting

        void drawRoom(Renderer& r) const
        {
            // A room rather than a gradient: papered wall, a dado rail with
            // panelling under it, sconces either side, a rail of bulbs along
            // the ceiling and a patterned carpet. Every colour is a palette
            // colour, so it follows the theme, and all of it is kept at low
            // contrast - the reels and the board on the wall are what the eye
            // is supposed to find.
            constexpr float kFloorY = 700.0f;
            constexpr float kDado = 560.0f;

            r.gradientRect(Rect { 0.0f, 0.0f, Renderer::DesignWidth, kFloorY },
                theme::bg1, theme::bg0);

            // Harlequin paper: two sets of diagonals crossing, clipped to the
            // wall above the dado.
            //
            // Bands with no discs on the ends: stroke() would put a cap at
            // each end of all fifty-six lines, and every one of those ends is
            // outside the clip anyway. That is a hundred and twelve draws the
            // wall does not need.
            r.pushClipVertical(Rect { 0.0f, 0.0f, Renderer::DesignWidth, kDado });
            Color paper = theme::bg2.scaleAlpha(0.5f);
            for (int lean = 0; lean < 2; lean++) {
                float slope = lean == 0 ? 1.0f : -1.0f;
                for (int i = -6; i < 22; i++) {
                    float x = float(i) * 150.0f;
                    float x2 = x + slope * (kDado + 80.0f);
                    const float line[8] = { x - 1.0f, -40.0f, x2 - 1.0f, kDado + 40.0f,
                        x2 + 1.0f, kDado + 40.0f, x + 1.0f, -40.0f };
                    r.band(line, paper);
                }
            }
            r.popClip();

            // The rail, and the panelling below it.
            r.rect(Rect { 0.0f, kDado, Renderer::DesignWidth, 10.0f },
                theme::accentTint);
            r.rect(Rect { 0.0f, kDado + 10.0f, Renderer::DesignWidth, 3.0f },
                theme::bg3);
            for (int i = 0; i < 8; i++) {
                Rect panel { 24.0f + float(i) * 240.0f, kDado + 34.0f, 196.0f,
                    kFloorY - kDado - 60.0f };
                r.roundRect(panel, theme::r2, theme::bg1);
                r.strokeRect(panel, theme::r2, theme::stroke, theme::bg3);
            }

            // Bulbs along the ceiling, which is the cheapest thing that says
            // this is not a plaza. One wide glow behind the row rather than a
            // glow each: a radial falloff is the most expensive call here, and
            // fifteen of them across the top looks the same as one.
            r.glow(Rect { -40.0f, -30.0f, Renderer::DesignWidth + 80.0f, 200.0f },
                theme::accentGlow.scaleAlpha(0.20f), 1.6f);
            for (int i = 0; i < 15; i++)
                r.circle(60.0f + float(i) * 128.0f, 64.0f, 9.0f, theme::mark);

            // A sconce on either side of the machine: bracket, shade, light.
            drawSconce(r, 180.0f);
            drawSconce(r, 1130.0f);

            // Carpet: diamonds in two tones, which is what a casino floor is.
            r.gradientRect(Rect { 0.0f, kFloorY, Renderer::DesignWidth,
                               Renderer::DesignHeight - kFloorY },
                theme::bg2, theme::bg1);
            for (int row = 0; row < 3; row++) {
                float cy = kFloorY + 60.0f + float(row) * 120.0f;
                for (int i = 0; i < 9; i++) {
                    float cx = 40.0f + float(i) * 230.0f + (row % 2 ? 115.0f : 0.0f);
                    diamond(r, cx, cy, 86.0f, 52.0f,
                        theme::bg3.scaleAlpha((row + i) % 2 ? 0.55f : 0.28f));
                }
            }

            // And the light hanging over the machine.
            r.glow(Rect { kCabX + kCabW * 0.5f - 420.0f, -120.0f, 840.0f, 700.0f },
                theme::accentGlow.scaleAlpha(0.16f), 2.0f);
        }

        // Two trapezoids back to back: the renderer has no rotated rectangle,
        // and a diamond is the one shape that does not need one.
        static void diamond(Renderer& r, float cx, float cy, float w, float h,
            Color ink)
        {
            r.trapezoid(cy - h * 0.5f, cx, cx, cy, cx - w * 0.5f, cx + w * 0.5f, ink);
            r.trapezoid(cy, cx - w * 0.5f, cx + w * 0.5f, cy + h * 0.5f, cx, cx, ink);
        }

        static void drawSconce(Renderer& r, float x)
        {
            constexpr float kY = 360.0f;
            r.rect(Rect { x - 4.0f, kY, 8.0f, 46.0f }, theme::bg3);
            // The shade, wider at the bottom, with the light inside it.
            r.trapezoid(kY + 40.0f, x - 20.0f, x + 20.0f, kY + 96.0f, x - 46.0f,
                x + 46.0f, theme::bg2);
            r.glow(Rect { x - 110.0f, kY + 20.0f, 220.0f, 220.0f },
                theme::accentGlow.scaleAlpha(0.28f), 1.9f);
            r.circle(x, kY + 96.0f, 13.0f, theme::mark);
        }

        void drawCabinet(App& app, Renderer& r)
        {
            Rect cab { kCabX, kCabY, kCabW, kCabH };

            // The stand first, so the body sits on top of it: a waisted column
            // from the cabinet down to a plinth on the carpet, with the shadow
            // under that.
            float cx = cab.centerX();
            r.ellipse(cx, kBaseY + 24.0f, 230.0f, 28.0f, theme::bg0.scaleAlpha(0.34f),
                0.0f);
            // Narrow at the waist, flaring at both ends - two trapezoids doing
            // the work of a turned column.
            float waist = (cab.bottom() + kBaseY) * 0.5f;
            r.trapezoid(cab.bottom() - 8.0f, cx - 132.0f, cx + 132.0f, waist,
                cx - 62.0f, cx + 62.0f, theme::bg3);
            r.trapezoid(waist, cx - 62.0f, cx + 62.0f, kBaseY - 18.0f, cx - 120.0f,
                cx + 120.0f, theme::bg3);
            r.roundRect(Rect { cx - 150.0f, kBaseY - 22.0f, 300.0f, 26.0f }, 10.0f,
                theme::bg2);
            r.roundRect(Rect { cx - 176.0f, kBaseY - 2.0f, 352.0f, 20.0f }, 9.0f,
                theme::bg3);

            // Body: lit from above, with a chrome rail down each side.
            r.gradientRect(cab, theme::bg2, theme::bg1, theme::r5, theme::r4);
            r.strokeRect(cab, theme::r5, 3.0f, theme::stroke3);
            for (int side = 0; side < 2; side++) {
                float x = side == 0 ? cab.x + 14.0f : cab.right() - 32.0f;
                r.roundRect(Rect { x, cab.y + 26.0f, 18.0f, cab.h - 52.0f }, 9.0f,
                    theme::bg3.scaleAlpha(0.8f));
            }
            app.touchZone(Rect { cab.x, cab.y, cab.w, 108.0f }, Zone_Machine);

            // Marquee, with a row of bulbs over it - which is the detail that
            // makes a cabinet look like a cabinet.
            Rect sign { cab.x + 66.0f, cab.y + 34.0f, cab.w - 132.0f, 68.0f };
            r.roundRect(sign, theme::r2, theme::bg0);
            r.strokeRect(sign, theme::r2, theme::stroke, theme::accentTint);
            for (int i = 0; i < 9; i++) {
                float x = sign.x + 22.0f + float(i) * ((sign.w - 44.0f) / 8.0f);
                r.circle(x, sign.y - 12.0f, 5.0f, theme::mark);
            }
            TextStyle name;
            name.size = theme::textMd;
            name.weight = FontWeight::Bold;
            name.color = theme::accent;
            name.tracking = theme::trackingWide;
            r.text(sign, m_five ? "FIVE SYMBOLS" : "THREE SYMBOLS", name, Align::Center,
                VAlign::Middle);

            // The window, its reels, and a double bezel.
            Rect glass { cab.x + 58.0f, kWinY, cab.w - 116.0f, kWinH };
            r.roundRect(glass.inset(-8.0f, -8.0f), theme::r3, theme::bg3);
            r.roundRect(glass, theme::r2, theme::bg0);
            float cell = (glass.w - 24.0f) / 3.0f;
            for (int i = 0; i < kReels; i++) {
                Rect slot { glass.x + 12.0f + float(i) * cell, glass.y, cell - 8.0f,
                    glass.h };
                drawReel(r, slot, i);
            }
            // Sheen across the glass: one flat band at a low alpha, so it
            // reads as a pane without hiding what is behind it.
            const float sheen[8] = { glass.x, glass.bottom(), glass.x + glass.w * 0.42f,
                glass.y, glass.x + glass.w * 0.62f, glass.y, glass.x + glass.w * 0.2f,
                glass.bottom() };
            r.band(sheen, theme::fg1.scaleAlpha(0.05f));
            r.strokeRect(glass, theme::r2, 3.0f, theme::accentTint);

            // Payline arrows, pointing at the row that counts.
            for (int side = 0; side < 2; side++) {
                float tip = side == 0 ? glass.x - 12.0f : glass.right() + 12.0f;
                float back = side == 0 ? tip - 20.0f : tip + 20.0f;
                r.trapezoid(glass.centerY() - 13.0f, back, back, glass.centerY() + 13.0f,
                    tip, tip, theme::accent);
            }

            // A coin slot, and the tray it pays into.
            r.roundRect(Rect { cx - 46.0f, kWinY + kWinH + 22.0f, 92.0f, 14.0f },
                7.0f, theme::bg0);
            Rect tray { cab.x + 132.0f, kWinY + kWinH + 48.0f, cab.w - 264.0f, 50.0f };
            r.roundRect(tray, theme::r2, theme::bg0.scaleAlpha(0.65f));
            r.rect(Rect { tray.x + 12.0f, tray.y + 8.0f, tray.w - 24.0f, 3.0f },
                theme::bg3.scaleAlpha(0.7f));

            drawLever(r, cab);
        }

        void drawReel(Renderer& r, const Rect& slot, int reel) const
        {
            int n = symbols();
            float p = m_pos[reel];
            int base = int(std::floor(p));
            float frac = p - float(base);

            r.pushClipVertical(slot);
            for (int k = -2; k <= 2; k++) {
                int idx = ((base + k) % n + n) % n;
                float y = slot.centerY() + (float(k) - frac) * kCell;
                Rect box { slot.x, y - 60.0f, slot.w, 120.0f };
                if (box.bottom() < slot.y || box.y > slot.bottom())
                    continue;
                bool centre = k == 0 && frac < 0.5f;
                drawSymbol(r, box, idx,
                    centre && m_phase == Phase_Done ? theme::accent : theme::fg1,
                    theme::bg0);
            }
            r.popClip();

            // The line the machine is read along.
            r.rect(Rect { slot.x, slot.centerY() - 1.0f, slot.w, 2.0f },
                theme::accentTint.scaleAlpha(0.5f));
        }

        void drawLever(Renderer& r, const Rect& cab) const
        {
            // Pulled while the reels are turning, back up when they stop: the
            // one moving part a bandit is named for.
            float u = m_phase == Phase_Spin ? std::min(m_clock / 0.22f, 1.0f) : 0.0f;
            float angle = -1.15f + u * 1.9f;
            float px = cab.right() + 16.0f;
            float py = cab.y + 190.0f;
            float len = 128.0f;
            float tx = px + std::sin(angle) * len * 0.55f;
            float ty = py + std::cos(angle) * len;

            float dx = tx - px;
            float dy = ty - py;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length > 1e-4f) {
                float nx = -dy / length * 7.0f;
                float ny = dx / length * 7.0f;
                const float corners[8] = { px + nx, py + ny, tx + nx, ty + ny,
                    tx - nx, ty - ny, px - nx, py - ny };
                r.band(corners, theme::bg3);
            }
            r.circle(px, py, 12.0f, theme::bg3);
            r.circle(tx, ty, 22.0f, theme::accent);
        }

        // Every line the machine pays, from the same arrays payout() reads.
        //
        // A board on the wall, not a wall of board: this was 640 by 800 - a
        // third of the screen for seven lines - with 56px symbols and a
        // paragraph of prose under them. Forty pixel symbols on a 48px pitch
        // say the same thing in 380 by 450, and what is left of the prose is
        // the one number worth knowing, which is how often a triple lands.
        void drawPaytable(Renderer& r) const
        {
            Rect panel { 1420.0f, 150.0f, 380.0f, 450.0f };
            r.roundRect(panel, theme::r3, theme::bg1);
            r.strokeRect(panel, theme::r3, theme::stroke, theme::stroke2);
            Rect inner = panel.inset(theme::s5, theme::s5);
            float y = inner.y;

            ui::eyebrow(r, Rect { inner.x, y, inner.w, 28.0f }, "what it pays");
            y += 40.0f;

            int n = symbols();
            TextStyle amount;
            amount.size = theme::textBase;
            amount.weight = FontWeight::Bold;
            amount.color = theme::fg1;

            // Three of a kind, best first.
            constexpr float kMark = 40.0f;
            constexpr float kRow = 48.0f;
            int order[Sym_Count] = { Sym_Seven, Sym_Bar, Sym_Bell, Sym_Sun, Sym_Moon };
            for (int slot = 0; slot < Sym_Count; slot++) {
                int sym = order[slot];
                if (sym >= n)
                    continue;
                uint32_t pays = triples()[size_t(sym)];
                if (pays == 0)
                    continue;

                for (int i = 0; i < 3; i++) {
                    Rect box { inner.x + float(i) * (kMark + 6.0f), y, kMark, kMark };
                    drawSymbol(r, box, sym, theme::fg2, theme::bg1);
                }
                r.text(Rect { inner.x, y, inner.w, kMark }, format("%u", unsigned(pays)),
                    amount, Align::Right, VAlign::Middle);
                y += kRow;
            }

            y += theme::s3;
            ui::divider(r, inner.x, y, inner.w);
            y += theme::s3;

            TextStyle line;
            line.size = theme::textSm;
            line.color = theme::fg2;
            if (m_five) {
                r.text(inner.x, y, "Two sevens", line);
                r.text(Rect { inner.x, y, inner.w, line.size * theme::leadingSnug },
                    format("%u", unsigned(kTwoSevens)), amount, Align::Right,
                    VAlign::Top);
                y += line.size * theme::leadingNormal + 6.0f;
            }
            r.text(inner.x, y, "Any two the same", line);
            r.text(Rect { inner.x, y, inner.w, line.size * theme::leadingSnug },
                format("%u", unsigned(kPair)), amount, Align::Right, VAlign::Top);
            y += line.size * theme::leadingNormal + theme::s3;

            TextStyle note;
            note.size = theme::textXs;
            note.color = theme::fg4;
            note.tracking = theme::trackingWide;
            r.text(inner.x, y,
                m_five ? "a triple about every 25 spins" : "a triple about every 9",
                note);
        }

        Rect plate(float width, float height) const
        {
            // On the right, under the board on the wall: the machine keeps the
            // middle of the room and the words that go with it stand beside
            // it, rather than across its feet.
            return Rect { 1860.0f - width, Renderer::DesignHeight - 140.0f - height,
                width, height };
        }

        void drawReady(App& app, Renderer& r)
        {
            uint32_t coins = Wallet::get().balance();
            app.hint("A", "spin");
            app.hint("X", m_five ? "three symbols" : "five symbols");
            app.hint("B", "back");

            Rect box = plate(800.0f, 250.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.94f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s6, theme::s5);
            float y = inner.y;

            TextStyle title;
            title.size = theme::textXl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingSnug;
            r.text(inner.x, y, "The bandit", title);
            y += title.size * theme::leadingSnug + 4.0f;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            y += r.textWrapped(Rect { inner.x, y, inner.w, 80.0f },
                format("%u coins a spin, and %u to spend. X changes machines.",
                    unsigned(kStake), unsigned(coins)),
                body, 2);

            drawButtons(app, r,
                Rect { inner.x, std::max(inner.bottom() - kButton, y + theme::s4),
                    inner.w, kButton },
                "Spin for free", coins >= kStake);
            drawBack(app, r, box);
        }

        void drawResult(App& app, Renderer& r)
        {
            app.hint("A", "spin");
            app.hint("X", m_five ? "three symbols" : "five symbols");
            app.hint("B", "back");

            Rect box = plate(800.0f, 250.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.96f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s6, theme::s5);
            float y = inner.y;

            uint32_t would = landedPayout();
            bool triple = m_reel[0] == m_reel[1] && m_reel[1] == m_reel[2];

            TextStyle title;
            title.size = theme::textXl;
            title.weight = FontWeight::Bold;
            title.color = would > 0 ? theme::accent : theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingSnug;
            std::string headline;
            if (triple)
                headline = format("Three %s", symbolName(m_reel[0]));
            else if (would > 0)
                headline = "A pair";
            else
                headline = "Nothing";
            r.text(inner.x, y, headline, title);
            y += title.size * theme::leadingSnug + 4.0f;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            std::string line;
            if (m_staked) {
                line = m_paid > 0
                    ? format("%u coins, and %u to spend.", unsigned(m_paid),
                          unsigned(Wallet::get().balance()))
                    : format("Five gone. %u left.", unsigned(Wallet::get().balance()));
            } else {
                line = would > 0
                    ? format("Nothing staked, so nothing won - it would have paid %u.",
                          unsigned(would))
                    : "Nothing staked, and nothing to stake it on.";
            }
            y += r.textWrapped(Rect { inner.x, y, inner.w, 80.0f }, line, body, 2);

            drawButtons(app, r,
                Rect { inner.x, std::max(inner.bottom() - kButton, y + theme::s4),
                    inner.w, kButton },
                "Spin again", Wallet::get().balance() >= kStake);
            drawBack(app, r, box);
        }

        void drawButtons(App& app, Renderer& r, const Rect& row, const char* plainLabel,
            bool canStake)
        {
            std::string staked = format("Bet %u coins", unsigned(kStake));
            float plainW = ui::actionButtonWidth(r, plainLabel);
            float stakeW = ui::actionButtonWidth(r, staked);

            Rect plain { row.x, row.y, plainW, row.h };
            Rect stake { std::max(plain.right() + theme::s4, row.right() - stakeW),
                row.y, stakeW, row.h };

            app.touchZone(plain, Zone_Spin);
            if (canStake)
                app.touchZone(stake, Zone_Stake);

            float pulse = 0.7f + 0.3f * m_pulse;
            bool onStake = m_button == 1 && canStake;
            ui::actionButton(r, plain, plainLabel, !onStake,
                app.touchHeld(Zone_Spin) ? 1.0f : (onStake ? 0.0f : pulse));
            ui::actionButton(r, stake, staked, onStake,
                canStake && app.touchHeld(Zone_Stake) ? 1.0f
                                                      : (onStake ? pulse : 0.0f));
        }

        void drawBack(App& app, Renderer& r, const Rect& box)
        {
            Rect back { box.right() - 60.0f, box.y + 18.0f, 42.0f, 42.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);
        }

        static constexpr float kButton = 76.0f;

        int m_phase = Phase_Ready;
        bool m_five = false;
        float m_clock = 0.0f;
        float m_pulse = 0.0f;
        int m_button = 0;
        bool m_staked = false;
        uint32_t m_paid = 0;

        int m_reel[kReels] = { 0, 0, 0 };
        float m_pos[kReels] = { 0.0f, 0.0f, 0.0f };
        float m_from[kReels] = { 0.0f, 0.0f, 0.0f };
        float m_sweep[kReels] = { 0.0f, 0.0f, 0.0f };
    };
}

std::unique_ptr<Scene> makeSlotsScene() { return std::make_unique<SlotsScene>(); }

} // namespace nxp
