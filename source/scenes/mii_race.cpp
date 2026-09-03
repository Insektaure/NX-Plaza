#include "app.h"
#include "core/store.h"
#include "core/util.h"
#include "core/wallet.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nxp {

namespace {

    constexpr float kTau = 6.2831853f;

    float randomUnit()
    {
        uint32_t bits = 0;
        randomBytes(&bits, sizeof(bits));
        return float(bits) / float(0xFFFFFFFFu);
    }

    float randomRange(float lo, float hi) { return lo + (hi - lo) * randomUnit(); }

    // Four Miis running left to right, and a coin on it if you like.
    //
    // The result is drawn before the countdown finishes: every racer is given a
    // finishing time up front, uniformly between kFastest and kSlowest, and the
    // whole race is that number played back. Nothing about the drawing can
    // change who wins, which is what makes "fully random" true rather than
    // nearly true - and it is also what bounds the race, since the slowest
    // possible time plus the countdown is under twenty seconds by construction.
    //
    // The wobble each racer carries is scaled by sin(pi * progress), so it is
    // zero at the line at both ends: the lead can change four times in the
    // middle and the finishing order still comes out exactly as the times say.
    //
    // On the bet: the stake is taken and written to the card the moment the
    // countdown starts, not when the race ends. Charging at the end would mean
    // quitting on a loss cost nothing, which turns a game with a house edge
    // into a coin printer. Four racers paying three for one is minus a quarter
    // of a coin a race, so the shop stays somewhere you have to show up for.
    class MiiRaceScene final : public Scene {
    public:
        enum Zone : int {
            Zone_Race = Touch_SceneBase,
            Zone_Stake,
            Zone_Back,
            Zone_Option, // index = a row on the ready screen
            Zone_Pick,   // index = a runner on the prediction screen
        };

        bool coversChrome() const override { return true; }

        // Eighteen seconds at the very worst, and not a frame longer: the
        // countdown and the run hold the quit button, the ready and result
        // screens do not.
        bool blocksExit() const override
        {
            return m_phase == Phase_Count || m_phase == Phase_Run;
        }

        void onEnter(App& app) override
        {
            cast(app);
            m_phase = Phase_Ready;
            m_clock = 0.0f;
            // Everything about the last visit goes, including the cursor: the
            // free row is what A lands on, every time the scene opens.
            m_mode = Mode_Win;
            m_row = 0;
            m_button = 0;
            m_pickStep = 0;
            m_pick[0] = -1;
            m_pick[1] = -1;
            m_staked = false;
            m_paid = 0;
            m_called = false;
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_clock += dt;
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

            TouchTarget tap;
            bool tapped = app.takeTap(tap);

            switch (m_phase) {
            case Phase_Ready:
                // The cursor is shared with the prediction screen, which has
                // four rows to this one's three. Clamped rather than trusted,
                // so no path back here can leave it pointing past the list.
                m_row = std::min(m_row, kOptions - 1);
                if (tapped) {
                    if (tap.is(Zone_Back)) {
                        app.popOverlay();
                        return;
                    }
                    if (tap.is(Zone_Option) && tap.index >= 0
                        && tap.index < kOptions) {
                        m_row = tap.index;
                        choose(app, tap.index);
                    }
                    return;
                }
                if (input.back()) {
                    app.popOverlay();
                    return;
                }
                if (input.navDown)
                    m_row = (m_row + 1) % kOptions;
                if (input.navUp)
                    m_row = (m_row - 1 + kOptions) % kOptions;
                // Only A, and only whatever the cursor is on.
                if (input.accept())
                    choose(app, m_row);
                break;

            case Phase_Predict:
                if (Wallet::get().balance() < kStake)
                    m_button = 0;

                if (tapped) {
                    if (tap.is(Zone_Back)) {
                        stepBack(app);
                        return;
                    }
                    if (m_pickStep < 2 && tap.is(Zone_Pick) && tap.index >= 0
                        && tap.index < int(m_racers.size())) {
                        pick(tap.index);
                    } else if (m_pickStep == 2 && tap.is(Zone_Race)) {
                        m_button = 0;
                        begin(app, false, Mode_Predict);
                    } else if (m_pickStep == 2 && tap.is(Zone_Stake)) {
                        m_button = 1;
                        begin(app, true, Mode_Predict);
                    }
                    return;
                }
                if (input.back()) {
                    stepBack(app);
                    return;
                }

                if (m_pickStep < 2) {
                    int count = int(m_racers.size());
                    if (input.navDown)
                        m_row = (m_row + 1) % count;
                    if (input.navUp)
                        m_row = (m_row - 1 + count) % count;
                    if (input.accept())
                        pick(m_row);
                } else {
                    if (input.navLeft)
                        m_button = 0;
                    if (input.navRight && Wallet::get().balance() >= kStake)
                        m_button = 1;
                    if (input.accept())
                        begin(app, m_button == 1, Mode_Predict);
                }
                break;

            case Phase_Done: {
                // A prediction has one way on from here; a win bet has two.
                bool choice = m_mode != Mode_Predict;
                if (!choice || Wallet::get().balance() < kStake)
                    m_button = 0;

                if (tapped) {
                    if (tap.is(Zone_Back)) {
                        app.popOverlay();
                        return;
                    }
                    if (tap.is(Zone_Race)) {
                        m_button = 0;
                        again(app, false);
                    } else if (choice && tap.is(Zone_Stake)) {
                        m_button = 1;
                        again(app, true);
                    }
                    return;
                }
                if (input.back()) {
                    app.popOverlay();
                    return;
                }
                if (choice) {
                    if (input.navLeft)
                        m_button = 0;
                    if (input.navRight && Wallet::get().balance() >= kStake)
                        m_button = 1;
                }
                if (input.accept())
                    again(app, m_button == 1);
                break;
            }

            // Neither B nor + does anything from here until the result is up.
            // A race is three seconds of countdown and at most fourteen of
            // running: short enough to be worth protecting from a stray thumb,
            // and far too short to be worth an escape hatch.
            case Phase_Count:
                if (m_clock >= kCountdown) {
                    m_phase = Phase_Run;
                    m_clock = 0.0f;
                }
                break;

            case Phase_Run:
                // Held open a moment after the last one is home, so the finish
                // is something you watch rather than something you are told.
                if (m_clock >= m_slowest + kHold)
                    settle();
                break;
            }
        }

