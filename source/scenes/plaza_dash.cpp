#include "app.h"
#include "core/store.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/plaza_scroll.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nxp {

namespace {

    float randomUnit()
    {
        uint32_t bits = 0;
        randomBytes(&bits, sizeof(bits));
        return float(bits) / float(0xFFFFFFFFu);
    }

    float randomRange(float lo, float hi) { return lo + (hi - lo) * randomUnit(); }

    // Your own Mii, running through the plaza, jumping what the market leaves
    // in the way.
    class PlazaDashScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Start = Touch_SceneBase,
            Zone_Back,
        };

        bool coversChrome() const override { return true; }

        void onEnter(App& app) override
        {
            m_best = app.store().bestScore(kGameKey);
            m_mii = app.store().myPass().face();
            m_phase = Phase_Ready;
            reset();
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            // A frame that took a quarter of a second - an SD write, a wifi
            // hiccup - must not teleport the runner through a crate.
            dt = std::min(dt, 1.0f / 30.0f);

            TouchTarget tap;
            bool tapped = app.takeTap(tap);

            if (m_phase != Phase_Run) {
                if (tapped) {
                    if (tap.is(Zone_Back)) {
                        app.popOverlay();
                        return;
                    }
                    if (tap.is(Zone_Start))
                        begin();
                    return;
                }
                if (input.back()) {
                    app.popOverlay();
                    return;
                }
                if (input.accept())
                    begin();
                return;
            }

            // ------------------------------------------------------- running

            // A tap anywhere jumps, since there is nothing else to touch.
            if (input.accept() || tapped)
                jump();
            // B gives up the run and keeps the distance: there is no stake, so
            // there is nothing to protect by trapping somebody in a game.
            if (input.back()) {
                land(app);
                return;
            }

            m_speed = std::min(m_speed + kRamp * dt, kTopSpeed);
            m_travelled += m_speed * dt;

            // How long a jump is, decided by how long the button is down.
            //
            // The climb is cut once, when A comes up while there is still
            // upward velocity left - and never before kMinThrust, so a jump
            // cannot be cancelled by a release in the same frame as the press.
            // Cheaper and steadier than two gravities, and it cannot be
            // applied twice because of the flag.
            if (!m_grounded) {
                m_air += dt;
                // A finger counts as the button: a jump started by a tap rises
                // while the finger is down, so handheld gets the same long and
                // short jumps rather than only the short one.
                bool held = input.holding(HidNpadButton_A) || input.touch.down;
                if (!m_cutDone && !held && m_velocity > 0.0f
                    && m_air >= kMinThrust) {
                    m_velocity *= kCut;
                    m_cutDone = true;
                }
            }

            // Up is positive, for both of these: the jump sets a positive
            // velocity, gravity takes it away, and the height follows it. They
            // were the other way round, which cancelled the jump inside a
            // single frame - height went negative on the first step and the
            // clamp below put the runner straight back on the ground.
            m_velocity -= kGravity * dt;
            m_height += m_velocity * dt;
            if (m_height <= 0.0f) {
                m_height = 0.0f;
                m_velocity = 0.0f;
                m_grounded = true;
            }

            for (Hazard& hazard : m_hazards)
                hazard.x -= m_speed * dt;
            while (!m_hazards.empty() && m_hazards.front().x + m_hazards.front().w < -80.0f)
                m_hazards.erase(m_hazards.begin());

            m_nextSpawn -= dt;
            if (m_nextSpawn <= 0.0f)
                spawn();

            if (hit())
                land(app);
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);

            // With the scenery held still, the camera handed to the backdrop
            // simply stops advancing: the hills, the posts and the floor marks
            // freeze where they are and the hazards and the runner carry on
            // exactly as before. Nothing about the pace or the distance
            // changes, so a score is a score either way.
            float scenery = app.store().settings().reduceMotion ? 0.0f : m_travelled;
            // Sparser and dimmer than the race's lamps, because this runs at
            // three times the pace: 560px is 1.5 Hz at full speed where 340
            // was 2.4, and the bloom is half as strong.
            ui::plazaBackdrop(r, scenery, kHorizon, 560.0f, 0.16f);
            ui::plazaGround(r, scenery, kHorizon);

            for (const Hazard& hazard : m_hazards)
                drawHazard(r, hazard);
            drawRunner(r);

            switch (m_phase) {
            case Phase_Ready:
                drawReady(app, r);
                break;
            case Phase_Run:
                drawMeter(app, r);
                break;
            case Phase_Over:
                drawOver(app, r);
                break;
            }
        }

    private:
        enum Phase : int {
            Phase_Ready = 0,
            Phase_Run,
            Phase_Over,
        };

        struct Hazard {
            float x = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            bool crate = true; // a crate to clear, or a puddle to hurdle
        };

        static constexpr const char* kGameKey = "dash";
        static constexpr float kHorizon = 470.0f;
        static constexpr float kGroundY = 830.0f; // where the feet stand
        static constexpr float kRunnerX = 380.0f;
        static constexpr float kFigure = 150.0f;
        // 1150 up against 2600 down is a 254px arch lasting 0.88s - enough to
        // clear the tallest crate with room, and short enough that landing is
        // part of the rhythm rather than a wait.
        static constexpr float kGravity = 2600.0f;
        static constexpr float kJump = 1150.0f;
        // Let go on the way up and most of what is left of the climb goes with
        // the button: a tap tops out at 150px and holding it gets the full
        // 245px. Every jump used to be the same one, which is the difference
        // between a game and a metronome.
        static constexpr float kCut = 0.45f;
        // The thrust nobody can cut short. Without it, a tap so brief that the
        // release lands inside the same frame as the press would be no jump at
        // all, and the shortest jump has to clear a low crate.
        static constexpr float kMinThrust = 0.12f;
        static constexpr float kStartSpeed = 620.0f;
        static constexpr float kRamp = 17.0f; // pixels per second, per second
        static constexpr float kTopSpeed = 1500.0f;
        // Never below the airtime, so there is always a landing between two
        // hazards and no pair can be unclearable however fast it gets.
        static constexpr float kMinGap = 1.0f;
        static constexpr float kMaxGap = 1.9f;
        // Sixty pixels to the metre, so the opening pace reads as about ten
        // metres a second.
        static constexpr float kPixelsPerMetre = 60.0f;

        void reset()
        {
            m_travelled = 0.0f;
            m_speed = kStartSpeed;
            m_height = 0.0f;
            m_velocity = 0.0f;
            m_grounded = true;
            m_air = 0.0f;
            m_cutDone = false;
            m_hazards.clear();
            m_nextSpawn = 1.4f;
            m_beatBest = false;
            m_recorded = false;
        }

        void begin()
        {
            reset();
            m_phase = Phase_Run;
        }

        void jump()
        {
            if (!m_grounded)
                return;
            m_grounded = false;
            m_velocity = kJump;
            m_air = 0.0f;
            m_cutDone = false;
        }

        // One write per run, in the frame the run ends: drawing should not be
        // the thing that changes what is stored.
        void land(App& app)
        {
            m_phase = Phase_Over;
            if (!m_recorded) {
                m_beatBest = app.store().noteBestScore(kGameKey, metres());
                if (m_beatBest)
                    m_best = metres();
                m_recorded = true;
            }
        }

        void spawn()
        {
            Hazard hazard;
            hazard.crate = randomUnit() > 0.35f;
            if (hazard.crate) {
                // One crate or two stacked. Both clear on the same jump; the
                // tall one only looks worse, which is the point of it.
                bool tall = randomUnit() > 0.6f;
                hazard.w = randomRange(70.0f, 110.0f);
                hazard.h = tall ? randomRange(150.0f, 190.0f) : randomRange(80.0f, 120.0f);
            } else {
                // A puddle: low but wide, so it is cleared by jumping early
                // rather than by jumping high.
                hazard.w = randomRange(200.0f, 320.0f);
                hazard.h = 26.0f;
            }
            hazard.x = Renderer::DesignWidth + 120.0f;
            m_hazards.push_back(hazard);
            m_nextSpawn = randomRange(kMinGap, kMaxGap);
        }

        // The runner's feet box against each hazard. Narrow on purpose - a
        // shoulder that clips a crate is not what anybody thinks they hit.
        Rect body() const
        {
            float width = kFigure * 0.34f;
            float height = kFigure * 0.62f;
            return Rect { kRunnerX - width * 0.5f, kGroundY - m_height - height, width,
                height };
        }

        bool hit() const
        {
            Rect me = body();
            for (const Hazard& hazard : m_hazards) {
                Rect box { hazard.x, kGroundY - hazard.h, hazard.w, hazard.h };
                if (me.right() > box.x && me.x < box.right() && me.bottom() > box.y)
                    return true;
            }
            return false;
        }

        uint32_t metres() const
        {
            return uint32_t(m_travelled / kPixelsPerMetre);
        }

        // ---------------------------------------------------------- painting

        void drawHazard(Renderer& r, const Hazard& hazard) const
        {
            Rect box { hazard.x, kGroundY - hazard.h, hazard.w, hazard.h };
            if (box.right() < -20.0f || box.x > Renderer::DesignWidth + 20.0f)
                return;

            r.ellipse(box.centerX(), kGroundY + 6.0f, box.w * 0.55f, 10.0f,
                theme::bg0.scaleAlpha(0.30f), 0.0f);

            if (!hazard.crate) {
                r.roundRect(box, 13.0f, theme::teal.scaleAlpha(0.55f));
                return;
            }

            r.roundRect(box, 6.0f, theme::bg3);
            r.strokeRect(box, 6.0f, theme::stroke, theme::fg4);
            // A slat across the middle, so a crate reads as a crate rather than
            // as a grey rectangle.
            r.rect(Rect { box.x + 6.0f, box.centerY() - 3.0f, box.w - 12.0f, 6.0f },
                theme::fg4.scaleAlpha(0.55f));
        }

        void drawRunner(Renderer& r) const
        {
            // No bounce. The ground is already moving under the runner and the
            // jump is the only other vertical thing here, so a stride bob just
            // made the figure look unsteady - and it was drawing the runner
            // somewhere the collision box was not.
            float feet = kGroundY - m_height;

            float shadow = 44.0f - std::min(m_height, 240.0f) * 0.10f;
            r.ellipse(kRunnerX, kGroundY + 5.0f, std::max(shadow, 18.0f),
                std::max(11.0f - m_height * 0.02f, 4.0f),
                theme::bg0.scaleAlpha(0.32f), 0.0f);

            Rect box { kRunnerX - kFigure * 0.42f, feet - kFigure, kFigure * 0.84f,
                kFigure };
            ui::miiFigure(r, box, m_mii, 1.0f);
        }

        void drawMeter(App& app, Renderer& r)
        {
            app.hint("A", "jump - hold to go higher");
            app.hint("B", "give up");

            TextStyle style;
            style.size = theme::text3xl;
            style.weight = FontWeight::Bold;
            style.color = theme::fg1;
            style.tracking = theme::trackingTight;
            style.leading = theme::leadingTight;
            r.text(Rect { 0.0f, 54.0f, Renderer::DesignWidth - theme::edge, 100.0f },
                format("%u m", unsigned(metres())), style, Align::Right, VAlign::Top);

            if (m_best > 0) {
                TextStyle best;
                best.size = theme::textSm;
                best.color = metres() > m_best ? theme::accent : theme::fg3;
                best.tracking = theme::trackingWide;
                r.text(Rect { 0.0f, 54.0f + theme::text3xl * theme::leadingTight,
                          Renderer::DesignWidth - theme::edge, 30.0f },
                    metres() > m_best ? "a new best" : format("best %u m", unsigned(m_best)),
                    best, Align::Right, VAlign::Top);
            }
        }

        Rect plate(float width, float height) const
        {
            return Rect { Renderer::DesignWidth * 0.5f - width * 0.5f, 150.0f, width,
                height };
        }

        void drawReady(App& app, Renderer& r)
        {
            app.hint("A", "run");
            app.hint("B", "back");

            // 42 of eyebrow, 67 of title, two 36px lines and a 76px button, with
            // a gap before it: 282 of interior, which 360 provides.
            Rect box = plate(940.0f, 360.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.94f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s6);
            float y = inner.y;

            ui::eyebrow(r, Rect { inner.x, y, inner.w, 30.0f }, "plaza dash");
            y += 30.0f + theme::s3;

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            r.text(inner.x, y, "Jump the market", title);

            if (m_best > 0) {
                TextStyle best;
                best.size = theme::textSm;
                best.color = theme::fg3;
                best.tracking = theme::trackingWide;
                r.text(Rect { inner.x, y, inner.w, title.size * theme::leadingTight },
                    format("best %u m", unsigned(m_best)), best, Align::Right,
                    VAlign::Middle);
            }
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            // Two lines' worth of words, not two and a fifth: the longer
            // version ended in an ellipsis where its own last clause should
            // have been. What is not at stake is on the shelf row already.
            y += r.textWrapped(Rect { inner.x, y, inner.w, 90.0f },
                "A to jump, and hold it to jump higher. It gets quicker until you "
                "hit something.",
                body, 2);

            // Below the text or on the floor of the plate, whichever is lower.
            // Anchored to the floor alone, a line of text that grows runs
            // underneath the button instead of pushing it down.
            Rect go { inner.x, std::max(inner.bottom() - 76.0f, y + theme::s5),
                ui::actionButtonWidth(r, "Run"), 76.0f };
            app.touchZone(go, Zone_Start);
            ui::actionButton(r, go, "Run", true,
                app.touchHeld(Zone_Start) ? 1.0f : 0.7f + 0.3f * m_pulse);
            drawBack(app, r, box);
        }

        void drawOver(App& app, Renderer& r)
        {
            app.hint("A", "run again");
            app.hint("B", "back");

            Rect box = plate(940.0f, 330.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.96f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s6);
            float y = inner.y;

            TextStyle title;
            title.size = theme::text3xl;
            title.weight = FontWeight::Bold;
            title.color = m_beatBest ? theme::accent : theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            r.text(inner.x, y, format("%u m", unsigned(metres())), title);
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            r.text(inner.x, y,
                m_beatBest ? "A new best."
                           : format("Best is %u m.", unsigned(m_best)),
                body);
            y += body.size * theme::leadingNormal;

            Rect go { inner.x, std::max(inner.bottom() - 76.0f, y + theme::s5),
                ui::actionButtonWidth(r, "Run again"), 76.0f };
            app.touchZone(go, Zone_Start);
            ui::actionButton(r, go, "Run again", true,
                app.touchHeld(Zone_Start) ? 1.0f : 0.7f + 0.3f * m_pulse);
            drawBack(app, r, box);
        }

        void drawBack(App& app, Renderer& r, const Rect& box)
        {
            Rect back { box.right() - 60.0f, box.y + 18.0f, 42.0f, 42.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);
        }

        Mii m_mii;
        int m_phase = Phase_Ready;
        float m_pulse = 0.0f;

        float m_travelled = 0.0f; // world pixels, and the camera
        float m_speed = kStartSpeed;
        float m_height = 0.0f; // above the ground
        float m_velocity = 0.0f;
        bool m_grounded = true;
        float m_air = 0.0f; // seconds since takeoff
        bool m_cutDone = false;

        std::vector<Hazard> m_hazards;
        float m_nextSpawn = 1.4f;

        uint32_t m_best = 0;
        bool m_beatBest = false;
        bool m_recorded = false;
    };
}

std::unique_ptr<Scene> makePlazaDashScene()
{
    return std::make_unique<PlazaDashScene>();
}

} // namespace nxp
