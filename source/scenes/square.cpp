#include "app.h"
#include "core/store.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {
    constexpr size_t kMaxFigures = 20;

    // The ground, in world units. Depth runs away from the camera.
    constexpr float kNearZ = 6.0f;
    constexpr float kFarZ = 29.0f;
    constexpr float kFocal = 670.0f;        // pixels per world unit at unit depth
    constexpr float kPersonHeight = 1.7f;   // world units, head to floor
    // The camera is high and looking down, which is what puts the floor across
    // the whole frame and the vanishing point off the top of it.
    constexpr float kCameraHeight = 9.9f;
    // Where parallel floor lines would meet, as a fraction of the stage height.
    // Negative, so it sits above the frame and no horizon is ever drawn.
    constexpr float kHorizonAt = -0.15f;

    // The floor, in world units. One tile is this wide and this deep.
    constexpr float kTile = 2.0f;
    constexpr float kFloorNear = 3.0f;
    constexpr float kFloorFar = 40.0f;
    // Past this a row's tiles are thinner than a line and only cost quads.
    constexpr int kMaxCellsPerRow = 34;
    // Half the view cone's width per unit of depth. Sideways room grows with
    // distance, which is what keeps a figure inside the frame wherever it
    // stands: the alternative is a fixed world width, where anyone near the
    // camera and off to one side is projected clean off the screen.
    // Widened so the crowd reaches the frame edges rather than sitting in a
    // column down the middle. Screen spread is kSpread * kFocal whatever the
    // depth, so this is 837 of the stage's 904 half-width.
    constexpr float kSpread = 1.25f;

    // Everybody is drawn in full, at every depth.

    constexpr float kSpeed = 0.55f;       // world units a second, unhurried
    // Closer than a figure is wide, so the crowd overlaps the way a crowd
    // does. Any further apart and it reads as a queue.
    constexpr float kSeparation = 1.15f;
    constexpr float kBubbleSeconds = 4.5f;
    constexpr float kBubbleGap = 3.0f;
    // Tall enough to reach from the bubble down to the top of a head, so the
    // two read as one thing rather than as a label floating nearby.
    constexpr float kTailHeight = 26.0f;
    constexpr float kTailWidth = 30.0f;
    // Wide enough that a full greeting takes two lines rather than four, and
    // narrow enough to sit over one person rather than across the square. There
    // is no line limit: a greeting is sixty characters at most, so the bubble
    // grows to hold all of it rather than ellipsising.
    constexpr float kBubbleMaxWidth = 520.0f;
    // Below this a speaker is too small for a bubble to point at usefully.
    constexpr float kBubbleMinFigure = 70.0f;

    // The corner the eyebrow and its line of copy occupy. The crowd is kept out
    // of it: text over a moving crowd is unreadable, and a crowd that walks
    // behind text looks like a mistake rather than like depth.
    constexpr float kHeaderWidth = 680.0f;
    constexpr float kHeaderBottom = 190.0f; // from the top of the stage
    // Roughly half a near figure, so somebody standing just past the header
    // still does not clip into its last word.
    constexpr float kFigureHalfWidth = 60.0f;

    struct Figure {
        Mii mii;
        std::string name;
        std::string greeting;
        std::string crossingId; // empty for the console's own Mii
        float x = 0.0f, z = 0.0f;
        float targetX = 0.0f, targetZ = 0.0f;
        float bob = 0.0f; // phase, so they do not all bounce together
    };

    float randomUnit()
    {
        uint32_t bits = 0;
        randomBytes(&bits, sizeof(bits));
        return float(bits % 100000u) / 100000.0f;
    }

    float randomRange(float lo, float hi) { return lo + randomUnit() * (hi - lo); }

    class SquareScene final : public Scene {
    public:
        void onEnter(App& app) override
        {
            // Only when the collection has actually changed. Re-picking the cast
            // every time this tab is opened would mean the square is a different
            // place each time, and nobody would ever recognise it.
            uint64_t generation = app.store().crossingsGeneration();
            if (m_figures.empty() || generation != m_generation) {
                m_generation = generation;
                populate(app);
            }
            m_focus = ownIndex();
        }

        void update(App& app, const Input& input, float dt) override
        {
            m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);
            m_time += dt;

            if (m_figures.empty())
                return;

            wander(app.contentArea(), dt);
            pickBubble(dt);

            if (input.navLeft)
                stepFocus(-1);
            if (input.navRight)
                stepFocus(1);
            if (input.pressed(HidNpadButton_X))
                populate(app);

            if (input.accept())
                open(app);
        }

        void draw(App& app, Renderer& r) override
        {
            Rect area = app.contentArea();

            app.hint("A", m_figures.empty() ? "-" : "look at them");
            if (!m_figures.empty())
                app.hint("X", "shuffle");

            drawGround(r, area);
            drawHeader(r, area);

            if (m_figures.empty())
                return;

            // Painter's algorithm: furthest first, so a nearer figure covers the
            // one behind it. Sorting indices keeps the figures themselves put,
            // which matters because the focus is an index into them.
            m_order.resize(m_figures.size());
            for (size_t i = 0; i < m_order.size(); i++)
                m_order[i] = i;
            std::sort(m_order.begin(), m_order.end(), [this](size_t a, size_t b) {
                return m_figures[a].z > m_figures[b].z;
            });

            for (size_t index : m_order)
                drawFigure(app, r, m_figures[index], index, area);

            drawBubble(r, area);
        }

    private:
        // ------------------------------------------------------------- cast

        void populate(App& app)
        {
            Rect area = app.contentArea();
            m_figures.clear();

            // The console's own Mii, always, and first: a square with nobody in
            // it is a broken-looking screen, and on a new console that is
            // exactly what the collection would give us.
            Pass mine = app.store().myPass();
            Figure self;
            self.mii = mine.face();
            self.name = mine.handle.empty() ? std::string("You") : mine.handle;
            self.greeting = mine.greeting;
            place(self, area);
            m_figures.push_back(std::move(self));

            std::vector<Crossing> crossings = app.store().crossings();

            // Reservoir sampling: one pass, no shuffle of the whole collection,
            // and it does not care that the collection may be five thousand
            // long.
            std::vector<const Crossing*> chosen;
            size_t wanted = kMaxFigures - 1;
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
                Figure f;
                f.mii = c->pass.face();
                f.name = c->pass.handle;
                f.greeting = c->pass.greeting;
                f.crossingId = c->id;
                place(f, area);
                m_figures.push_back(std::move(f));
            }

            m_focus = 0;
            m_bubble = -1;
            m_bubbleAge = 0.0f;
        }

        // The corner the header owns, in screen space. Both the text and the
        // constraint on the crowd come from this, so they cannot disagree.
        static Rect headerBox(const Rect& area)
        {
            return Rect { area.x + theme::s9, area.y + theme::s7, kHeaderWidth,
                kHeaderBottom - theme::s7 };
        }

        // The furthest a figure may stand and still keep its head below the
        // header. Depth is what decides height on screen: further away is
        // higher up, so clearing the text is a ceiling on z, not on x.
        static float depthClearingHeader(const Rect& area)
        {
            // A few pixels of margin: at exactly the limit a head grazes the
            // last line of text, and the bob is enough to touch it.
            float drop = kHeaderBottom + 10.0f - area.h * kHorizonAt;
            if (drop <= 1.0f)
                return kFarZ;
            return (kCameraHeight - kPersonHeight) * kFocal / drop;
        }

        // Screen x depends only on how far off centre a figure stands as a
        // fraction of the room it has, never on its depth: the two cancel. That
        // is what lets the constraint be decided once, from u alone.
        static float screenXFor(float u, const Rect& area)
        {
            return area.centerX() + u * kSpread * kFocal;
        }

        // How far back this lateral position may go. Only the header's own
        // corner is limited; the rest of the square keeps its full depth.
        static float depthLimitFor(float u, const Rect& area)
        {
            Rect header = headerBox(area);
            float x = screenXFor(u, area);
            if (x > header.right() + kFigureHalfWidth)
                return kFarZ;
            return std::min(kFarZ, depthClearingHeader(area));
        }

        // Half the width available at this depth. Everything that places or
        // moves a figure goes through this, so nobody is ever put somewhere the
        // projection would throw off screen.
        static float halfWidthAt(float z) { return z * kSpread; }

        // A depth that spreads the crowd evenly down the screen.
        //
        // Screen height goes as 1/z, so picking z evenly bunches almost
        // everybody into the far half of the stage and leaves the foreground
        // empty. Picking 1/z evenly instead puts as many people in the front of
        // the square as the back, which is what actually uses the space.
        static float pickDepth(const Rect& area, float u)
        {
            float far_ = std::min(kFarZ, depthLimitFor(u, area));
            float lo = 1.0f / far_;
            float hi = 1.0f / kNearZ;
            return 1.0f / randomRange(lo, hi);
        }

        void place(Figure& f, const Rect& area)
        {
            // Evenly across the width. A bias towards the middle looked right
            // in the abstract and in practice piled everyone into a knot in the
            // centre with the sides of the square empty.
            float u = randomRange(-1.0f, 1.0f);
            f.z = pickDepth(area, u);
            f.x = u * halfWidthAt(f.z);
            f.targetX = f.x;
            f.targetZ = f.z;
            f.bob = randomUnit() * 6.2831853f;
        }

        // --------------------------------------------------------- movement

        void wander(const Rect& area, float dt)
        {
            for (Figure& f : m_figures) {
                float dx = f.targetX - f.x;
                float dz = f.targetZ - f.z;
                float distance = std::sqrt(dx * dx + dz * dz);

                if (distance < 0.20f) {
                    // Somewhere else, picked the same way the first spot was so
                    // the crowd keeps filling the square rather than drifting
                    // into whichever corner the wander happens to favour.
                    float u = randomRange(-1.0f, 1.0f);
                    f.targetZ = pickDepth(area, u);
                    f.targetX = u * halfWidthAt(f.targetZ);
                    continue;
                }

                float step = kSpeed * dt / distance;
                f.x += dx * step;
                f.z += dz * step;
            }

            separate();
            clearHeader(area);
        }

        // Brings anyone whose head has risen into the header nearer the camera,
        // which moves them down the screen and out of it. Eased rather than
        // snapped, and only in the corner the text occupies.
        void clearHeader(const Rect& area)
        {
            float limit = depthClearingHeader(area);
            for (Figure& f : m_figures) {
                float u = f.x / std::max(0.001f, f.z * kSpread);
                if (f.z <= depthLimitFor(u, area))
                    continue;

                // Keeping x/z fixed keeps the figure where it is across the
                // screen; only its depth changes, so it walks towards you
                // rather than sideways.
                float wanted = std::max(limit, kNearZ);
                float ratio = wanted / f.z;
                f.x *= ratio;
                f.z = wanted;
                f.targetZ = std::min(f.targetZ, wanted);
            }
        }

        // Pushes apart anyone standing in the same spot.
        //
        // Three passes, not one. A single pass leaves a pair that were pushed
        // into a third person still overlapping, and with twenty figures on a
        // stage this size that reads as a knot of people in one spot with empty
        // floor around them. Three is enough to settle a cluster and still far
        // short of a physics simulation.
        void separate()
        {
            for (int pass = 0; pass < 3; pass++)
                separateOnce();
        }

        void separateOnce()
        {
            for (size_t i = 0; i < m_figures.size(); i++) {
                for (size_t j = i + 1; j < m_figures.size(); j++) {
                    Figure& a = m_figures[i];
                    Figure& b = m_figures[j];
                    float dx = b.x - a.x;
                    float dz = b.z - a.z;
                    float d2 = dx * dx + dz * dz;
                    if (d2 >= kSeparation * kSeparation || d2 < 0.0001f)
                        continue;

                    float d = std::sqrt(d2);
                    float push = (kSeparation - d) * 0.5f;
                    dx /= d;
                    dz /= d;
                    a.x -= dx * push;
                    a.z -= dz * push;
                    b.x += dx * push;
                    b.z += dz * push;
                }
            }

            for (Figure& f : m_figures) {
                f.z = std::min(std::max(f.z, kNearZ), kFarZ);
                float limit = halfWidthAt(f.z);
                f.x = std::min(std::max(f.x, -limit), limit);
            }
        }

        // ------------------------------------------------------- projection

        struct Projected {
            Rect box;
            float scale = 0.0f;
        };

        // Screen y of the floor at this depth. Everything that touches the
        // ground goes through it, so a figure's feet and the tile it stands on
        // cannot drift apart.
        static float floorY(float z, const Rect& area)
        {
            return area.y + area.h * kHorizonAt + kCameraHeight * (kFocal / std::max(z, 0.5f));
        }

        Projected project(const Figure& f, const Rect& area) const
        {
            Projected out;
            float z = std::max(f.z, 0.5f);
            out.scale = kFocal / z;

            float height = kPersonHeight * out.scale;
            float width = height * 0.62f;

            // A little vertical bob, so a standing crowd is not a still image.
            float bob = std::sin(m_time * 1.7f + f.bob) * height * 0.012f;

            float cx = area.centerX() + f.x * out.scale;
            float feet = floorY(f.z, area) + bob;

            out.box = Rect { cx - width * 0.5f, feet - height, width, height };
            return out;
        }

        // ---------------------------------------------------------- drawing

        // The floor, tiled and receding.
        // The vanishing point is above the frame, so the ground is the whole picture.
        //
        // Tiles are constant in world space, which is what makes them converge
        // and is the only reason the flat colour underneath reads as a surface
        // going away from you. Drawn as a checkerboard, so only half of them
        // are quads at all.
        void drawGround(Renderer& r, const Rect& area)
        {
            r.rect(area, theme::bg1);

            // Alternating tiles, barely there. Any more contrast and the floor
            // competes with the people standing on it.
            Color light = theme::bg2.scaleAlpha(0.55f);

            int row = 0;
            for (float z = kFloorNear; z < kFloorFar; z += kTile, row++) {
                float zNear = z;
                float zFar = z + kTile;
                float yNear = floorY(zNear, area);
                float yFar = floorY(zFar, area);

                // Rows above the frame are behind the camera's interest.
                if (yNear < area.y)
                    break;
                if (yFar > area.bottom())
                    continue;

                float scaleNear = kFocal / zNear;
                float scaleFar = kFocal / zFar;

                // How far sideways this row has to run to reach both edges.
                float halfWorld = (area.w * 0.5f) / scaleFar + kTile;
                int cells = std::min(int(halfWorld / kTile) * 2 + 2, kMaxCellsPerRow);

                for (int i = -cells / 2; i < cells / 2; i++) {
                    if (((i + row) & 1) == 0)
                        continue; // the dark half is the flat colour underneath

                    float x0 = float(i) * kTile;
                    float x1 = x0 + kTile;

                    // A tile is a trapezoid: its far edge is nearer the middle
                    // of the screen and shorter than its near edge.
                    float nx0 = area.centerX() + x0 * scaleNear;
                    float nx1 = area.centerX() + x1 * scaleNear;
                    float fx0 = area.centerX() + x0 * scaleFar;
                    float fx1 = area.centerX() + x1 * scaleFar;

                    if (std::max(nx1, fx1) < area.x || std::min(nx0, fx0) > area.right())
                        continue;

                    const float corners[8] = { fx0, yFar, fx1, yFar, nx1, yNear, nx0, yNear };
                    r.band(corners, light);
                }
            }

            // Distance haze. The floor loses contrast as it goes away, which is
            // what stops the tiles turning into a moire at the top of the frame.
            r.gradientRect(Rect { area.x, area.y, area.w, area.h * 0.55f },
                theme::bg0, theme::bg0.scaleAlpha(0.0f));
        }

        void drawHeader(Renderer& r, const Rect& area)
        {
            // No headline here, unlike every other tab. The floor is the whole
            // picture on this screen and a display-sized line of text sits on
            // top of it rather than in it.
            Rect box { area.x + theme::s9, area.y + theme::s7, area.w * 0.5f, 0.0f };
            ui::eyebrow(r, Rect { box.x, box.y, box.w, 34.0f }, "the square");

            TextStyle body;
            body.size = theme::textBase;
            body.color = theme::fg3;
            body.leading = theme::leadingNormal;
            size_t others = m_figures.empty() ? 0 : m_figures.size() - 1;
            std::string sub = others == 0
                ? std::string("Everyone you cross paths with turns up here.")
                : std::string("A few of the people you have met, milling about.");
            r.textWrapped(Rect { box.x, box.y + 40.0f, box.w, 80.0f }, sub, body, 2);
        }

        void drawFigure(App& app, Renderer& r, const Figure& f, size_t index, const Rect& area)
        {
            Projected p = project(f, area);
            if (p.box.h < 8.0f || p.box.bottom() < area.y || p.box.y > area.bottom())
                return;

            // Distance is told by size and by the haze on the floor.
            float depth = std::min(1.0f, (f.z - kNearZ) / (kFarZ - kNearZ));

            // The contact shadow does more for the grounding than anything else
            // on this screen, and costs one quad. It does soften with distance,
            // because a shadow genuinely is fainter under the haze.
            float shadow = p.box.w * 0.42f;
            r.ellipse(p.box.centerX(), p.box.bottom(), shadow, shadow * 0.26f,
                theme::fg1.scaleAlpha(0.13f * (1.0f - depth * 0.4f)));

            bool focused = index == m_focus;
            if (focused)
                drawFocusRim(r, p.box, f.mii);

            // Deliberately no touch zone. A figure is a small moving target on a
            // crowded floor, and the boxes a tap is tested against are last
            // frame's - so a tap that looked like it landed on somebody
            // regularly opened whoever had wandered into that spot instead. The
            // cursor and A are the way in here.

            // The same figure at every depth, just smaller. Opaque, so nobody
            // is a ghost at the back of the square.
            ui::miiFigure(r, p.box, f.mii);
        }

        // The rim that says who the cursor is on: the person's own outline, in
        // the accent colour, pulsing.
        void drawFocusRim(Renderer& r, const Rect& box, const Mii& mii)
        {
            float rim = std::max(3.0f, box.w * 0.05f);

            // Opaque, and the pulse is in the colour rather than the alpha.
            // Five copies overlap, and semi-transparent ones stack: at 0.55
            // alpha a single copy on the flank reads 0.55 while three at a
            // corner read 0.91, so the rim came out patchy round the curves.
            // Opaque copies stack to exactly themselves.
            Color ink = theme::accent.mix(theme::accentSoft, m_pulse * 0.6f);
            ink.a = 1.0f;

            // No downward copy: below the feet is where the contact shadow is,
            // and a rim there reads as the figure floating.
            const float dx[3] = { -rim, rim, 0.0f };
            const float dy[3] = { 0.0f, 0.0f, -rim };
            for (int i = 0; i < 3; i++)
                ui::miiSilhouette(r, box.offset(dx[i], dy[i]), mii, ink);

            // The diagonals fill the corners the four cardinals leave notched,
            // at a shorter reach so the rim stays an even thickness round a
            // curve rather than bulging at forty-five degrees.
            float d = rim * 0.7f;
            const float ex[2] = { -d, d };
            for (int i = 0; i < 2; i++)
                ui::miiSilhouette(r, box.offset(ex[i], -d), mii, ink);
        }

        void pickBubble(float dt)
        {
            m_bubbleAge += dt;
            if (m_bubble >= 0 && m_bubbleAge > kBubbleSeconds) {
                m_bubble = -1;
                m_bubbleAge = 0.0f;
                return;
            }
            if (m_bubble < 0 && m_bubbleAge > kBubbleGap) {
                // Somebody with something to say. A pass with no greeting stays
                // quiet rather than showing an empty bubble.
                std::vector<size_t> talkers;
                for (size_t i = 0; i < m_figures.size(); i++) {
                    if (!m_figures[i].greeting.empty())
                        talkers.push_back(i);
                }
                if (talkers.empty())
                    return;
                uint32_t bits = 0;
                randomBytes(&bits, sizeof(bits));
                m_bubble = static_cast<int>(talkers[bits % talkers.size()]);
                m_bubbleAge = 0.0f;
            }
        }

        void drawBubble(Renderer& r, const Rect& area)
        {
            if (m_bubble < 0 || size_t(m_bubble) >= m_figures.size())
                return;

            const Figure& f = m_figures[size_t(m_bubble)];
            Projected p = project(f, area);
            if (p.box.h < kBubbleMinFigure)
                return; // too far away to tell who is speaking

            TextStyle text;
            text.size = theme::textSm;
            text.color = theme::fg1;

            // Sized to the text, not assumed. A greeting runs to sixty
            // characters, which is well past one line at this width, and the
            // height used to be a single line whatever it held - so a long one
            // printed out through the bottom of its own bubble.
            std::string line = f.greeting;
            float inner = kBubbleMaxWidth - theme::s5 * 2.0f;
            float measured = r.measure(line, text);

            float width;
            float textHeight;
            if (measured <= inner) {
                width = measured + theme::s5 * 2.0f;
                textHeight = r.lineHeight(text);
            } else {
                width = kBubbleMaxWidth;
                textHeight = r.measureWrapped(inner, line, text, 0);
            }
            float height = textHeight + theme::s4 * 2.0f;

            // Fades in and out rather than snapping, and is nudged back on
            // screen when the speaker is near an edge.
            float alpha = std::min(1.0f, std::min(m_bubbleAge, kBubbleSeconds - m_bubbleAge) * 3.0f);
            float x = std::min(std::max(p.box.centerX() - width * 0.5f, area.x + theme::s6),
                area.right() - width - theme::s6);
            // Held clear of the top of the stage. A tall bubble over a speaker
            // already high in the frame would otherwise print off the screen,
            // and the fix has to be here rather than in the wrapping: the text
            // is what has to fit, so it is the bubble that gives way.
            float top = std::max(p.box.y - height - kTailHeight, area.y + theme::s5);
            Rect box { x, top, width, height };

            Color skin = theme::bg2.scaleAlpha(0.96f * alpha);
            r.roundRect(box, theme::r3, skin);

            // The tail, and the reason it is worth the arithmetic: the bubble
            // is nudged sideways to stay on screen, so above a speaker near an
            // edge it no longer sits over them and there is nothing to say who
            // is talking. The apex tracks the speaker even when the body of the
            // bubble cannot, which is what makes it lean towards them.
            float apexX = std::min(std::max(p.box.centerX(), box.x + theme::r3),
                box.right() - theme::r3);
            float half = kTailWidth * 0.5f;
            // The base stays on the bubble's flat edge, clear of both corners.
            float baseX = std::min(std::max(apexX, box.x + theme::r3 + half),
                box.right() - theme::r3 - half);
            // Stretches to reach the head rather than being a fixed length, so
            // a bubble pushed down by a long greeting - or up by the clamp
            // above - still ends on the person it belongs to.
            float apexY = std::max(p.box.y, box.bottom() + 10.0f);
            r.trapezoid(box.bottom() - 1.0f, baseX - half, baseX + half, apexY, apexX, apexX,
                skin);

            // Wrapped into the padded interior. Centred, because a bubble with
            // one short line in it looks wrong ragged-left.
            r.textWrapped(Rect { box.x + theme::s5, box.y + theme::s4, box.w - theme::s5 * 2.0f,
                              textHeight },
                line, text, 0, Align::Center);
        }

        // ------------------------------------------------------------ input

        size_t ownIndex() const { return 0; }

        void stepFocus(int delta)
        {
            if (m_figures.empty())
                return;
            size_t count = m_figures.size();
            m_focus = (m_focus + size_t(int(count) + delta)) % count;
        }

        void open(App& app)
        {
            if (m_focus >= m_figures.size())
                return;
            const Figure& f = m_figures[m_focus];
            if (f.crossingId.empty()) {
                // That is us. The pass screen is where our own face is edited.
                app.setTab(Tab::Passport);
                return;
            }
            // Handing over the siblings gives L and R inside the card, so the
            // crowd can be walked through without coming back out here. In the
            // order they stand, left to right, which is the order they are on
            // screen rather than the order they are in memory.
            std::vector<size_t> byScreen(m_figures.size());
            for (size_t i = 0; i < byScreen.size(); i++)
                byScreen[i] = i;
            std::sort(byScreen.begin(), byScreen.end(), [this](size_t a, size_t b) {
                return m_figures[a].x / m_figures[a].z < m_figures[b].x / m_figures[b].z;
            });

            std::vector<std::string> siblings;
            siblings.reserve(m_figures.size());
            for (size_t i : byScreen) {
                if (!m_figures[i].crossingId.empty())
                    siblings.push_back(m_figures[i].crossingId);
            }
            app.openEncounter(f.crossingId, std::move(siblings));
        }

            // Deliberately no touch zone. A figure is a small moving target on a
            // crowded floor, and the boxes a tap is tested against are last
            // frame's - so a tap that looked like it landed on somebody
            // regularly opened whoever had wandered into that spot instead. The
            // cursor and A are the way in here.

            // The same figure at every depth, just smaller. Opaque, so nobody
            // is a ghost at the back of the square.        std::vector<Figure> m_figures;
        std::vector<size_t> m_order;
        uint64_t m_generation = 0;
        size_t m_focus = 0;
        int m_bubble = -1;
        float m_bubbleAge = 0.0f;
        float m_pulse = 0.0f;
        float m_time = 0.0f;
    };
}

std::unique_ptr<Scene> makeSquareScene() { return std::make_unique<SquareScene>(); }

} // namespace nxp