        void draw(App& app, Renderer& r) override
        {
            r.clear(theme::bg0);

            float lead = 0.0f;
            for (const Racer& racer : m_racers)
                lead = std::max(lead, progress(racer));

            // The camera keeps the leader a little left of centre, and stops
            // once the finish is on screen so the line does not slide away
            // under the last few strides.
            float leadX = kStartX + lead * kTrack;
            float camera = std::min(std::max(leadX - Renderer::DesignWidth * 0.42f, 0.0f),
                kStartX + kTrack + 260.0f - Renderer::DesignWidth);

            drawBackground(r, camera);
            drawTrack(r, camera);
            for (size_t i = 0; i < m_racers.size(); i++)
                drawRacer(r, m_racers[i], int(i), camera);

            switch (m_phase) {
            case Phase_Ready:
                drawReady(app, r);
                break;
            case Phase_Predict:
                drawPredict(app, r);
                break;
            case Phase_Count:
                drawCountdown(app, r);
                break;
            case Phase_Run:
                drawStanding(app, r);
                break;
            case Phase_Done:
                drawResult(app, r);
                break;
            }
        }

    private:
        enum Phase : int {
            Phase_Ready = 0,
            Phase_Predict, // choosing who comes first and second
            Phase_Count,
            Phase_Run,
            Phase_Done,
        };

        // What the race is being played for. Both can be played for nothing:
        // a prediction with no coin on it still tells you whether you read the
        // field right, which is the whole of what it is for.
        enum Mode : int {
            Mode_Win = 0, // your own Mii to win
            Mode_Predict, // first and second, in order
        };

        struct Racer {
            Mii mii;
            std::string name;
            bool you = false;
            float finish = 12.0f; // seconds to the line
            float wobble = 0.0f;  // how much this one drifts mid-race
            float freq = 1.0f;
            float phase = 0.0f;
            int place = 0;
        };

        static constexpr int kRacers = 4;
        // Nine to fourteen seconds, a 3.2s countdown and a 0.8s hold on the
        // finish: eighteen seconds at the very worst, against the twenty the
        // race is allowed.
        static constexpr float kFastest = 9.0f;
        static constexpr float kSlowest = 14.0f;
        static constexpr float kCountdown = 3.2f;
        static constexpr float kHold = 0.8f;
        static constexpr float kTrack = 5200.0f; // world units from start to line
        static constexpr float kStartX = 220.0f;
        // Four lanes between the horizon and the hint strip: 372 + 4 * 152 is
        // 980, which leaves the 88px strip at 992 alone. A 132px figure in a
        // 152px lane has room for its own bounce without treading on the lane
        // above.
        static constexpr float kLaneTop = 372.0f;
        static constexpr float kLaneStep = 152.0f;
        static constexpr float kFigure = 132.0f;
        static constexpr float kButton = 76.0f;
        static constexpr uint32_t kStake = 1;
        // The stake back and two more, so betting on your own Mii is worth
        // three. One runner in four is a quarter of the time, so three-for-one
        // is a house edge of a quarter of a coin a race.
        static constexpr uint32_t kPayoutWin = 3;
        // First and second in order is one of twelve orderings, so eleven -
        // the coin back and ten - is an edge of a twelfth. Longer odds, longer
        // price, and still downhill for the player over enough races.
        static constexpr uint32_t kPayoutExacta = 11;
        static constexpr float kOption = 76.0f;

