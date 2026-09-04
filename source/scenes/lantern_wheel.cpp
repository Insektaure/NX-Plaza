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

    uint32_t randomBelow(uint32_t n)
    {
        uint32_t bits = 0;
        randomBytes(&bits, sizeof(bits));
        return n == 0 ? 0 : bits % n;
    }

    // Twelve lanterns and a needle.
    //
    // Every lantern pays something. Five give two coins back, four give five,
    // two give eight and one gives a puzzle piece, so a spin is a question of
    // how much rather than whether - which is the only version of this worth
    // having. Against a ten coin stake that is eight coins of expected value:
    //
    //     (5*2 + 4*5 + 2*8 + 50) / 12 = 8.0
    //
    // taking the piece at the fifty the shop charges for one. A fifth of the
    // stake to the plaza, and a piece off the wheel costs twelve spins on
    // average - a hundred and twenty coins against the shop's fifty. That is
    // deliberate: the wheel is the romantic way to get a piece and the shop is
    // the sensible one, so neither undercuts the other.
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
        // The board, in the order the lanterns are drawn. Every one pays.
        // A zero here means the piece, which is worth more than any of them.
        static constexpr uint32_t kPrize[kLanterns] = {
            2, 5, 2, 8, 2, 5, 0, 5, 2, 8, 2, 5,
        };
        static constexpr int kPieceLantern = 6;
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

        // What the wheel is mounted on, so it is standing at a stall rather
        // than hanging in the air.
        void drawStand(Renderer& r) const
        {
            // Down to a plinth in front of the ground line rather than onto it:
            // the wheel is nearer the camera than the tent is, so its feet
            // belong lower on the screen.
            constexpr float kFloor = 955.0f;
            float top = kCentreY + kRadius - 20.0f;
            r.rect(Rect { kCentreX - 11.0f, top, 22.0f, kFloor - top }, theme::bg2);
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

            // Watched for nothing: the needle still lands somewhere and the
            // plate still says where, but nothing is handed over. A free spin
            // that paid would make the wheel a coin printer - eight coins a
            // go, for nothing - which is the one thing it must not be.
            if (!m_staked)
                return;

            if (m_landed == kPieceLantern) {
                Store::PiecePurchase got = app.store().buyPiece(false, kWheelSource);
                if (got.set >= 0) {
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

                // What it pays, under the lantern. Readable before you spin,
                // which is the difference between a wheel and a slot machine.
                TextStyle label;
                label.size = theme::textSm;
                label.weight = FontWeight::Bold;
                label.color = under || won ? theme::accent : theme::fg3;
                std::string what = i == kPieceLantern
                    ? std::string("piece")
                    : format("%u", unsigned(kPrize[size_t(i)]));
                float lift = std::sin(a) < -0.3f ? -kLampR - 34.0f : kLampR + 12.0f;
                r.text(Rect { x - 70.0f, y + lift, 140.0f, 26.0f }, what, label,
                    Align::Center, VAlign::Top);
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

            bool onPiece = m_landed == kPieceLantern;

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
                headline = format("%u coins", unsigned(m_paid));
            else
                headline = onPiece ? std::string("The piece lantern")
                                   : format("The %u lantern",
                                         unsigned(kPrize[size_t(m_landed)]));
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
