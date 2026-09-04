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
#include <string>
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

    uint32_t randomBelow(uint32_t n)
    {
        uint32_t bits = 0;
        randomBytes(&bits, sizeof(bits));
        return n == 0 ? 0 : bits % n;
    }

    // A tower built out of the people you have crossed.
    //
    // One Mii swings above the stack, A drops it, and where it lands is where
    // that floor stands. Two ways to end: miss the shoulders below and the
    // floor falls past, or drift so far from the base that the whole thing
    // goes over. The lean is not a hidden number - the tower is drawn from the
    // floors' own positions, so the shape on screen *is* the record of every
    // drop, and the drift you have to correct is the one you can see.
    //
    // The world only rises once the tower is taller than the room above it,
    // and then in one 108px step per floor. That matters for the setting that
    // holds the scenery still: this has no continuous motion to hold, because
    // every movement here is the direct consequence of a press.
    //
    // Skill, so it pays nothing. It keeps a best height and that is all.
    class MiiTowerScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Start = Touch_SceneBase,
            Zone_Back,
        };

        bool coversChrome() const override { return true; }

        void onEnter(App& app) override
        {
            m_cast = gather(app);
            m_best = app.store().bestScore(kGameKey);
            m_phase = Phase_Ready;
            reset();
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            dt = std::min(dt, 1.0f / 30.0f);
            m_clock += dt;

            TouchTarget tap;
            bool tapped = app.takeTap(tap);

            switch (m_phase) {
            case Phase_Ready:
            case Phase_Over:
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

            case Phase_Play:
                playing(app, input, dt, tapped);
                return;

            case Phase_Fall:
                falling(dt);
                if (m_clock > 1.4f)
                    finish(app);
                return;
            }
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);
            float horizon = kGroundY + lift() + m_slide;
            ui::plazaBackdrop(r, 0.0f, horizon);
            drawClouds(r);
            if (horizon < Renderer::DesignHeight)
                ui::plazaGround(r, 0.0f, horizon);

            drawTower(r);
            // Only while it is still in the air, or falling past on a miss:
            // once it lands it belongs to the tower, and drawing it here as
            // well left a motionless copy of the top floor through a topple.
            if (m_phase == Phase_Play || (m_phase == Phase_Fall && m_missed))
                drawCarried(r);

            switch (m_phase) {
            case Phase_Ready:
                drawReady(app, r);
                break;
            case Phase_Play:
                drawMeter(app, r);
                break;
            case Phase_Over:
                drawOver(app, r);
                break;
            default:
                break;
            }
        }

    private:
        enum Phase : int {
            Phase_Ready = 0,
            Phase_Play, // one swinging above the stack, or on its way down
            Phase_Fall, // it missed, or the tower went over
            Phase_Over,
        };

        struct Floor {
            Mii mii;
            std::string name;
            float x = 0.0f; // centre, in world pixels
            float vx = 0.0f;
            float vy = 0.0f;
            float drop = 0.0f; // how far it has fallen during a collapse
        };

        static constexpr const char* kGameKey = "tower";
        static constexpr float kGroundY = 900.0f; // feet of the ground floor
        static constexpr float kFloor = 108.0f;   // one storey
        static constexpr float kFigure = 126.0f;
        static constexpr float kTopFeet = 540.0f; // where the top settles
        static constexpr float kHeadroom = kGroundY - kTopFeet;
        static constexpr float kHangFeet = 200.0f;
        static constexpr float kCentre = 960.0f;
        // A shoulder is 53px wide, so 58 is a hair over half a figure: land
        // wider than that and there is nothing under you.
        static constexpr float kCatch = 58.0f;
        // Roughly three worst-case drops in the same direction. Ordinary
        // sloppiness is 20px a floor, so it is eight of those - enough room to
        // notice a drift and lean back the other way.
        static constexpr float kLeanMax = 168.0f;
        static constexpr float kDeadCentre = 6.0f;
        static constexpr float kGravity = 2600.0f;
        static constexpr float kCloudGap = 420.0f; // one band of sky to the next
        // Half the storey gone in 50ms, settled by 350: quick enough to keep
        // up with a fast stacker, soft enough not to jolt.
        static constexpr float kCameraRate = 14.0f;

        // How far the world has risen. Nothing until the tower outgrows the
        // room above it, then one storey per floor.
        float lift() const
        {
            float raised = float(int(m_floors.size()) - 1) * kFloor - kHeadroom;
            return std::max(0.0f, raised);
        }

        float screenFeet(int index) const
        {
            return kGroundY - float(index) * kFloor + lift() + m_slide;
        }

        std::vector<Floor> gather(App& app) const
        {
            std::vector<Floor> cast;
            std::vector<Crossing> crossings = app.store().crossings();
            for (const Crossing& c : crossings) {
                Floor f;
                f.mii = c.pass.face();
                f.name = c.pass.handle.empty() ? std::string("A stranger")
                                               : c.pass.handle;
                cast.push_back(std::move(f));
                if (cast.size() >= 40)
                    break;
            }
            // Your own Mii belongs in the tower too, and on a console that has
            // met nobody it is the only one there is - so strangers fill in
            // behind it, the way the race fills a field.
            Pass mine = app.store().myPass();
            Floor self;
            self.mii = mine.face();
            self.name = mine.handle.empty() ? std::string("You") : mine.handle;
            cast.push_back(std::move(self));
            while (cast.size() < 8) {
                Pass stranger;
                uint32_t seed = 0;
                randomBytes(&seed, sizeof(seed));
                stranger.portrait = seed;
                Floor f;
                f.mii = stranger.face();
                f.name = "A stranger";
                cast.push_back(std::move(f));
            }
            return cast;
        }

        Floor next() const
        {
            if (m_cast.empty())
                return Floor {};
            Floor f = m_cast[randomBelow(uint32_t(m_cast.size()))];
            f.x = kCentre;
            return f;
        }

        void reset()
        {
            m_floors.clear();
            m_slide = 0.0f;
            m_clock = 0.0f;
            m_recorded = false;
            m_beatBest = false;
            m_missed = false;
            m_teetered = false;
            m_centred = 0;

            // The ground floor is placed for you, dead centre: the game is
            // what you put on top of it.
            Floor base = next();
            base.x = kCentre;
            m_floors.push_back(base);

            m_carried = next();
            m_dropping = false;
            m_carriedFeet = kHangFeet;
            m_carriedV = 0.0f;
            m_swing = 0.0f;
        }

        void begin()
        {
            reset();
            m_phase = Phase_Play;
        }

        int floors() const { return int(m_floors.size()); }

        void playing(App& app, const Input& input, float dt, bool tapped)
        {
            // B gives up the run and keeps the floors, the way the dash keeps
            // the distance. A game with no end of its own has to have a way
            // out that is not the quit button: the race and the dice can hold
            // B because they finish in seconds, and this cannot.
            if (input.back()) {
                finish(app);
                return;
            }

            // Leaning past nine tenths of the limit and getting back under a
            // third is the game's signature move - the drift is signed, so it
            // can be corrected - and nothing else rewards noticing it.
            if (!m_floors.empty()) {
                float fraction = std::abs(m_floors.back().x - m_floors.front().x)
                    / kLeanMax;
                if (fraction > 0.9f)
                    m_teetered = true;
                else if (m_teetered && fraction < 0.33f) {
                    app.store().setScore("tower_recovered", 1);
                    m_teetered = false;
                }
            }

            if (m_slide < 0.0f) {
                // The world catching up after a landing.
                //
                // An exponential approach rather than a constant crawl: it
                // covers most of the storey in the first few frames and eases
                // into place over about a third of a second, where the old
                // version slid at a flat 600 pixels a second and then stopped
                // dead on arrival. Same idiom the nav rail's highlight uses.
                m_slide += -m_slide * std::min(1.0f, dt * kCameraRate);
                // An exponential never quite arrives, and m_slide feeds the
                // landing height for the next drop, so it is snapped rather
                // than left a fraction of a pixel out forever.
                if (m_slide > -0.5f)
                    m_slide = 0.0f;
            }

            if (!m_dropping) {
                // Wider and faster the higher it gets, which is the whole of
                // the difficulty curve.
                float n = float(floors() - 1);
                float amplitude = std::min(380.0f + n * 6.0f, 520.0f);
                float rate = std::min(1.8f + n * 0.05f, 4.2f);
                m_swing += rate * dt;
                m_carried.x = kCentre + amplitude * std::sin(m_swing);
                m_carriedFeet = kHangFeet;

                if (input.accept() || tapped) {
                    m_dropping = true;
                    m_carriedV = 0.0f;
                }
                return;
            }

            m_carriedV += kGravity * dt;
            m_carriedFeet += m_carriedV * dt;

            float landing = screenFeet(floors());
            if (m_carriedFeet < landing)
                return;

            float offset = m_carried.x - m_floors.back().x;
            if (std::abs(offset) > kCatch) {
                // Nothing under it. The floor keeps going and the tower stands.
                m_missed = true;
                m_blame = m_carried.name;
                m_carried.vx = offset > 0.0f ? 240.0f : -240.0f;
                m_carried.vy = 0.0f;
                m_phase = Phase_Fall;
                m_clock = 0.0f;
                return;
            }

            // Within six pixels of the shoulders below: worth marking once.
            // Dead centre, and how many of those have come one after another.
            // The run lives in the scene rather than in the store: it is only
            // ever compared against the best, and a run that a crash
            // interrupts was not a run.
            if (std::abs(offset) <= kDeadCentre) {
                app.store().setScore("tower_centred", 1);
                m_centred++;
                app.store().noteBestScore("tower_centred_run", m_centred);
            } else {
                m_centred = 0;
            }

            m_carried.x = m_floors.back().x + offset;
            m_carried.vx = 0.0f;
            m_carried.vy = 0.0f;
            m_carried.drop = 0.0f;
            float before = lift();
            m_floors.push_back(m_carried);
            m_slide = -(lift() - before);

            if (std::abs(m_floors.back().x - m_floors.front().x) > kLeanMax) {
                topple();
                return;
            }

            m_carried = next();
            m_dropping = false;
            m_carriedFeet = kHangFeet;
        }

        void topple()
        {
            m_missed = false;
            m_blame = m_floors.empty() ? std::string() : m_floors.back().name;
            m_phase = Phase_Fall;
            m_clock = 0.0f;
            float away = m_floors.back().x >= m_floors.front().x ? 1.0f : -1.0f;
            for (size_t i = 0; i < m_floors.size(); i++) {
                // The higher a floor was, the further it goes: a tower comes
                // apart from the top.
                float share = float(i) / float(m_floors.size());
                m_floors[i].vx = away * (60.0f + share * 520.0f);
                m_floors[i].vy = -randomRange(40.0f, 220.0f) * share;
            }
        }

        void falling(float dt)
        {
            if (m_missed) {
                m_carriedV += kGravity * dt;
                m_carriedFeet += m_carriedV * dt;
                m_carried.x += m_carried.vx * dt;
                return;
            }
            for (Floor& f : m_floors) {
                f.vy += kGravity * dt;
                f.drop += f.vy * dt;
                f.x += f.vx * dt;
            }
        }

        void finish(App& app)
        {
            m_phase = Phase_Over;
            if (!m_recorded) {
                m_beatBest = app.store().noteBestScore(kGameKey, uint32_t(floors()));
                if (m_beatBest)
                    m_best = uint32_t(floors());
                m_recorded = true;
            }
        }

        // ---------------------------------------------------------- painting

        void drawFigure(Renderer& r, const Floor& f, float feet, float fade) const
        {
            if (feet < -kFigure || feet > Renderer::DesignHeight + kFigure * 2.0f)
                return;
            Rect box { f.x - kFigure * 0.42f, feet - kFigure, kFigure * 0.84f, kFigure };
            ui::miiFigure(r, box, f.mii, fade);
        }

        // Clouds, so the sky is not a flat gradient once the tower has climbed
        // above the hills.
        //
        // Procedural and unstored: a band every 420 world pixels, its x and its
        // size hashed from the band's own index, drawn through the same
        // transform the floors use - so they come down past you as the world
        // rises and a band keeps its place when it leaves the screen and comes
        // back. Only the two or three bands on screen are drawn.
        void drawClouds(Renderer& r) const
        {
            // Lighter than the sky in daylight, a pale wisp against it at
            // night. No single palette colour is lighter than that gradient in
            // both themes, so this is one of the few places worth asking which
            // one is in force.
            Color puff = theme::resolvedMode() == theme::Mode::Dark
                ? theme::fg1.scaleAlpha(0.10f)
                : theme::bg1.scaleAlpha(0.85f);

            float base = kGroundY - 260.0f + lift() + m_slide;
            int first = int(std::floor((base - Renderer::DesignHeight - 200.0f)
                / kCloudGap));
            for (int k = std::max(first, 0); k < first + 8; k++) {
                float y = base - float(k) * kCloudGap;
                if (y < -200.0f || y > Renderer::DesignHeight + 200.0f)
                    continue;

                uint32_t h = uint32_t(k) * 2654435761u;
                float x = 120.0f + float(h % 1600u);
                float scale = 0.7f + float((h >> 9) % 70u) * 0.01f;
                drawCloud(r, x, y, scale, puff);
            }
        }

        static void drawCloud(Renderer& r, float x, float y, float scale, Color puff)
        {
            // Five overlapping ellipses: three humps on a long flat base, which
            // is the fewest that stops reading as a row of circles.
            r.ellipse(x, y + 14.0f * scale, 132.0f * scale, 26.0f * scale, puff, 0.0f);
            r.ellipse(x - 54.0f * scale, y + 4.0f * scale, 52.0f * scale,
                34.0f * scale, puff, 0.0f);
            r.ellipse(x + 8.0f * scale, y - 14.0f * scale, 66.0f * scale,
                46.0f * scale, puff, 0.0f);
            r.ellipse(x + 66.0f * scale, y + 2.0f * scale, 44.0f * scale,
                30.0f * scale, puff, 0.0f);
            r.ellipse(x + 24.0f * scale, y + 10.0f * scale, 60.0f * scale,
                30.0f * scale, puff, 0.0f);
        }

        void drawTower(Renderer& r) const
        {
            for (size_t i = 0; i < m_floors.size(); i++) {
                const Floor& f = m_floors[i];
                float feet = screenFeet(int(i)) + f.drop;
                drawFigure(r, f, feet, 1.0f);
            }

            // The base's footprint, so the drift has something to be measured
            // against rather than being a feeling.
            if (m_floors.empty())
                return;
            float baseFeet = screenFeet(0);
            if (baseFeet > Renderer::DesignHeight + 40.0f)
                return;
            float x = m_floors.front().x;
            r.rect(Rect { x - kLeanMax, baseFeet + 6.0f, kLeanMax * 2.0f, 3.0f },
                theme::stroke1);
            r.rect(Rect { x - 2.0f, baseFeet + 2.0f, 4.0f, 12.0f }, theme::fg4);
        }

        void drawCarried(Renderer& r) const
        {
            // The line it hangs from, so it reads as carried rather than
            // floating.
            if (!m_dropping && m_phase == Phase_Play) {
                r.rect(Rect { m_carried.x - 1.5f, 0.0f, 3.0f,
                           m_carriedFeet - kFigure },
                    theme::stroke2);
            }
            drawFigure(r, m_carried, m_carriedFeet, 1.0f);
        }

        void drawMeter(App& app, Renderer& r)
        {
            app.hint("A", m_dropping ? "-" : "drop");
            app.hint("B", "give up");

            TextStyle count;
            count.size = theme::text3xl;
            count.weight = FontWeight::Bold;
            count.color = theme::fg1;
            count.tracking = theme::trackingTight;
            count.leading = theme::leadingTight;
            r.text(Rect { 0.0f, 54.0f, Renderer::DesignWidth - theme::edge, 100.0f },
                format("%d", floors()), count, Align::Right, VAlign::Top);

            TextStyle label;
            label.size = theme::textSm;
            label.color = uint32_t(floors()) > m_best ? theme::accent : theme::fg3;
            label.tracking = theme::trackingWide;
            r.text(Rect { 0.0f, 54.0f + theme::text3xl * theme::leadingTight,
                      Renderer::DesignWidth - theme::edge, 30.0f },
                uint32_t(floors()) > m_best && m_best > 0
                    ? "a new best"
                    : format("floors, best %u", unsigned(m_best)),
                label, Align::Right, VAlign::Top);

            // How close the lean is to going over. A bar rather than a number,
            // because it is a feeling about how much room is left.
            float lean = std::abs(m_floors.back().x - m_floors.front().x) / kLeanMax;
            Rect track { theme::edge, 70.0f, 340.0f, 10.0f };
            r.roundRect(track, 5.0f, theme::bg2);
            r.roundRect(Rect { track.x, track.y, track.w * std::min(lean, 1.0f),
                           track.h },
                5.0f, lean > 0.7f ? theme::accent : theme::fg3);
            TextStyle caption;
            caption.size = theme::textXs;
            caption.color = theme::fg4;
            caption.tracking = theme::trackingWider;
            caption.uppercase = true;
            r.text(track.x, track.bottom() + 8.0f, "lean", caption);
        }

        Rect plate(float width, float height) const
        {
            return Rect { Renderer::DesignWidth * 0.5f - width * 0.5f, 120.0f, width,
                height };
        }

        void drawReady(App& app, Renderer& r)
        {
            app.hint("A", "start");
            app.hint("B", "back");

            Rect box = plate(960.0f, 360.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.94f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s6);
            float y = inner.y;

            ui::eyebrow(r, Rect { inner.x, y, inner.w, 30.0f }, "the mii tower");
            y += 30.0f + theme::s3;

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            // Short, and ellipsised anyway: the long version was 29 characters
            // of 64px bold - about 870px in an 864px box - and the best-height
            // label shares the line with it. The eyebrow above already says
            // what this is.
            r.text(inner.x, y, r.ellipsize("Stack them up", title, inner.w * 0.62f),
                title);

            if (m_best > 0) {
                TextStyle best;
                best.size = theme::textSm;
                best.color = theme::fg3;
                best.tracking = theme::trackingWide;
                r.text(Rect { inner.x, y, inner.w, title.size * theme::leadingTight },
                    format("best %u floors", unsigned(m_best)), best, Align::Right,
                    VAlign::Middle);
            }
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            y += r.textWrapped(Rect { inner.x, y, inner.w, 90.0f },
                "A drops whoever is swinging. Miss the shoulders below and they "
                "fall; drift too far from the base and the lot goes over.",
                body, 2);

            Rect go { inner.x, std::max(inner.bottom() - 76.0f, y + theme::s5),
                ui::actionButtonWidth(r, "Start stacking"), 76.0f };
            app.touchZone(go, Zone_Start);
            ui::actionButton(r, go, "Start stacking", true,
                app.touchHeld(Zone_Start) ? 1.0f : 0.7f + 0.3f * m_pulse);
            drawBack(app, r, box);
        }

        void drawOver(App& app, Renderer& r)
        {
            app.hint("A", "again");
            app.hint("B", "back");

            Rect box = plate(960.0f, 330.0f);
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
            r.text(inner.x, y, format("%d floors", floors()), title);
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            std::string line = m_missed
                ? format("%s had nothing to stand on.", m_blame.c_str())
                : format("It went over with %s on top.", m_blame.c_str());
            if (m_beatBest)
                line = "A new best. " + line;
            r.text(inner.x, y, r.ellipsize(line, body, inner.w), body);
            y += body.size * theme::leadingNormal;

            Rect go { inner.x, std::max(inner.bottom() - 76.0f, y + theme::s5),
                ui::actionButtonWidth(r, "Stack again"), 76.0f };
            app.touchZone(go, Zone_Start);
            ui::actionButton(r, go, "Stack again", true,
                app.touchHeld(Zone_Start) ? 1.0f : 0.7f + 0.3f * m_pulse);
            drawBack(app, r, box);
        }

        void drawBack(App& app, Renderer& r, const Rect& box)
        {
            Rect back { box.right() - 60.0f, box.y + 18.0f, 42.0f, 42.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);
        }

        std::vector<Floor> m_cast;   // who is available to stack
        std::vector<Floor> m_floors; // the tower, ground floor first
        Floor m_carried;             // the one in the air
        int m_phase = Phase_Ready;
        float m_clock = 0.0f;
        float m_pulse = 0.0f;
        float m_swing = 0.0f;
        float m_slide = 0.0f; // the world catching up after a landing
        float m_carriedFeet = kHangFeet;
        float m_carriedV = 0.0f;
        bool m_dropping = false;
        bool m_missed = false;
        bool m_teetered = false; // has been within a whisker of going over
        uint32_t m_centred = 0;  // dead centre drops, one after another
        std::string m_blame;     // who the result plate is about
        uint32_t m_best = 0;
        bool m_beatBest = false;
        bool m_recorded = false;
    };
}

std::unique_ptr<Scene> makeMiiTowerScene()
{
    return std::make_unique<MiiTowerScene>();
}

} // namespace nxp
