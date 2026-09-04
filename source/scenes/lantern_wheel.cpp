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

            TouchTarget tap;
            if (app.takeTap(tap)) {
                if (tap.is(Zone_Back)) {
                    app.popOverlay();
                    return;
                }
                if (tap.is(Zone_Spin))
                    begin(app);
                return;
            }
            if (input.back()) {
                app.popOverlay();
                return;
            }
            if (input.accept())
                begin(app);
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);
            ui::plazaBackdrop(r, 0.0f, kHorizon);
            ui::plazaGround(r, 0.0f, kHorizon);

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

        static constexpr float kHorizon = 760.0f;
        static constexpr float kCentreX = 960.0f;
        static constexpr float kCentreY = 470.0f;
        static constexpr float kRadius = 260.0f;
        static constexpr float kLampR = 34.0f;

        // Two and a half seconds of slowing down, then a moment on the answer.
        static constexpr float kSpin = 2.5f;
        static constexpr float kHold = 0.55f;
        static constexpr float kTurns = 4.0f; // whole turns before it settles

        static float lanternAngle(int index)
        {
            // Lantern 0 at the top, going clockwise.
            return -kTau * 0.25f + kTau * float(index) / float(kLanterns);
        }

        void begin(App& app)
        {
            Wallet& wallet = Wallet::get();
            if (!wallet.spend(kStake)) {
                app.toast(format("%u coins for a spin", unsigned(kStake)),
                    "Ten arrive on each new day you open the app.");
                return;
            }
            // Written before the needle moves, so leaving mid-spin still costs
            // the stake.
            wallet.flush();

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
            return Rect { Renderer::DesignWidth * 0.5f - width * 0.5f,
                Renderer::DesignHeight - height - 130.0f, width, height };
        }

        void drawReady(App& app, Renderer& r)
        {
            uint32_t coins = Wallet::get().balance();
            app.hint("A", coins >= kStake ? "spin" : "not enough");
            app.hint("B", "back");

            Rect box = plate(1100.0f, 210.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.94f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s5);

            TextStyle title;
            title.size = theme::textXl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            r.text(inner.x, inner.y, "The lantern wheel", title);

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            r.text(inner.x, inner.y + title.size * theme::leadingSnug + 6.0f,
                format("%u coins a spin. Every lantern pays something, and one of "
                       "them is a puzzle piece. You have %u.",
                    unsigned(kStake), unsigned(coins)),
                body);

            Rect go { inner.right() - ui::actionButtonWidth(r, "Spin"),
                inner.bottom() - 76.0f, ui::actionButtonWidth(r, "Spin"), 76.0f };
            if (coins >= kStake)
                app.touchZone(go, Zone_Spin);
            ui::actionButton(r, go, "Spin", coins >= kStake,
                coins < kStake ? 0.0f
                               : (app.touchHeld(Zone_Spin) ? 1.0f
                                                           : 0.7f + 0.3f * m_pulse));
            drawBack(app, r, box);
        }

        void drawResult(App& app, Renderer& r)
        {
            app.hint("A", "again");
            app.hint("B", "back");

            Rect box = plate(1100.0f, 210.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.96f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s5);

            TextStyle title;
            title.size = theme::textXl;
            title.weight = FontWeight::Bold;
            title.color = m_piece ? theme::accent : theme::fg1;
            title.tracking = theme::trackingTight;
            std::string headline = m_piece
                ? format("A piece of %s", m_pieceName.c_str())
                : format("%u coins", unsigned(m_paid));
            r.text(inner.x, inner.y, headline, title);

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            std::string line = m_piece
                ? format("Piece %d, and it will say the wheel brought it.",
                      m_pieceIndex + 1)
                : format("%u coins to spend. A spin costs %u.",
                      unsigned(Wallet::get().balance()), unsigned(kStake));
            r.text(inner.x, inner.y + title.size * theme::leadingSnug + 6.0f, line,
                body);

            Rect go { inner.right() - ui::actionButtonWidth(r, "Again"),
                inner.bottom() - 76.0f, ui::actionButtonWidth(r, "Again"), 76.0f };
            bool can = Wallet::get().balance() >= kStake;
            if (can)
                app.touchZone(go, Zone_Spin);
            ui::actionButton(r, go, "Again", can,
                can ? (app.touchHeld(Zone_Spin) ? 1.0f : 0.7f + 0.3f * m_pulse)
                    : 0.0f);
            drawBack(app, r, box);
        }

        void drawBack(App& app, Renderer& r, const Rect& box)
        {
            Rect back { box.x + 18.0f, box.y + 18.0f, 42.0f, 42.0f };
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
