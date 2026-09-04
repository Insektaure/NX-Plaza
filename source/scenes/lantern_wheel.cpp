#include "app.h"
#include "core/pieces.h"
#include "core/store.h"
#include "core/util.h"
#include "core/wallet.h"
#include "scenes/scene.h"
#include "ui/plaza_scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace nxp {

namespace {

    constexpr float kTau = 6.2831853f;

    // The clown's own two colours. Hardcoded for the same reason the dice are
    // ivory: a face that changed with the theme would stop reading as a face,
    // and paint does not follow the light.
    const Color kGreasepaint = Color::hex(0xF6EFE3);
    const Color kInk = Color::hex(0x33302B);

    uint32_t randomBelow(uint32_t n)
    {
        uint32_t bits = 0;
        randomBytes(&bits, sizeof(bits));
        return n == 0 ? 0 : bits % n;
    }

    // Twelve lanterns and a needle.
    //
    // Every lantern pays something, and two of the twelve pay a puzzle piece.
    // That is what the wheel is for: one chance in six at the thing worth
    // wanting, with coins as the consolation. Against a ten coin stake:
    //
    //     (8*1 + 2*3 + 2*50) / 12 = 9.50
    //
    // taking a piece at the fifty the shop charges for one. Half a coin a spin
    // to the plaza, and a piece off the wheel costs six spins on average -
    // sixty coins, less the consolations - against the shop's fifty, so the
    // shop stays the sensible way to buy one and this is the romantic way.
    //
    // The landing is chosen before the needle moves and the spin is solved
    // backwards from it, so no amount of dropped frames can change the prize.
    class LanternWheelScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Spin = Touch_SceneBase,
            Zone_Stake,
            Zone_Back,
        };

        bool coversChrome() const override { return true; }

        // A spin is two and a half seconds with coins already staked.
        bool blocksExit() const override { return m_phase == Phase_Spin; }

        void onEnter(App& app) override
        {
            (void)app;
            m_phase = Phase_Ready;
            m_angle = -kTau * 0.25f; // the needle starts at the top lantern
            m_landed = -1;
            m_paid = 0;
            m_piece = false;
            m_staked = false;
            m_button = 0;
            m_clock = 0.0f;
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            m_clock += dt;

            if (m_phase == Phase_Spin) {
                if (m_clock >= kSpin + kHold)
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
                if (tap.is(Zone_Spin)) {
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
            // No lamp posts: a row of lit posts behind a ring of lit lanterns
            // is one glowing circle too many.
            ui::plazaBackdrop(r, 0.0f, kHorizon, 0.0f);
            ui::plazaGround(r, 0.0f, kHorizon);
            drawTent(r);
            drawClown(r);
            drawStand(r);

            drawRing(r);
            drawNeedle(r);

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

        static constexpr int kLanterns = 12;
        static constexpr uint32_t kStake = 10;
        // The board, in the order the lanterns are drawn. A zero means a
        // puzzle piece.
        //
        // Two pieces, opposite each other, and coins in two sizes so the ring
        // is not twelve of the same thing. The coin budget is the whole
        // constraint: at the fifty the shop charges for a piece, two in twelve
        // is 8.33 of value before a single coin is added, so a ten coin spin
        // has about a coin and a half of room left for the other ten lanterns.
        // Eight ones and two threes spends fourteen of it and leaves the plaza
        // half a coin a spin; ten twos instead would be exactly even money,
        // and a wheel with no edge stops draining coins at all.
        static constexpr uint32_t kPrize[kLanterns] = {
            1, 1, 3, 0, 1, 1, 1, 3, 1, 0, 1, 1,
        };

        static bool piecePays(int index)
        {
            return kPrize[size_t(index)] == 0;
        }
        // What the piece lantern pays when every puzzle is already finished
        // and there is no piece left to hand over.
        static constexpr uint32_t kInsteadOfPiece = 25;

        // The ring sits above the plate and the plate above the hint strip.
        // 330 +/- 240 is 90 to 570, and a label hangs 46 below that, which
        // clears the plate at 672. It used to be a 260px ring centred at 470,
        // whose labels ran 36px into the plate.
        static constexpr float kHorizon = 600.0f;
        static constexpr float kCentreX = 960.0f;
        static constexpr float kCentreY = 645.0f;
        static constexpr float kRadius = 240.0f;
        static constexpr float kLampR = 34.0f;
        static constexpr float kButton = 76.0f;

        // Two and a half seconds of slowing down, then a moment on the answer.
        static constexpr float kSpin = 2.5f;
        static constexpr float kHold = 0.55f;
        static constexpr float kTurns = 4.0f; // whole turns before it settles

        // A big top, off to the left, standing on the same ground the plaza's
        // lamp posts stand on.
        //
        // `trapezoid` takes its top and bottom widths separately, so a stripe
        // that narrows to a point is one call and a fan of them from an apex
        // is a conical roof. Amber and cream rather than the circus red: red
        // is not in this palette and would fight the lanterns.
        void drawTent(Renderer& r) const
        {
            constexpr float kLeft = 140.0f;
            constexpr float kRight = 640.0f;
            constexpr float kApexX = (kLeft + kRight) * 0.5f;
            constexpr float kApexY = 320.0f;
            constexpr float kEaves = 455.0f;
            constexpr int kStripes = 8;

            // The pennant first, so the roof covers where its pole enters.
            r.rect(Rect { kApexX - 2.0f, kApexY - 52.0f, 4.0f, 60.0f }, theme::fg4);
            r.rect(Rect { kApexX + 2.0f, kApexY - 50.0f, 56.0f, 22.0f },
                theme::accent);

            // Walls, then the doorway cut into them as a dark arch.
            Rect walls { kLeft + 30.0f, kEaves, kRight - kLeft - 60.0f,
                kHorizon - kEaves };
            r.rect(walls, theme::bg1);
            r.rect(Rect { walls.x, walls.y, walls.w, theme::stroke }, theme::stroke2);
            Rect door { kApexX - 62.0f, kEaves + 34.0f, 124.0f, kHorizon - kEaves - 34.0f };
            r.roundRect(door, 58.0f, theme::bg0.scaleAlpha(0.85f));

            // The roof: a fan from the apex to the eaves.
            float step = (kRight - kLeft) / float(kStripes);
            for (int i = 0; i < kStripes; i++) {
                float x0 = kLeft + step * float(i);
                r.trapezoid(kApexY, kApexX, kApexX, kEaves, x0, x0 + step,
                    (i % 2) == 0 ? theme::accentTint : theme::bg2);
            }

            // And the scalloped hem along the eaves, which is what says canvas
            // rather than a striped roof.
            float bump = step * 0.5f;
            for (int i = 0; i < kStripes * 2; i++) {
                float x = kLeft + bump * float(i) + bump * 0.5f;
                r.circle(x, kEaves, bump * 0.5f,
                    (i % 2) == 0 ? theme::accent.scaleAlpha(0.5f) : theme::accentTint);
            }
        }

        // Somebody working the stall, over on the right.
        //
        // Built the way the Mii figures are - trapezoids for anything that
        // tapers, circles for anything round, bands for the arms - and dressed
        // in the app's own two colours rather than circus red and green, which
        // would be the only red and green in the whole interface.
        void drawClown(Renderer& r) const
        {
            constexpr float x = 1560.0f;
            constexpr float kFeet = 660.0f;

            r.ellipse(x, kFeet + 8.0f, 74.0f, 13.0f, theme::bg0.scaleAlpha(0.28f),
                0.0f);

            // Boots, which on a clown are the widest thing about him.
            r.ellipse(x - 36.0f, kFeet - 6.0f, 48.0f, 15.0f, kInk, 0.0f);
            r.ellipse(x + 36.0f, kFeet - 6.0f, 48.0f, 15.0f, kInk, 0.0f);

            // Trousers: wider at the ankle than the hip, which is the whole
            // silhouette.
            r.trapezoid(560.0f, x - 42.0f, x - 6.0f, kFeet - 12.0f, x - 62.0f,
                x - 12.0f, theme::teal);
            r.trapezoid(560.0f, x + 6.0f, x + 42.0f, kFeet - 12.0f, x + 12.0f,
                x + 62.0f, theme::teal);

            // Body, narrow at the shoulders and wide at the hips.
            r.trapezoid(470.0f, x - 34.0f, x + 34.0f, 562.0f, x - 52.0f, x + 52.0f,
                theme::teal);
            r.circle(x, 498.0f, 8.0f, theme::accent);
            r.circle(x, 528.0f, 8.0f, theme::accent);

            // Arms: one up, one down, as bands with a disc at each end - the
            // same way the runner icon is drawn.
            arm(r, x - 32.0f, 486.0f, x - 100.0f, 446.0f);
            arm(r, x + 32.0f, 486.0f, x + 96.0f, 516.0f);

            // A collar of pompoms, then the head over the top of it.
            for (int i = -2; i <= 2; i++)
                r.circle(x + float(i) * 21.0f, 466.0f, 13.0f, theme::accent);

            r.circle(x, 430.0f, 35.0f, kGreasepaint);
            r.circle(x - 40.0f, 426.0f, 16.0f, theme::teal);
            r.circle(x + 40.0f, 426.0f, 16.0f, theme::teal);
            r.circle(x - 13.0f, 422.0f, 4.5f, kInk);
            r.circle(x + 13.0f, 422.0f, 4.5f, kInk);
            r.circle(x, 440.0f, 10.0f, theme::accent);
            // A grin, as three discs along an arc rather than a curve the
            // renderer cannot draw.
            r.circle(x - 11.0f, 452.0f, 3.5f, kInk);
            r.circle(x, 456.0f, 3.5f, kInk);
            r.circle(x + 11.0f, 452.0f, 3.5f, kInk);

            // Cone hat and its pompom.
            r.trapezoid(352.0f, x - 4.0f, x + 4.0f, 404.0f, x - 32.0f, x + 32.0f,
                theme::accent);
            r.circle(x, 348.0f, 11.0f, theme::teal);
        }

        static void arm(Renderer& r, float x1, float y1, float x2, float y2)
        {
            float dx = x2 - x1;
            float dy = y2 - y1;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-4f)
                return;
            float nx = -dy / len * 7.0f;
            float ny = dx / len * 7.0f;
            const float corners[8] = { x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx,
                y2 - ny, x1 - nx, y1 - ny };
            r.band(corners, theme::teal);
            r.circle(x1, y1, 7.0f, theme::teal);
            r.circle(x2, y2, 9.0f, kGreasepaint);
        }

        // What the wheel is mounted on, so it is standing at a stall rather
        // than hanging in the air.
        void drawStand(Renderer& r) const
        {
            // Down to a plinth in front of the ground line rather than onto it:
            // the wheel is nearer the camera than the tent is, so its feet
            // belong lower on the screen.
            constexpr float kFloor = 955.0f;

            // From the hub, not from the bottom of the ring. It used to start
            // at the rim, which left the axle - and the needle turning on it -
            // attached to nothing. The ring and its lanterns are drawn after
            // this, so the pole passes behind them.
            //
            // Tapered, because a post holding a wheel this size wants a wider
            // foot than neck.
            r.trapezoid(kCentreY, kCentreX - 13.0f, kCentreX + 13.0f, kFloor,
                kCentreX - 26.0f, kCentreX + 26.0f, theme::bg3);
            r.ellipse(kCentreX, kFloor + 6.0f, 96.0f, 16.0f,
                theme::bg0.scaleAlpha(0.30f), 0.0f);
            r.roundRect(Rect { kCentreX - 84.0f, kFloor - 16.0f, 168.0f, 26.0f }, 8.0f,
                theme::bg3);
        }

        static float lanternAngle(int index)
        {
            // Lantern 0 at the top, going clockwise.
            return -kTau * 0.25f + kTau * float(index) / float(kLanterns);
        }

        void begin(App& app, bool staked)
        {
            Wallet& wallet = Wallet::get();
            if (staked) {
                if (!wallet.spend(kStake)) {
                    app.toast(format("%u coins for a spin", unsigned(kStake)),
                        "Ten arrive on each new day you open the app.");
                    return;
                }
                // Written before the needle moves, so leaving mid-spin still
                // costs the stake.
                wallet.flush();
            }
            m_staked = staked;
            m_button = 0;

            m_landed = int(randomBelow(kLanterns));
            m_from = m_angle;
            // Solved backwards: whole turns plus however far round the winning
            // lantern is from where the needle happens to be resting.
            float target = lanternAngle(m_landed);
            float delta = std::fmod(target - m_from, kTau);
            if (delta < 0.0f)
                delta += kTau;
            m_sweep = kTurns * kTau + delta;

            m_phase = Phase_Spin;
            m_clock = 0.0f;
            m_paid = 0;
            m_piece = false;
        }

        // Eases out on a cubic, so it is fastest at the start and creeps the
        // last few degrees.
        float spinProgress() const
        {
            float u = std::min(m_clock / kSpin, 1.0f);
            float back = 1.0f - u;
            return 1.0f - back * back * back;
        }

        void settle(App& app)
        {
            m_phase = Phase_Done;
            if (m_landed < 0)
                return;

            // Where it stopped, which is what the needle points at from now
            // on. Without this, needleAngle() fell back to m_angle - still the
            // angle it was resting at before the spin - and the needle snapped
            // back to the top the instant the result appeared.
            m_angle = lanternAngle(m_landed);

            // Watched for nothing: the needle still lands somewhere and the
            // plate still says where, but nothing is handed over. A free spin
            // that paid would make the wheel a coin printer - eight coins a
            // go, for nothing - which is the one thing it must not be.
            if (!m_staked)
                return;

            if (piecePays(m_landed)) {
                Store::PiecePurchase got = app.store().buyPiece(false, kWheelSource);
                if (got.set >= 0) {
                    // The only thing a spin leaves behind, and so the only
                    // thing its own trophies can be about: a free go writes
                    // nothing, and a coin prize is indistinguishable from the
                    // race's or the dice's once it is in the wallet.
                    Store& store = app.store();
                    store.setScore("wheel_pieces",
                        store.bestScore("wheel_pieces") + 1);
                    m_piece = true;
                    m_pieceName = pieceSets()[size_t(got.set)].name;
                    m_pieceIndex = got.piece;
                    return;
                }
                // Every picture finished, so there is nothing to hand over.
                award(kInsteadOfPiece);
                return;
            }
            award(kPrize[size_t(m_landed)]);
        }

        void award(uint32_t coins)
        {
            if (coins == 0)
                return;
            Wallet& wallet = Wallet::get();
            wallet.award(coins);
            wallet.flush();
            m_paid = coins;
        }

        // ---------------------------------------------------------- painting

        float needleAngle() const
        {
            if (m_phase != Phase_Spin)
                return m_angle;
            return m_from + m_sweep * spinProgress();
        }

        void drawRing(Renderer& r) const
        {
            // The ring the lanterns hang on.
            r.strokeRect(Rect { kCentreX - kRadius, kCentreY - kRadius, kRadius * 2.0f,
                           kRadius * 2.0f },
                kRadius, theme::stroke, theme::stroke2);

            float needle = needleAngle();
            for (int i = 0; i < kLanterns; i++) {
                float a = lanternAngle(i);
                float x = kCentreX + std::cos(a) * kRadius;
                float y = kCentreY + std::sin(a) * kRadius;

                // Lit when the needle is on it: while it spins that is a tick
                // going round, and at the end it is the answer.
                float delta = std::fmod(std::abs(needle - a), kTau);
                if (delta > kTau * 0.5f)
                    delta = kTau - delta;
                bool under = delta < kTau / float(kLanterns) * 0.5f;
                bool won = m_phase == Phase_Done && i == m_landed;

                if (under || won) {
                    r.glow(Rect { x - 74.0f, y - 74.0f, 148.0f, 148.0f },
                        theme::accentGlow.scaleAlpha(won ? 0.5f : 0.3f), 1.7f);
                }
                r.circle(x, y, kLampR, under || won ? theme::mark : theme::bg2);
                r.circle(x - kLampR * 0.22f, y - kLampR * 0.26f, kLampR * 0.4f,
                    under || won ? theme::accentSoft : theme::bg3);

                // What it pays.
                TextStyle label;
                label.size = theme::textSm;
                label.weight = FontWeight::Bold;
                label.color = under || won ? theme::accent : theme::fg3;
                float out = kRadius + kLampR + 26.0f;
                float lx = kCentreX + std::cos(a) * out;
                float ly = kCentreY + std::sin(a) * out;

                // Placed by its own width, so a label to the right of the ring
                // starts at the ring and one to the left ends there, instead of
                // being centred on a point that leaves it half over the
                // lanterns.
                float cosA = std::cos(a);
                auto placeX = [cosA, lx](float w) {
                    if (cosA > 0.35f)
                        return lx;
                    if (cosA < -0.35f)
                        return lx - w;
                    return lx - w * 0.5f;
                };

                if (piecePays(i)) {
                    // The puzzle's own icon rather than the word.
                    constexpr float kMark = 36.0f;
                    ui::icon(r,
                        Rect { placeX(kMark), ly - kMark * 0.5f, kMark, kMark },
                        ui::Icon::Puzzle, label.color, 2.5f);
                } else {
                    std::string what = format("%u", unsigned(kPrize[size_t(i)]));
                    float w = r.measure(what, label);
                    r.text(Rect { placeX(w), ly - 13.0f, w, 26.0f }, what, label,
                        Align::Left, VAlign::Middle);
                }
            }
        }

        void drawNeedle(Renderer& r) const
        {
            float a = needleAngle();
            float reach = kRadius - kLampR - 10.0f;
            float tipX = kCentreX + std::cos(a) * reach;
            float tipY = kCentreY + std::sin(a) * reach;

            // A stroke at an angle is a band with a disc at each end - the
            // renderer has no rotated rectangle, and does not need one.
            float dx = tipX - kCentreX;
            float dy = tipY - kCentreY;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-4f) {
                float nx = -dy / len * 5.0f;
                float ny = dx / len * 5.0f;
                const float corners[8] = { kCentreX + nx, kCentreY + ny, tipX + nx,
                    tipY + ny, tipX - nx, tipY - ny, kCentreX - nx, kCentreY - ny };
                r.band(corners, theme::fg1);
            }
            r.circle(tipX, tipY, 9.0f, theme::accent);
            r.circle(kCentreX, kCentreY, 22.0f, theme::bg1);
            r.circle(kCentreX, kCentreY, 15.0f, theme::mark);
        }

        Rect plate(float width, float height) const
        {
            return Rect { Renderer::DesignWidth * 0.5f - width * 0.5f, 30.0f, width,
                height };
        }

        void drawReady(App& app, Renderer& r)
        {
            uint32_t coins = Wallet::get().balance();
            app.hint("A", "spin");
            app.hint("B", "back");

            // 57 of title, two 36px lines of body, a gap and a 76px button
            // row: 230 of interior, which 300 provides. The old plate was 210
            // and drew its body straight through the button.
            Rect box = plate(1100.0f, 280.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.94f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s5);
            float y = inner.y;

            TextStyle title;
            title.size = theme::textXl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingSnug;
            r.text(inner.x, y, "The lantern wheel", title);
            y += title.size * theme::leadingSnug + 6.0f;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            y += r.textWrapped(Rect { inner.x, y, inner.w, 90.0f },
                format("Watch it for nothing, or put %u coins on it. Every lantern "
                       "pays something and one of them is a piece. You have %u.",
                    unsigned(kStake), unsigned(coins)),
                body, 2);

            drawButtons(app, r,
                Rect { inner.x, std::max(inner.bottom() - kButton, y + theme::s4),
                    inner.w, kButton },
                "Spin for nothing", coins >= kStake);
            drawBack(app, r, box);
        }

        void drawResult(App& app, Renderer& r)
        {
            app.hint("A", "spin");
            app.hint("B", "back");

            Rect box = plate(1100.0f, 280.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.96f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s5);
            float y = inner.y;

            bool onPiece = piecePays(m_landed);

            TextStyle title;
            title.size = theme::textXl;
            title.weight = FontWeight::Bold;
            title.color = m_piece ? theme::accent : theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingSnug;
            std::string headline;
            if (m_piece)
                headline = format("A piece of %s", m_pieceName.c_str());
            else if (m_staked)
                headline.clear(); // the coin and the number, drawn below
            else
                headline = onPiece ? std::string("The piece lantern")
                                   : format("The %u lantern",
                                         unsigned(kPrize[size_t(m_landed)]));
            if (headline.empty())
                ui::coinAmount(r, inner.x, y, m_paid, title);
            else
                r.text(inner.x, y, r.ellipsize(headline, title, inner.w), title);
            y += title.size * theme::leadingSnug + 6.0f;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            std::string line;
            if (m_piece) {
                line = format("Piece %d, and the panel will say the wheel brought "
                              "it. %u coins left.",
                    m_pieceIndex + 1, unsigned(Wallet::get().balance()));
            } else if (m_staked) {
                line = format("%u coins to spend. A spin costs %u.",
                    unsigned(Wallet::get().balance()), unsigned(kStake));
            } else {
                // The honest version of a free spin: it says what it landed on
                // and that nothing was on it.
                line = onPiece
                    ? "Nothing was on it - that one only pays when you have staked."
                    : format("Nothing staked, so nothing won. It would have paid %u.",
                          unsigned(kPrize[size_t(m_landed)]));
            }
            y += r.textWrapped(Rect { inner.x, y, inner.w, 90.0f }, line, body, 2);

            drawButtons(app, r,
                Rect { inner.x, std::max(inner.bottom() - kButton, y + theme::s4),
                    inner.w, kButton },
                "Spin again", Wallet::get().balance() >= kStake);
            drawBack(app, r, box);
        }

        // Watch it, or put coins on it. The cursor starts on the free one
        // every spin and there is no shortcut into the stake, the way the race
        // and the dice do it.
        void drawButtons(App& app, Renderer& r, const Rect& row, const char* plainLabel,
            bool canStake)
        {
            std::string staked = format("Bet %u coins", unsigned(kStake));
            Rect plain { row.x, row.y, ui::actionButtonWidth(r, plainLabel), row.h };
            Rect stake { plain.right() + theme::s4, row.y,
                ui::actionButtonWidth(r, staked), row.h };

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

        int m_phase = Phase_Ready;
        float m_clock = 0.0f;
        float m_pulse = 0.0f;
        float m_angle = 0.0f; // where the needle rests
        float m_from = 0.0f;  // where this spin started
        float m_sweep = 0.0f; // how far it turns
        int m_landed = -1;
        bool m_staked = false;
        int m_button = 0; // 0 = watch it, 1 = ten coins on it
        uint32_t m_paid = 0;
        bool m_piece = false;
        std::string m_pieceName;
        int m_pieceIndex = 0;
    };
}

std::unique_ptr<Scene> makeLanternWheelScene()
{
    return std::make_unique<LanternWheelScene>();
}

} // namespace nxp