        // ------------------------------------------------------------- setup

        void cast(App& app)
        {
            m_racers.clear();

            Pass mine = app.store().myPass();
            Racer you;
            you.mii = mine.face();
            you.name = mine.handle.empty() ? std::string("You") : mine.handle;
            you.you = true;
            m_racers.push_back(std::move(you));

            // Three out of the collection, by the reservoir the Square uses, so
            // it does not care that the collection may be five thousand long.
            std::vector<Crossing> crossings = app.store().crossings();
            std::vector<const Crossing*> chosen;
            size_t wanted = kRacers - 1;
            for (size_t i = 0; i < crossings.size(); i++) {
                if (chosen.size() < wanted) {
                    chosen.push_back(&crossings[i]);
                    continue;
                }
                uint32_t bits = 0;
                randomBytes(&bits, sizeof(bits));
                size_t slot = bits % (i + 1);
                if (slot < wanted)
                    chosen[slot] = &crossings[i];
            }
            for (const Crossing* c : chosen) {
                Racer other;
                other.mii = c->pass.face();
                other.name = c->pass.handle.empty() ? std::string("A stranger")
                                                    : c->pass.handle;
                m_racers.push_back(std::move(other));
            }

            // A console that has met nobody still gets a race. The faces come
            // from the same seeding a blank pass uses, so they are plausible
            // people rather than four copies of you.
            while (m_racers.size() < kRacers) {
                Pass stranger;
                uint32_t seed = 0;
                randomBytes(&seed, sizeof(seed));
                stranger.portrait = seed;
                Racer other;
                other.mii = stranger.face();
                other.name = "A stranger";
                m_racers.push_back(std::move(other));
            }

            // Your own lane is drawn, not fixed: a race where you are always in
            // lane one tells you where to look before it starts.
            uint32_t swap = 0;
            randomBytes(&swap, sizeof(swap));
            std::swap(m_racers[0], m_racers[swap % m_racers.size()]);

            draw_lots();
        }

        // The whole result, decided here and played back by the drawing.
        void draw_lots()
        {
            m_slowest = 0.0f;
            for (Racer& racer : m_racers) {
                racer.finish = randomRange(kFastest, kSlowest);
                racer.wobble = randomRange(0.03f, 0.07f);
                racer.freq = randomRange(1.5f, 3.5f);
                racer.phase = randomRange(0.0f, kTau);
                racer.place = 0;
                m_slowest = std::max(m_slowest, racer.finish);
            }

            std::vector<int> order(m_racers.size());
            for (size_t i = 0; i < order.size(); i++)
                order[i] = int(i);
            std::sort(order.begin(), order.end(), [this](int a, int b) {
                return m_racers[size_t(a)].finish < m_racers[size_t(b)].finish;
            });
            for (size_t i = 0; i < order.size(); i++)
                m_racers[size_t(order[i])].place = int(i) + 1;
        }

        static constexpr int kOptions = 3;

        // A row on the ready screen.
        void choose(App& app, int option)
        {
            switch (option) {
            case 0:
                begin(app, false, Mode_Win);
                break;
            case 1:
                begin(app, true, Mode_Win);
                break;
            default:
                // The mode is set on the way in, not in begin(): the bet
                // button prices whatever payout() returns, and payout() reads
                // the mode - so setting it only at the start of the race left
                // the prediction screen offering three coins for an eleven
                // coin bet.
                m_mode = Mode_Predict;
                m_phase = Phase_Predict;
                m_pickStep = 0;
                m_pick[0] = -1;
                m_pick[1] = -1;
                m_row = 0;
                m_button = 0;
                break;
            }
        }

        // Racing again from the result keeps whatever was played for, so the
        // person working through a prediction is not sent back to the menu
        // every time - but the pick itself is drawn fresh, since the field is.
        void again(App& app, bool staked)
        {
            if (m_mode == Mode_Predict) {
                // Straight back to the pick screen. `staked` is not consulted
                // and the result screen does not ask for it: the coin is
                // decided there, once there is something to bet on.
                m_phase = Phase_Predict;
                m_pickStep = 0;
                m_pick[0] = -1;
                m_pick[1] = -1;
                m_row = 0;
                m_button = 0;
                return;
            }
            begin(app, staked, Mode_Win);
        }

        void pick(int racer)
        {
            if (racer < 0 || racer >= int(m_racers.size()))
                return;
            if (m_pickStep == 0) {
                m_pick[0] = racer;
                m_pickStep = 1;
                // Onto somebody who is still available, so A twice in a row
                // cannot pick the same runner for both places.
                m_row = (racer + 1) % int(m_racers.size());
                return;
            }
            if (m_pickStep == 1) {
                if (racer == m_pick[0])
                    return;
                m_pick[1] = racer;
                m_pickStep = 2;
            }
        }

