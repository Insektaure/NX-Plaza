#include "app.h"
#include "core/store.h"
#include "core/util.h"
#include "core/wallet.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/plaza_scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace nxp {

namespace {

    uint32_t randomBelow(uint32_t n)
    {
        uint32_t bits = 0;
        randomBytes(&bits, sizeof(bits));
        return n == 0 ? 0 : bits % n;
    }

    // Dice are white with dark pips in both palettes, the way bronze is bronze
    // in both: a die that changed colour with the theme would stop reading as
    // a die.
    const Color kIvory = Color::hex(0xF3EEE5);
    const Color kPip = Color::hex(0x2A2724);
    const Color kEdge = Color::hex(0xD6CEC1);

    // One die each, highest roll takes it.
    //
    // The roll is the game. Fifteen of the thirty-six pairs win, fifteen lose
    // and six draw; a draw hands the stake back, and what pays for that is the
    // price rather than the rule - see kStake.
    //
    // Nothing about the animation decides anything. Both faces are drawn from
    // randomBytes the moment the roll starts and the tumble is theatre played
    // over the top of them, so a slow frame cannot change who won.
    class DiceDuelScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Roll = Touch_SceneBase,
            Zone_Stake,
            Zone_Back,
        };

        bool coversChrome() const override { return true; }

        // A roll is 1.8 seconds and a coin may already be on it, so + waits -
        // the same bargain the race makes, and just as short.
        bool blocksExit() const override { return m_phase == Phase_Roll; }

        void onEnter(App& app) override
        {
            m_mine = app.store().myPass();
            pickOpponent(app);
            m_phase = Phase_Ready;
            m_clock = 0.0f;
            m_button = 0;
            m_staked = false;
            m_paid = 0;
            // Something to look at before the first roll. Replaced the moment
            // one starts, so these are decoration and never a result.
            m_myFace = 1 + int(randomBelow(6));
            m_theirFace = 1 + int(randomBelow(6));
            m_streak = app.store().bestScore("dice_streak");
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_clock += dt;
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

            TouchTarget tap;
            bool tapped = app.takeTap(tap);

            if (m_phase == Phase_Roll) {
                // Both dice down, plus a moment to look at them.
                if (m_clock >= kSpin + kStagger + kHold)
                    settle(app);
                return;
            }

            if (Wallet::get().balance() < kStake)
                m_button = 0;

            if (tapped) {
                if (tap.is(Zone_Back)) {
                    app.popOverlay();
                    return;
                }
                if (tap.is(Zone_Roll)) {
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
            // A still plaza: nothing here is moving past, so the backdrop has
            // no camera to follow.
            ui::plazaBackdrop(r, 0.0f, kHorizon);
            ui::plazaGround(r, 0.0f, kHorizon);

            drawPlayer(r, kMyX, m_mine.face(), true);
            drawPlayer(r, kTheirX, m_theirs.face(), false);

            drawDie(r, kMyDieX, 0.0f, m_myFace, true);
            drawDie(r, kTheirDieX, kStagger, m_theirFace, false);

            switch (m_phase) {
            case Phase_Ready:
                drawReady(app, r);
                break;
            case Phase_Roll:
                drawCalling(app, r);
                break;
            case Phase_Done:
                drawResult(app, r);
                break;
            }
        }

    private:
        enum Phase : int {
            Phase_Ready = 0,
            Phase_Roll,
            Phase_Done,
        };

        static constexpr float kHorizon = 430.0f;
        static constexpr float kFloor = 700.0f; // where a die comes to rest
        static constexpr float kDie = 150.0f;
        static constexpr float kMyX = 380.0f;
        static constexpr float kTheirX = 1540.0f;
        static constexpr float kMyDieX = 700.0f;
        static constexpr float kTheirDieX = 1220.0f;

        // The tumble: a decaying bounce, a face change every 55ms, and the
        // second die a fifth of a second behind the first so the two land one
        // after the other instead of together.
        static constexpr float kSpin = 0.95f;
        static constexpr float kStagger = 0.18f;
        static constexpr float kHold = 0.65f;
        static constexpr float kFlyIn = 0.32f;
        static constexpr float kBounce = 170.0f;
        static constexpr float kDecay = 3.2f;
        static constexpr float kPeriod = 0.26f;
        static constexpr float kFlicker = 0.055f;

        // Two staked for a three coin prize, and a draw hands the two back.
        //
        // The tie rule and the price are one decision. Any symmetric duel that
        // refunds a draw and pays double the stake is exactly even money -
        // 2(1-d)/2 + d - 1 = 0 for whatever the chance of a draw is - so a
        // refunded draw cannot be paid for by changing the dice, only by the
        // price. Charging two for a three coin win leaves the plaza a fifth of
        // the stake and lets a draw be what everybody expects a draw to be.
        static constexpr uint32_t kStake = 2;
        static constexpr uint32_t kPayout = 3;
        static constexpr uint32_t kRefund = 2;
        static constexpr float kButton = 76.0f;

        void pickOpponent(App& app)
        {
            std::vector<Crossing> crossings = app.store().crossings();
            if (!crossings.empty()) {
                const Crossing& c = crossings[randomBelow(uint32_t(crossings.size()))];
                m_theirs = c.pass;
                if (m_theirs.handle.empty())
                    m_theirs.handle = "A stranger";
                return;
            }
            // Nobody crossed yet: a seeded face, the way the race fills a
            // field on a console that has met no-one.
            Pass stranger;
            uint32_t seed = 0;
            randomBytes(&seed, sizeof(seed));
            stranger.portrait = seed;
            stranger.handle = "A stranger";
            m_theirs = stranger;
        }

        void begin(App& app, bool staked)
        {
            Wallet& wallet = Wallet::get();
            if (staked) {
                if (!wallet.spend(kStake)) {
                    app.toast(format("Two coins to play"),
                        "Ten arrive on each new day you open the app.");
                    return;
                }
                // On the card before the dice move, so walking out on a bad
                // roll still costs the coin.
                wallet.flush();
            }
            m_staked = staked;
            m_paid = 0;
            m_button = 0;

            // Decided here, once. Everything after this is drawing.
            m_myFace = 1 + int(randomBelow(6));
            m_theirFace = 1 + int(randomBelow(6));
            m_phase = Phase_Roll;
            m_clock = 0.0f;
        }

        bool won() const { return m_myFace > m_theirFace; }
        bool drawn() const { return m_myFace == m_theirFace; }

        void settle(App& app)
        {
            m_phase = Phase_Done;
            record(app);

            if (!m_staked)
                return;
            // A win pays three, a draw gives the two back, a loss keeps them.
            uint32_t give = won() ? kPayout : (drawn() ? kRefund : 0u);
            if (give == 0)
                return;
            Wallet& wallet = Wallet::get();
            wallet.award(give);
            wallet.flush();
            m_paid = give;
        }

        // What a duel leaves behind, which is the only reason it can have
        // trophies of its own: nothing else about a roll is recoverable
        // afterwards. Three numbers in the same score table the dash uses -
        // duels won, the longest run of them, and whether two sixes have ever
        // turned up together. The live streak is kept rather than held in this
        // scene, so a run survives walking out of the games tab.
        void record(App& app)
        {
            Store& store = app.store();
            if (m_myFace == 6 && m_theirFace == 6)
                store.setScore("dice_six_all", 1);

            if (!won()) {
                store.setScore("dice_streak", 0);
                return;
            }
            store.setScore("dice_wins", store.bestScore("dice_wins") + 1);
            uint32_t streak = store.bestScore("dice_streak") + 1;
            store.setScore("dice_streak", streak);
            store.noteBestScore("dice_streak_best", streak);
            m_streak = streak;
        }

        // ------------------------------------------------------- the tumble

        // Height above the floor, as a decaying bounce that reaches zero at
        // kSpin. exp decay against a |sin| gives a first hop of 112px and then
        // 49, 21, 9 - a die losing its energy without a table of timings.
        float height(float t) const
        {
            if (m_phase != Phase_Roll)
                return 0.0f;
            if (t <= 0.0f || t >= kSpin)
                return 0.0f;
            float envelope = std::exp(-kDecay * t);
            return kBounce * envelope
                * std::abs(std::sin(t * 3.14159265f / kPeriod));
        }

        int faceAt(float t, int settled) const
        {
            if (m_phase == Phase_Ready)
                return settled;
            if (m_phase != Phase_Roll || t >= kSpin)
                return settled;
            if (t <= 0.0f)
                return settled;
            // A hash of which flicker slot this is, so the face holds for the
            // whole 55ms instead of changing every frame.
            uint32_t slot = uint32_t(t / kFlicker);
            return 1 + int((slot * 2654435761u >> 13) % 6u);
        }

        void drawPlayer(Renderer& r, float x, const Mii& mii, bool mine) const
        {
            constexpr float kFigure = 210.0f;
            Rect box { x - kFigure * 0.42f, kFloor - kFigure, kFigure * 0.84f, kFigure };
            if (mine && m_phase == Phase_Done && won()) {
                Color rim = theme::accent.scaleAlpha(0.5f);
                ui::miiSilhouette(r, box.inset(-6.0f), mii, rim);
            }
            ui::miiFigure(r, box, mii, 1.0f);

            TextStyle name;
            name.size = theme::textSm;
            name.weight = FontWeight::Bold;
            name.color = mine ? theme::accent : theme::fg2;
            std::string label = mine
                ? (m_mine.handle.empty() ? std::string("You") : m_mine.handle)
                : m_theirs.handle;
            r.text(Rect { x - 150.0f, box.y - 34.0f, 300.0f, 28.0f },
                r.ellipsize(label, name, 300.0f), name, Align::Center, VAlign::Top);
        }

        void drawDie(Renderer& r, float restX, float offset, int settled, bool mine) const
        {
            float t = m_clock - offset;
            float h = height(t);
            int face = faceAt(t, settled);

            // Flies in from off to the right and eases into place, so the dice
            // arrive rather than appearing.
            float x = restX;
            if (m_phase == Phase_Roll && t < kFlyIn) {
                float u = std::max(t, 0.0f) / kFlyIn;
                float ease = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
                x = restX + 520.0f * (1.0f - ease);
            }

            // Squashed flat on contact, stretched at the top of a hop: the
            // whole reason a bouncing rectangle reads as a solid object.
            float contact = m_phase == Phase_Roll && t > 0.0f && t < kSpin
                ? std::max(0.0f, 1.0f - h / 34.0f)
                : 0.0f;
            float sx = 1.0f + 0.20f * contact;
            float sy = 1.0f - 0.20f * contact;

            float w = kDie * sx;
            float tall = kDie * sy;
            Rect box { x - w * 0.5f, kFloor - h - tall, w, tall };

            // The shadow tightens as the die comes down, which is what says
            // "above the floor" without a horizon line to read against.
            float spread = 1.0f - std::min(h, 200.0f) / 260.0f;
            r.ellipse(x, kFloor + 8.0f, kDie * 0.42f * spread + 14.0f,
                12.0f * spread + 3.0f, theme::bg0.scaleAlpha(0.30f), 0.0f);

            bool winner = m_phase == Phase_Done && !drawn() && (mine == won());
            if (winner) {
                r.glow(Rect { box.centerX() - 130.0f, box.centerY() - 130.0f, 260.0f,
                           260.0f },
                    theme::accentGlow.scaleAlpha(0.30f), 1.9f);
            }

            r.roundRect(box, kDie * 0.18f, kIvory);
            r.strokeRect(box, kDie * 0.18f, 3.0f, kEdge);
            drawPips(r, box, face);
        }

        static void drawPips(Renderer& r, const Rect& box, int face)
        {
            // Unit positions, so a squashed die keeps its pips inside itself.
            static const float kSpots[6][12] = {
                { 0.0f, 0.0f },
                { -0.46f, -0.46f, 0.46f, 0.46f },
                { -0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.5f },
                { -0.46f, -0.46f, 0.46f, -0.46f, -0.46f, 0.46f, 0.46f, 0.46f },
                { -0.48f, -0.48f, 0.48f, -0.48f, 0.0f, 0.0f, -0.48f, 0.48f, 0.48f,
                    0.48f },
                { -0.46f, -0.55f, 0.46f, -0.55f, -0.46f, 0.0f, 0.46f, 0.0f, -0.46f,
                    0.55f, 0.46f, 0.55f },
            };
            static const int kCount[6] = { 1, 2, 3, 4, 5, 6 };

            int index = std::min(std::max(face, 1), 6) - 1;
            float rx = box.w * 0.5f;
            float ry = box.h * 0.5f;
            float radius = std::min(box.w, box.h) * 0.115f;
            for (int i = 0; i < kCount[index]; i++) {
                float px = box.centerX() + kSpots[index][i * 2] * rx * 0.72f;
                float py = box.centerY() + kSpots[index][i * 2 + 1] * ry * 0.72f;
                r.ellipse(px, py, radius, radius * (box.h / box.w), kPip, 0.0f);
            }
        }

        // ---------------------------------------------------------- painting

        Rect plate(float width, float height) const
        {
            return Rect { Renderer::DesignWidth * 0.5f - width * 0.5f, 96.0f, width,
                height };
        }

        void drawReady(App& app, Renderer& r)
        {
            uint32_t coins = Wallet::get().balance();
            app.hint("A", "roll");
            app.hint("B", "back");

            Rect box = plate(1100.0f, 300.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.94f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s6);
            float y = inner.y;

            ui::eyebrow(r, Rect { inner.x, y, inner.w, 30.0f }, "the dice duel");
            y += 30.0f + theme::s3;

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            r.text(inner.x, y, "One roll each", title);

            TextStyle purse;
            purse.size = theme::textSm;
            purse.color = theme::fg3;
            r.text(Rect { inner.x, y, inner.w, title.size * theme::leadingTight },
                format("%u %s", unsigned(coins), coins == 1 ? "coin" : "coins"), purse,
                Align::Right, VAlign::Middle);
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            y += r.textWrapped(Rect { inner.x, y, inner.w, 60.0f },
                format("Highest roll takes it. A draw hands your %u back.",
                    unsigned(kStake)),
                body, 1);

            drawButtons(app, r,
                Rect { inner.x, std::max(inner.bottom() - kButton, y + theme::s4),
                    inner.w, kButton },
                "Roll for free", coins >= kStake);
            drawBack(app, r, box);
        }

        void drawCalling(App& app, Renderer& r)
        {
            app.hint("B", "-");
            (void)r;
        }

        void drawResult(App& app, Renderer& r)
        {
            app.hint("A", "roll again");
            app.hint("B", "back");

            Rect box = plate(1100.0f, 290.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.96f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s6);
            float y = inner.y;

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = won() ? theme::accent : theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            std::string headline = drawn()
                ? format("%d all - nobody takes it", m_myFace)
                : (won() ? format("%d beats %d", m_myFace, m_theirFace)
                         : format("%d loses to %d", m_myFace, m_theirFace));
            r.text(inner.x, y, headline, title);
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            std::string line;
            if (m_staked) {
                uint32_t left = Wallet::get().balance();
                if (won())
                    line = format("The bet paid %u - %u coins to spend.",
                        unsigned(m_paid), unsigned(left));
                else if (drawn())
                    line = format("Your %u came back. %u to spend.",
                        unsigned(kRefund), unsigned(left));
                else
                    line = format("The bet cost you %u. %u left.", unsigned(kStake),
                        unsigned(left));
            } else {
                line = drawn() ? "Nothing bet, and nothing decided."
                               : (won() ? "Nothing bet, but a win is a win."
                                        : "Nothing bet, nothing lost.");
            }
            r.text(inner.x, y, line, body);
            y += body.size * theme::leadingNormal;

            if (won() && m_streak >= 2) {
                TextStyle run;
                run.size = theme::textSm;
                run.weight = FontWeight::Bold;
                run.color = theme::accent;
                run.tracking = theme::trackingWide;
                r.text(Rect { inner.x, inner.y, inner.w,
                          title.size * theme::leadingTight },
                    format("%u in a row", unsigned(m_streak)), run, Align::Right,
                    VAlign::Middle);
            }

            drawButtons(app, r,
                Rect { inner.x, std::max(inner.bottom() - kButton, y + theme::s4),
                    inner.w, kButton },
                "Roll again", Wallet::get().balance() >= kStake);
            drawBack(app, r, box);
        }

        void drawButtons(App& app, Renderer& r, const Rect& row, const char* plainLabel,
            bool canStake)
        {
            std::string staked = format("Bet %u coins - %u back", unsigned(kStake),
                unsigned(kPayout));
            Rect plain { row.x, row.y, ui::actionButtonWidth(r, plainLabel), row.h };
            Rect stake { plain.right() + theme::s4, row.y,
                ui::actionButtonWidth(r, staked), row.h };

            app.touchZone(plain, Zone_Roll);
            if (canStake)
                app.touchZone(stake, Zone_Stake);

            float pulse = 0.7f + 0.3f * m_pulse;
            bool onStake = m_button == 1 && canStake;
            ui::actionButton(r, plain, plainLabel, !onStake,
                app.touchHeld(Zone_Roll) ? 1.0f : (onStake ? 0.0f : pulse));
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

        Pass m_mine;
        Pass m_theirs;
        int m_phase = Phase_Ready;
        float m_clock = 0.0f;
        float m_pulse = 0.0f;
        int m_button = 0;
        bool m_staked = false;
        uint32_t m_paid = 0;
        int m_myFace = 1;
        int m_theirFace = 1;
        uint32_t m_streak = 0; // this run of wins, for the result plate to say
    };
}

std::unique_ptr<Scene> makeDiceDuelScene()
{
    return std::make_unique<DiceDuelScene>();
}

} // namespace nxp