        void stepBack(App& app)
        {
            if (m_pickStep == 0) {
                m_phase = Phase_Ready;
                m_row = 2;
                return;
            }
            m_pickStep--;
            m_pick[m_pickStep] = -1;
            m_row = 0;
            (void)app;
        }

        uint32_t payout() const
        {
            return m_mode == Mode_Predict ? kPayoutExacta : kPayoutWin;
        }

        void begin(App& app, bool staked, int mode)
        {
            Wallet& wallet = Wallet::get();
            if (staked) {
                if (!wallet.spend(kStake)) {
                    app.toast("Nothing to bet",
                        "Ten coins arrive on each new day you open the app.");
                    return;
                }
                // Written now, so quitting on a bad race still costs the coin.
                wallet.flush();
            }
            m_mode = mode;
            m_staked = staked;
            m_paid = 0;
            m_called = false;
            // Back to the free button for the next one. Betting twice takes
            // two deliberate presses of Right, which is the point: the result
            // screen appears under your thumb, and whatever it says, A on it
            // should never cost a coin you did not choose to spend.
            m_button = 0;
            draw_lots();
            m_phase = Phase_Count;
            m_clock = 0.0f;
        }

        // Did the thing that was played for come in?
        bool called() const
        {
            if (m_mode == Mode_Predict) {
                if (m_pick[0] < 0 || m_pick[1] < 0)
                    return false;
                return m_racers[size_t(m_pick[0])].place == 1
                    && m_racers[size_t(m_pick[1])].place == 2;
            }
            for (const Racer& racer : m_racers) {
                if (racer.you)
                    return racer.place == 1;
            }
            return false;
        }

        void settle()
        {
            m_phase = Phase_Done;
            // The clock keeps running rather than resetting: progress() reads
            // it, so zeroing it here sent everybody back to the start line and
            // ran the whole race again behind the result plate.
            m_called = called();
            if (!m_staked || !m_called)
                return;

            Wallet& wallet = Wallet::get();
            wallet.award(payout());
            wallet.flush();
            m_paid = payout();
        }

        // ------------------------------------------------------------ motion

        float progress(const Racer& racer) const
        {
            // Everybody is home by the time the result is up, and saying so
            // here means no future change to the clock can restart them.
            if (m_phase == Phase_Done)
                return 1.0f;
            // Only a running race reads the clock. This was a list of the
            // phases that stand still instead, which is the wrong way round:
            // adding the prediction screen added a phase the list did not
            // name, so it fell through to the arithmetic and - with the clock
            // counting since the scene opened - put the whole field on the
            // finish line while you were choosing.
            if (m_phase != Phase_Run)
                return 0.0f;
            float u = std::min(m_clock / racer.finish, 1.0f);
            // Zero at both ends, so the order the lots drew is the order that
            // crosses the line however much the middle of the race moves.
            float drift = std::sin(u * kTau * racer.freq + racer.phase)
                * racer.wobble * std::sin(u * 3.14159265f);
            return std::min(std::max(u + drift, 0.0f), 1.0f);
        }

        const Racer* leader() const
        {
            const Racer* best = nullptr;
            float most = -1.0f;
            for (const Racer& racer : m_racers) {
                float p = progress(racer);
                if (p > most) {
                    most = p;
                    best = &racer;
                }
            }
            return best;
        }

        // ---------------------------------------------------------- painting

        // Three layers at three speeds. Nothing here is art: a gradient, some
        // hills, some lamp posts. The parallax is what says "moving", which is
        // the only job it has when the runners themselves stay put on screen.
        void drawBackground(Renderer& r, float camera)
        {
            Rect sky { 0.0f, 0.0f, Renderer::DesignWidth, kLaneTop };
            r.gradientRect(sky, theme::plazaTop, theme::plazaMid);

            // Far hills, a quarter of the speed.
            float far = camera * 0.22f;
            for (int i = -1; i < 8; i++) {
                float cx = float(i) * 520.0f - std::fmod(far, 520.0f);
                float w = 620.0f;
                float h = 190.0f + float((i + 8) % 3) * 46.0f;
                r.ellipse(cx, sky.bottom(), w * 0.5f, h, theme::plazaBottom, 0.0f);
            }

            // Lamp posts, half speed, so there is something with an edge to it
            // to measure the motion against.
            float mid = camera * 0.55f;
            for (int i = -1; i < 12; i++) {
                float x = float(i) * 340.0f - std::fmod(mid, 340.0f);
                Rect post { x, sky.bottom() - 210.0f, 8.0f, 210.0f };
                r.rect(post, theme::bg2);
                r.glow(Rect { post.centerX() - 34.0f, post.y - 34.0f, 68.0f, 68.0f },
                    theme::accentGlow.scaleAlpha(0.35f), 1.8f);
                r.circle(post.centerX(), post.y, 13.0f, theme::mark);
            }
        }

        void drawTrack(Renderer& r, float camera)
        {
            Rect ground { 0.0f, kLaneTop, Renderer::DesignWidth,
                Renderer::DesignHeight - kLaneTop };
            r.rect(ground, theme::bg1);

            // Lane separators, and the ground stripes that run at full speed.
            for (int lane = 0; lane <= kRacers; lane++) {
                float y = kLaneTop + float(lane) * kLaneStep;
                r.rect(Rect { 0.0f, y, Renderer::DesignWidth, theme::stroke },
                    theme::stroke1);
            }
            for (int i = -1; i < 26; i++) {
                float x = float(i) * 160.0f - std::fmod(camera, 160.0f);
                for (int lane = 0; lane < kRacers; lane++) {
                    float y = kLaneTop + float(lane) * kLaneStep + kLaneStep * 0.5f;
                    r.rect(Rect { x, y, 60.0f, 4.0f }, theme::bg3);
                }
            }

            // The line: a chequered post, and the start line behind you.
            drawChequer(r, kStartX - camera - 30.0f, theme::bg3);
            drawChequer(r, kStartX + kTrack - camera, theme::fg1);
        }

        void drawChequer(Renderer& r, float x, Color ink)
        {
            if (x < -60.0f || x > Renderer::DesignWidth + 60.0f)
                return;
            float top = kLaneTop;
            float bottom = kLaneTop + float(kRacers) * kLaneStep;
            r.rect(Rect { x, top, 26.0f, bottom - top }, theme::bg0);
            int rows = int((bottom - top) / 26.0f);
            for (int i = 0; i < rows; i++) {
                if (i % 2 == 0)
                    continue;
                r.rect(Rect { x, top + float(i) * 26.0f, 13.0f, 26.0f }, ink);
                r.rect(Rect { x + 13.0f, top + float(i) * 26.0f - 26.0f, 13.0f, 26.0f },
                    ink);
            }
        }

        void drawRacer(Renderer& r, const Racer& racer, int lane, float camera)
        {
            float p = progress(racer);
            float x = kStartX + p * kTrack - camera;
            float baseY = kLaneTop + float(lane) * kLaneStep + kLaneStep - 12.0f;

            // A bob rather than legs: the figure is head and shoulders, so what
            // sells running is the bounce and the shadow it leaves.
            bool running = m_phase == Phase_Run && p < 1.0f;
            float bob = running
                ? std::abs(std::sin(m_clock * 11.0f + racer.phase)) * 11.0f
                : 0.0f;

            r.ellipse(x, baseY + 4.0f, 44.0f - bob * 0.8f, 11.0f - bob * 0.2f,
                theme::bg0.scaleAlpha(0.35f), 0.0f);

            Rect box { x - kFigure * 0.42f, baseY - kFigure - bob, kFigure * 0.84f,
                kFigure };
            if (racer.you) {
                // Your own runner carries the accent, because four Miis in
                // motion is exactly when a name label is hardest to read.
                Color rim = theme::accent.scaleAlpha(0.55f);
                ui::miiSilhouette(r, box.inset(-5.0f), racer.mii, rim);
            }
            ui::miiFigure(r, box, racer.mii, 1.0f);

            TextStyle name;
            name.size = theme::textSm;
            name.weight = FontWeight::Bold;
            name.color = racer.you ? theme::accent : theme::fg2;
            std::string label = r.ellipsize(racer.name, name, 200.0f);
            r.text(Rect { x - 100.0f, box.y - 30.0f, 200.0f, 26.0f }, label, name,
                Align::Center, VAlign::Top);
        }

        // A plate the overlays sit on, so text never lands on a lamp post.
        Rect plate(float width, float height) const
        {
            return Rect { Renderer::DesignWidth * 0.5f - width * 0.5f,
                Renderer::DesignHeight * 0.5f - height * 0.5f - 30.0f, width, height };
        }

        void drawReady(App& app, Renderer& r)
        {
            uint32_t coins = Wallet::get().balance();
            app.hint("A", "choose");
            app.hint("B", "back");

            // Three rows rather than a row of buttons: a third choice made the
            // widest button wider than the plate, and a list grows downwards
            // for as long as there are things to play for.
            Rect box = plate(1100.0f, 520.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.94f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s6);
            float y = inner.y;

            ui::eyebrow(r, Rect { inner.x, y, inner.w, 30.0f }, "the mii race");
            y += 30.0f + theme::s3;

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            r.text(inner.x, y, "Four runners, one line", title);

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
            body.leading = theme::leadingNormal;
            r.text(inner.x, y,
                "Nobody is faster than anybody. Nothing you do changes that.", body);
            y += body.size * theme::leadingNormal + theme::s5;

            const char* labels[kOptions] = {
                "Race for free",
                "Bet 1 coin - your Mii to win",
                "Predict 1st and 2nd",
            };
            std::string notes[kOptions] = {
                std::string("Just to watch."),
                format("Pays %u if it comes in.", unsigned(kPayoutWin)),
                format("For free, or bet 1 coin to win %u.", unsigned(kPayoutExacta)),
            };

            for (int i = 0; i < kOptions; i++) {
                Rect row { inner.x, y, inner.w, kOption };
                bool poor = i == 1 && coins < kStake;
                drawOption(app, r, row, i, labels[i],
                    poor ? std::string("Nothing to bet with.") : notes[i], !poor);
                y += kOption + theme::s3;
            }

            drawBack(app, r, box);
        }

        void drawOption(App& app, Renderer& r, const Rect& row, int index,
            const char* label, const std::string& note, bool live)
        {
            bool focused = index == m_row;
            app.touchZone(row, Zone_Option, index);
            float focus = focused
                ? (app.touchHeld(Zone_Option, index) ? 1.0f : 0.7f + 0.3f * m_pulse)
                : 0.0f;
            ui::card(r, row, focus, focused ? theme::bg3 : theme::bg2, theme::r3);

            Rect inner = row.inset(theme::s5, theme::s4);
            TextStyle name;
            name.size = theme::textBase;
            name.weight = FontWeight::Bold;
            name.color = live ? theme::fg1 : theme::fg3;
            r.text(inner.x, inner.centerY() - name.size * 0.62f, label, name);

            TextStyle hint;
            hint.size = theme::textSm;
            hint.color = live ? theme::fg3 : theme::fg4;
            r.text(Rect { inner.x, inner.y, inner.w, inner.h }, note, hint, Align::Right,
                VAlign::Middle);
        }

        // Pick who comes first, then who comes second. The field is the four
        // runners already standing on the line behind this plate, so the names
        // here are the names out there.
        void drawPredict(App& app, Renderer& r)
        {
            uint32_t coins = Wallet::get().balance();
            app.hint("A", m_pickStep < 2 ? "choose" : "race");
            app.hint("B", "back");

            // 30 of eyebrow, 67 of title, a line of text, four 76px rows and a
            // 76px button row: 622 of interior, which 720 provides.
            Rect box = plate(1100.0f, 720.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.94f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s6);
            float y = inner.y;

            ui::eyebrow(r, Rect { inner.x, y, inner.w, 30.0f }, "your prediction");
            y += 30.0f + theme::s3;

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            const char* asks[3] = { "Who comes first?", "And who comes second?",
                "That is your call" };
            r.text(inner.x, y, asks[m_pickStep], title);
            y += title.size * theme::leadingTight + theme::s3;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            r.text(inner.x, y, callText(), body);
            y += body.size * theme::leadingNormal + theme::s5;

            for (size_t i = 0; i < m_racers.size(); i++) {
                Rect row { inner.x, y, inner.w, kOption };
                drawPickRow(app, r, row, int(i));
                y += kOption + theme::s3;
            }

            if (m_pickStep == 2) {
                drawButtons(app, r,
                    Rect { inner.x, inner.bottom() - kButton, inner.w, kButton },
                    "Race for free", coins >= kStake);
            }
            drawBack(app, r, box);
        }

        void drawPickRow(App& app, Renderer& r, const Rect& row, int index)
        {
            const Racer& racer = m_racers[size_t(index)];
            bool first = index == m_pick[0];
            bool second = index == m_pick[1];
            bool taken = first || second;
            bool choosable = m_pickStep < 2 && !taken;
            bool focused = m_pickStep < 2 && index == m_row;

            if (choosable || m_pickStep == 2)
                app.touchZone(row, Zone_Pick, index);
            float focus = focused
                ? (app.touchHeld(Zone_Pick, index) ? 1.0f : 0.7f + 0.3f * m_pulse)
                : 0.0f;
            ui::card(r, row, focus, taken ? theme::bg3 : theme::bg2, theme::r3);

            Rect inner = row.inset(theme::s5, theme::s4);

            // The runner's own face, so the pick is made against what is on the
            // track rather than against a list of words.
            Rect head { inner.x, inner.centerY() - 26.0f, 52.0f, 52.0f };
            ui::miiHead(r, head, racer.mii, 1.0f);

            TextStyle name;
            name.size = theme::textBase;
            name.weight = FontWeight::Bold;
            name.color = racer.you ? theme::accent : theme::fg1;
            r.text(head.right() + theme::s4, inner.centerY() - name.size * 0.62f,
                r.ellipsize(racer.name, name, inner.w * 0.6f), name);

            if (taken) {
                TextStyle badge;
                badge.size = theme::textSm;
                badge.weight = FontWeight::Bold;
                std::string label = first ? "1st" : "2nd";
                float width = r.measure(label, badge) + theme::s5;
                ui::pill(r,
                    Rect { inner.right() - width, inner.centerY() - 18.0f, width, 36.0f },
                    label, theme::bg0, theme::accent, theme::textSm);
            }
        }

        // What has been called so far, in words, so the plate always says what
        // pressing A would commit to.
        std::string callText() const
        {
            if (m_pick[0] < 0)
                return format("First and second, in order. Eleven for a coin if you "
                              "call it, or watch for nothing.");
            const std::string& first = m_racers[size_t(m_pick[0])].name;
            if (m_pick[1] < 0)
                return format("%s to win. Now who is behind them?", first.c_str());
            return format("%s first, %s second.", first.c_str(),
                m_racers[size_t(m_pick[1])].name.c_str());
        }

        void drawCountdown(App& app, Renderer& r)
        {
            app.hint("B", "-");

            // Four beats of eight tenths. Each one arrives big and settles, so
            // the numbers have a pulse rather than a blink.
            int beat = std::min(int(m_clock / 0.8f), 3);
            float within = std::fmod(m_clock, 0.8f) / 0.8f;
            const char* words[4] = { "3", "2", "1", "Go" };

            TextStyle count;
            count.size = theme::text4xl * (1.35f - 0.35f * std::min(within * 2.5f, 1.0f));
            count.weight = FontWeight::Bold;
            count.color = beat == 3 ? theme::accent : theme::fg1;
            count.tracking = theme::trackingTight;

            float fade = 1.0f - std::max(0.0f, (within - 0.75f) / 0.25f);
            count.color = count.color.scaleAlpha(fade);

            Rect where { 0.0f, Renderer::DesignHeight * 0.5f - 140.0f,
                Renderer::DesignWidth, 200.0f };
            r.glow(Rect { where.centerX() - 200.0f, where.y - 40.0f, 400.0f, 280.0f },
                theme::accentGlow.scaleAlpha(0.20f * fade), 2.0f);
            r.text(where, words[beat], count, Align::Center, VAlign::Middle);
        }

        void drawStanding(App& app, Renderer& r)
        {
            app.hint("B", "-");
            const Racer* front = leader();
            if (!front)
                return;

            TextStyle style;
            style.size = theme::textMd;
            style.weight = FontWeight::Bold;
            style.color = front->you ? theme::accent : theme::fg2;
            r.text(Rect { 0.0f, 60.0f, Renderer::DesignWidth - theme::edge, 40.0f },
                front->you ? std::string("You are in front")
                           : format("%s is in front", front->name.c_str()),
                style, Align::Right, VAlign::Middle);
        }

        void drawResult(App& app, Renderer& r)
        {
            app.hint("A", m_mode == Mode_Predict ? "predict again" : "race again");
            app.hint("B", "back");

            Rect box = plate(1100.0f, 520.0f);
            r.roundRect(box, theme::r5, theme::bg1.scaleAlpha(0.96f));
            r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);
            Rect inner = box.inset(theme::s7, theme::s6);

            // Only the winner is needed here now: whether the thing played
            // for came in is m_called, decided at the line rather than worked
            // out again from the field.
            const Racer* first = nullptr;
            for (const Racer& racer : m_racers) {
                if (racer.place == 1)
                    first = &racer;
            }

            TextStyle title;
            title.size = theme::text2xl;
            title.weight = FontWeight::Bold;
            title.color = m_called ? theme::accent : theme::fg1;
            title.tracking = theme::trackingTight;
            title.leading = theme::leadingTight;
            // The headline is about whatever was played for: your own runner
            // winning, or the order coming in as called.
            std::string headline;
            if (m_mode == Mode_Predict) {
                headline = m_called ? std::string("Called it")
                                    : format("%s won", first ? first->name.c_str()
                                                             : "Nobody");
            } else {
                headline = m_called ? std::string("You won")
                                    : format("%s won", first ? first->name.c_str()
                                                             : "Nobody");
            }
            r.text(inner.x, inner.y, headline, title);
            float y = inner.y + title.size * theme::leadingTight + 8.0f;

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;

            // What was called, next to what happened, so a near miss reads as
            // a near miss rather than as a flat no.
            if (m_mode == Mode_Predict && m_pick[0] >= 0 && m_pick[1] >= 0) {
                const Racer& one = m_racers[size_t(m_pick[0])];
                const Racer& two = m_racers[size_t(m_pick[1])];
                r.text(inner.x, y,
                    format("You said %s then %s - they came %s and %s.",
                        one.name.c_str(), two.name.c_str(), ordinal(one.place),
                        ordinal(two.place)),
                    body);
                y += body.size * theme::leadingNormal + 4.0f;
            }

            std::string line;
            if (m_staked) {
                line = m_paid > 0
                    ? format("The bet paid %u - %u coins to spend.", unsigned(m_paid),
                          unsigned(Wallet::get().balance()))
                    : format("The bet cost you a coin. %u left.",
                          unsigned(Wallet::get().balance()));
            } else {
                line = m_called ? "Nothing bet, but right is right."
                                : "Nothing bet, nothing lost.";
            }
            r.text(inner.x, y, line, body);

            // The order, so a fourth place still says who beat you.
            y += body.size * theme::leadingNormal + theme::s5;
            for (int place = 1; place <= int(m_racers.size()); place++) {
                for (const Racer& racer : m_racers) {
                    if (racer.place != place)
                        continue;
                    TextStyle row;
                    row.size = theme::textBase;
                    row.weight = racer.you ? FontWeight::Bold : FontWeight::Regular;
                    row.color = racer.you ? theme::accent : theme::fg2;
                    r.text(inner.x, y, format("%d.  %s", place, racer.name.c_str()), row);
                    r.text(Rect { inner.x, y, inner.w, row.size * theme::leadingSnug },
                        format("%.2fs", double(racer.finish)), row, Align::Right,
                        VAlign::Top);
                    y += row.size * theme::leadingSnug + 8.0f;
                }
            }

            Rect row { inner.x, inner.bottom() - kButton, inner.w, kButton };
            if (m_mode == Mode_Predict) {
                // One button, because both of them did the same thing: a
                // prediction goes back to the pick screen, and the choice
                // between free and a coin is made there, after the two runners
                // have been named. Offering it twice priced a bet that had not
                // been placed yet.
                drawOneButton(app, r, row, "Predict again");
            } else {
                drawButtons(app, r, row, "Race again",
                    Wallet::get().balance() >= kStake);
            }
            drawBack(app, r, box);
        }

        static const char* ordinal(int place)
        {
            switch (place) {
            case 1:
                return "1st";
            case 2:
                return "2nd";
            case 3:
                return "3rd";
            default:
                return "4th";
            }
        }

        // The two ways to start, side by side, so the coin is a thing on the
        // screen and not only a line in the hint strip. The staked one goes
        // quiet when there is nothing to stake.
        void drawButtons(App& app, Renderer& r, const Rect& row, const char* plainLabel,
            bool canStake)
        {
            std::string staked = format("Bet %u coin to win %u if lucky!",
                unsigned(kStake), unsigned(payout()));
            float plainW = ui::actionButtonWidth(r, plainLabel);
            float stakeW = ui::actionButtonWidth(r, staked);

            Rect plain { row.x, row.y, plainW, row.h };
            Rect stake { plain.right() + theme::s4, row.y, stakeW, row.h };

            app.touchZone(plain, Zone_Race);
            if (canStake)
                app.touchZone(stake, Zone_Stake);

            // The cursor rides the pulse the rest of the app uses for focus, so
            // left and right visibly move between the two.
            float pulse = 0.7f + 0.3f * m_pulse;
            bool onStake = m_button == 1 && canStake;
            ui::actionButton(r, plain, plainLabel, !onStake,
                app.touchHeld(Zone_Race) ? 1.0f : (onStake ? 0.0f : pulse));
            ui::actionButton(r, stake, staked, onStake,
                canStake && app.touchHeld(Zone_Stake) ? 1.0f
                                                      : (onStake ? pulse : 0.0f));
        }

        // The only thing to press, so it is always the focused thing.
        void drawOneButton(App& app, Renderer& r, const Rect& row, const char* label)
        {
            Rect only { row.x, row.y, ui::actionButtonWidth(r, label), row.h };
            app.touchZone(only, Zone_Race);
            ui::actionButton(r, only, label, true,
                app.touchHeld(Zone_Race) ? 1.0f : 0.7f + 0.3f * m_pulse);
        }

        void drawBack(App& app, Renderer& r, const Rect& box)
        {
            Rect back { box.right() - 60.0f, box.y + 18.0f, 42.0f, 42.0f };
            app.touchZone(back.inset(-theme::s3, -theme::s3), Zone_Back);
            ui::icon(r, back, ui::Icon::ArrowLeft, theme::fg3, 3.0f);
        }

        std::vector<Racer> m_racers;
        int m_phase = Phase_Ready;
        float m_clock = 0.0f;
        float m_slowest = kSlowest;
        bool m_staked = false;
        uint32_t m_paid = 0;
        int m_button = 0; // 0 = race for free, 1 = race with a bet
        float m_pulse = 0.0f;

        int m_mode = Mode_Win;
        int m_row = 0;      // the ready screen's cursor
        int m_pickStep = 0; // 0 = first, 1 = second, 2 = ready to run
        int m_pick[2] = { -1, -1 };
        bool m_called = false; // the prediction came in
    };
}

std::unique_ptr<Scene> makeMiiRaceScene() { return std::make_unique<MiiRaceScene>(); }

} // namespace nxp
