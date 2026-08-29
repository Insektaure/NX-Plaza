#include "app.h"
#include "core/place.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

namespace nxp {

namespace {

// Who is awake in the same place.
//
// The radar is a 1120px circle at left:-60 top:80 - it deliberately bleeds off
// the left edge and under the rail - with four rings, the lantern at its
// centre standing for this console, and the other consoles as dots on it. The
// text and the list live in a 660px column pinned to the right.
class NearbyScene final : public Scene {
public:
    enum Zone : int {
        Zone_Peer = Touch_SceneBase,
        Zone_Dot,
        Zone_AutoTrade,
    };

    void update(App& app, const Input& input, float dt) override
    {
        m_peers = app.sync().peers();
        m_status = app.sync().status();
        m_sweep += dt * 0.4f;
        if (m_sweep > 1.0f)
            m_sweep -= 1.0f;
        m_pulse = 0.5f + 0.5f * std::sin(app.time() * 3.0f);

        // One selection covers both halves of the screen: a peer is highlighted
        // on the radar and, when it is one of the few the list has room for, in
        // the list too. The cursor runs through everyone; the list is a window
        // that follows it rather than a viewport you scroll.
        int peerCount = static_cast<int>(m_peers.size());
        m_selected = std::min(std::max(m_selected, 0), std::max(peerCount - 1, 0));
        if (peerCount == 0)
            m_onToggle = true;

        TouchTarget tap;
        if (app.takeTap(tap)) {
            if ((tap.is(Zone_Peer) || tap.is(Zone_Dot)) && tap.index < peerCount) {
                m_onToggle = false;
                m_selected = tap.index;
                app.pushOverlay(makePeerScene(m_peers[static_cast<size_t>(tap.index)]));
                return;
            }
            if (tap.is(Zone_AutoTrade)) {
                m_onToggle = true;
                toggleAuto(app);
                return;
            }
        }

        if (input.navDown) {
            if (m_onToggle) {
                // nowhere below the toggle
            } else if (m_selected + 1 < peerCount) {
                m_selected++;
            } else {
                m_onToggle = true;
            }
        }
        if (input.navUp) {
            if (m_onToggle) {
                m_onToggle = peerCount == 0;
                m_selected = std::max(peerCount - 1, 0);
            } else {
                m_selected = std::max(m_selected - 1, 0);
            }
        }

        if (input.accept()) {
            if (m_onToggle) {
                toggleAuto(app);
            } else if (peerCount > 0) {
                app.pushOverlay(makePeerScene(m_peers[static_cast<size_t>(m_selected)]));
                return;
            }
        }

        if (input.pressed(HidNpadButton_X))
            app.sync().kick();
    }

    void draw(App& app, Renderer& r) override
    {
        Rect area = app.contentArea();

        // "position:absolute;left:-60px;top:80px;width:1120px;height:1120px"
        Rect radar { area.x - 60.0f, 80.0f, 1120.0f, 1120.0f };
        drawRadar(app, r, radar);

        // "position:absolute;right:64px;top:48px;width:660px"
        Rect column { area.right() - theme::edge - kColumnWidth, area.y + theme::edgeTop,
            kColumnWidth, area.h - theme::edgeTop * 2.0f };
        drawColumn(app, r, column);
    }

private:
    static constexpr float kColumnWidth = 660.0f;
    static constexpr float kRowHeight = 124.0f; // 76px avatar on --space-4 padding
    static constexpr float kRowPitch = kRowHeight + theme::s3;
    static constexpr float kVisibleRows = 3.0f;

    // Which peer the list starts at, so the window keeps the selected one in
    // view without the list ever becoming something you scroll.
    int windowStart() const
    {
        int count = static_cast<int>(m_peers.size());
        int span = static_cast<int>(kVisibleRows);
        if (count <= span)
            return 0;
        int anchor = m_onToggle ? count - 1 : m_selected;
        return std::min(std::max(anchor - 1, 0), count - span);
    }
    static constexpr float kAvatar = 76.0f;

    void toggleAuto(App& app)
    {
        Settings settings = app.store().settings();
        settings.autoExchange = !settings.autoExchange;
        app.store().setSettings(settings);
        app.store().flush();
        app.sync().kick();
    }

    // Four rings as hairlines, the innermost tinted teal, the lantern at the
    // centre, and a dot per console.
    void drawRadar(App& app, Renderer& r, const Rect& box)
    {
        float cx = box.centerX();
        float cy = box.centerY();

        struct Ring {
            float inset;
            Color color;
        };
        const Ring rings[4] = {
            { 0.0f, theme::fg1.withAlpha(0.05f) },
            { 150.0f, theme::fg1.withAlpha(0.06f) },
            { 300.0f, theme::fg1.withAlpha(0.07f) },
            { 440.0f, theme::teal.withAlpha(0.22f) },
        };

        for (const Ring& ring : rings) {
            float radius = box.w * 0.5f - ring.inset;
            Rect circle { cx - radius, cy - radius, radius * 2.0f, radius * 2.0f };
            if (ring.inset >= 440.0f) {
                // radial-gradient(circle, rgba(74,211,200,.1), transparent 70%)
                r.glow(circle, theme::teal.withAlpha(0.10f), 1.4f);
            }
            r.strokeRect(circle, radius, theme::stroke, ring.color);
        }

        // A slow sweep outward, so the screen reads as listening.
        float sweepRadius = (box.w * 0.5f - 440.0f) + m_sweep * 440.0f;
        r.strokeRect(Rect { cx - sweepRadius, cy - sweepRadius, sweepRadius * 2.0f,
                        sweepRadius * 2.0f },
            sweepRadius, theme::stroke, theme::teal.withAlpha(0.18f * (1.0f - m_sweep)));

        // "width:96px;height:96px;box-shadow:0 0 60px" - this console.
        r.glow(Rect { cx - 60.0f, cy - 60.0f, 120.0f, 120.0f },
            theme::accentGlow.scaleAlpha(0.5f), 1.6f);
        r.circle(cx, cy, 48.0f, theme::mark);
        r.circle(cx - 10.0f, cy - 12.0f, 18.0f, theme::accentSoft);

        // The artboard places four dots by hand; here the angle comes from the
        // handle and the distance from the closeness bucket, so a peer keeps its
        // spot between frames.
        for (size_t i = 0; i < m_peers.size(); i++) {
            const Peer& peer = m_peers[i];
            float bucket = static_cast<float>(std::min(peer.closeness, 2));
            float ringRadius = (box.w * 0.5f - 440.0f) + (bucket + 0.5f) * 150.0f;

            float spread = static_cast<float>(fnv1a(peer.handle) % 1000) / 1000.0f;
            float angle = 6.2831853f * spread;
            float x = cx + std::cos(angle) * ringRadius;
            float y = cy + std::sin(angle) * ringRadius * 0.92f;

            float size = 64.0f - bucket * 8.0f;
            bool close = peer.closeness == 0;
            bool focused = static_cast<int>(i) == m_selected && !m_onToggle;

            // The dot is the reachable thing: everyone is on the radar, and only
            // the nearest few fit in the list beside it.
            Rect dot { x - size * 0.5f, y - size * 0.5f, size, size };
            app.touchZone(dot.inset(-10.0f), Zone_Dot, static_cast<int>(i));

            float held = app.touchHeld(Zone_Dot, static_cast<int>(i)) ? 1.0f : 0.0f;
            float lift = std::max(held, focused ? 0.7f + 0.3f * m_pulse : 0.0f);

            r.circle(x, y, size * 0.5f, theme::bg4);
            ui::miiHead(r, Rect { x - size * 0.42f, y - size * 0.44f, size * 0.84f,
                            size * 0.88f },
                peer.face());
            if (close)
                r.strokeRect(dot, size * 0.5f, theme::stroke, theme::teal.withAlpha(0.5f));
            ui::focusRing(r, dot, size * 0.5f, lift);

            TextStyle label;
            label.size = theme::textXs;
            label.color = focused ? theme::fg1 : (close ? theme::fg2 : theme::fg3);
            r.text(Rect { x - 120.0f, y + size * 0.5f + 10.0f, 240.0f, 40.0f },
                r.ellipsize(peer.handle, label, 240.0f), label, Align::Center, VAlign::Top);
        }
    }

    // The right column: a live eyebrow, the count, the explanation, the list.
    void drawColumn(App& app, Renderer& r, const Rect& box)
    {
        // A opens a peer, or works the toggle at the end of the list.
        app.hint("A", m_onToggle ? "toggle" : "open");
        app.hint("X", "look now");

        const char* eyebrowText = "live - scanning";
        Color eyebrowColor = theme::teal;
        switch (m_status.state) {
        case Sync::State::Offline:
            eyebrowText = "offline";
            eyebrowColor = theme::fg3;
            break;
        case Sync::State::Error:
            eyebrowText = "cannot reach the plaza";
            eyebrowColor = theme::danger;
            break;
        case Sync::State::Working:
            eyebrowText = "live - trading";
            break;
        default:
            break;
        }

        // A 14px dot with its own bloom, then the label.
        float dotY = box.y + theme::textSm * theme::leadingNormal * 0.5f;
        r.glow(Rect { box.x - 8.0f, dotY - 15.0f, 30.0f, 30.0f },
            eyebrowColor.withAlpha(0.55f), 1.6f);
        r.circle(box.x + 7.0f, dotY, 7.0f, eyebrowColor);

        TextStyle eyebrow;
        eyebrow.size = theme::textSm;
        eyebrow.weight = FontWeight::Bold;
        eyebrow.color = eyebrowColor;
        eyebrow.tracking = theme::trackingWider;
        eyebrow.uppercase = true;
        r.text(box.x + 14.0f + theme::s3, box.y, eyebrowText, eyebrow);

        TextStyle title;
        title.size = theme::text2xl;
        title.weight = FontWeight::Bold;
        title.color = theme::fg1;
        title.tracking = theme::trackingTight;
        title.leading = theme::leadingTight;

        int count = static_cast<int>(m_peers.size());
        std::string headline = count == 1
            ? std::string("1 console awake near you")
            : format("%d consoles awake near you", count);

        float titleY = box.y + theme::textSm * theme::leadingNormal + theme::s3;
        float titleUsed = r.textWrapped(Rect { box.x, titleY, box.w, title.size * 2.3f },
            headline, title, 2);

        TextStyle body;
        body.size = theme::textBase;
        body.color = theme::fg2;
        body.leading = theme::leadingNormal;
        std::string explainer = m_status.message.empty()
            ? std::string("Passes exchange on their own. You do not have to sit here.")
            : m_status.message;
        float bodyY = titleY + titleUsed + theme::s3;
        float bodyUsed = r.textWrapped(Rect { box.x, bodyY, box.w, 130.0f }, explainer, body, 3);

        // The auto-trade row is pinned to the bottom; the peers scroll between
        // it and the explanation, so a crowded network is all reachable.
        Rect autoRow { box.x, box.bottom() - kRowHeight, box.w, kRowHeight };
        float listTop = bodyY + bodyUsed + theme::s5;

        // Three at a time. Filling the column with as many rows as fit turns the
        // panel into a wall of cards and buries the radar it is describing; the
        // rest of the network is a scroll away, and all of it is on the radar
        // already.
        float room = std::max(autoRow.y - theme::s5 - listTop, kRowHeight);
        Rect list { box.x, listTop, box.w,
            std::min(room, kVisibleRows * kRowPitch - theme::s3) };

        // A focused row grows and draws a ring outside itself. The rows are held
        // in from the column edges by exactly that much, so the lift happens
        // inside the list instead of reaching over the radar beside it.
        float lift = box.w * theme::focusGrow + theme::focusGap + theme::focusRing;
        Rect rows { box.x + lift, list.y, box.w - lift * 2.0f, list.h };

        float y = list.y;
        int first = windowStart();
        int last = std::min(first + static_cast<int>(kVisibleRows),
            static_cast<int>(m_peers.size()));
        for (int i = first; i < last; i++) {
            drawPeerRow(app, r, Rect { rows.x, y, rows.w, kRowHeight },
                m_peers[static_cast<size_t>(i)], i);
            y += kRowPitch;
        }

        if (m_peers.empty()) {
            TextStyle empty;
            empty.size = theme::textBase;
            empty.color = theme::fg3;
            r.textWrapped(Rect { rows.x, list.y, rows.w, 120.0f },
                "Nobody else has checked in here yet. Leave the app open; the plaza fills "
                "faster if you do.",
                empty, 3);
        }

        drawAutoRow(app, r, Rect { rows.x, autoRow.y, rows.w, autoRow.h });


    }

    void drawPeerRow(App& app, Renderer& r, const Rect& box, const Peer& peer, int index)
    {
        app.touchZone(box, Zone_Peer, index);

        bool focused = index == m_selected && !m_onToggle;
        float focus = app.touchHeld(Zone_Peer, index)
            ? 1.0f
            : (focused ? 0.7f + 0.3f * m_pulse : 0.0f);
        ui::card(r, box, focus, focused ? theme::bg2 : theme::bg1, theme::r3);

        Rect inner = box.inset(theme::s5, theme::s4);

        // "width:76px;height:76px;border-radius:radius-2" - a cropped portrait.
        Rect avatar { inner.x, inner.centerY() - kAvatar * 0.5f, kAvatar, kAvatar };
        r.pushClip(avatar);
        ui::miiStage(r, avatar, peer.face(), 0,
            ui::StageFigure { 0.62f, 0.66f, 0.38f, 0.52f, 0.0f, 0.17f }, theme::r2, theme::r2);
        r.popClip();

        TextStyle name;
        name.size = theme::textBase;
        name.weight = FontWeight::Bold;
        name.color = theme::fg1;

        TextStyle meta;
        meta.size = theme::textSm;
        meta.color = theme::fg3;

        const char* stateText = "waiting";
        Color stateColor = theme::fg3;
        bool pill = false;
        switch (peer.state) {
        case Peer::State_Exchanging:
            stateText = "exchanging...";
            stateColor = theme::fg3;
            break;
        case Peer::State_Passed:
            stateText = "passed";
            stateColor = theme::teal;
            pill = true;
            break;
        case Peer::State_OutOfRange:
            stateText = "out of range";
            stateColor = theme::fg4;
            break;
        default:
            break;
        }

        TextStyle state;
        state.size = pill ? theme::textXs : theme::textSm;
        state.weight = FontWeight::Bold;
        state.color = stateColor;
        state.uppercase = pill;
        state.tracking = pill ? theme::trackingWide : 0.0f;

        float stateWidth = r.measure(stateText, state) + (pill ? 32.0f : 0.0f);
        if (pill) {
            Rect badge { inner.right() - stateWidth, inner.centerY() - 20.0f, stateWidth,
                40.0f };
            ui::pill(r, badge, stateText, stateColor, theme::tealTint, theme::textXs);
        } else {
            r.text(Rect { inner.right() - stateWidth, inner.y, stateWidth, inner.h }, stateText,
                state, Align::Left, VAlign::Middle);
        }

        float textX = avatar.right() + theme::s5;
        float textW = inner.right() - stateWidth - theme::s5 - textX;
        float nameY = inner.centerY() - (name.size * theme::leadingSnug + 4.0f
                          + meta.size * theme::leadingNormal) * 0.5f;
        r.text(textX, nameY, r.ellipsize(peer.handle, name, textW), name);

        std::string detail = peer.playing.empty() ? std::string("hidden title") : peer.playing;
        detail += " - " + peer.proximityLabel();
        r.text(textX, nameY + name.size * theme::leadingSnug + 4.0f,
            r.ellipsize(detail, meta, textW), meta);
    }

    void drawAutoRow(App& app, Renderer& r, const Rect& box)
    {
        app.touchZone(box, Zone_AutoTrade);

        Settings settings = app.store().settings();
        bool focused = m_onToggle;
        float focus = app.touchHeld(Zone_AutoTrade)
            ? 1.0f
            : (focused ? 0.7f + 0.3f * m_pulse : 0.0f);

        ui::card(r, box, focus, focused ? theme::bg2 : theme::bg1, theme::r3);
        Rect inner = box.inset(theme::s6, theme::s5);

        TextStyle label;
        label.size = theme::textBase;
        label.weight = FontWeight::Bold;
        label.color = theme::fg1;
        r.text(inner.x, inner.y, "Exchange passes automatically", label);

        TextStyle hint;
        hint.size = theme::textSm;
        hint.color = theme::fg3;
        std::string detail = format("Up to %d per day while the app is open",
            settings.dailyLimit);
        if (!m_status.networkName.empty()) {
            detail += m_status.placeKnown
                ? format(" - matching on \"%s\"", m_status.networkName.c_str())
                : format(" - on \"%s\", no name to match", m_status.networkName.c_str());
        }
        r.text(inner.x, inner.y + label.size * theme::leadingSnug + 6.0f,
            r.ellipsize(detail, hint, inner.w * 0.66f), hint);

        ui::toggle(r, inner, settings.autoExchange, focus);
    }

    std::vector<Peer> m_peers;
    Sync::Status m_status;
    int m_selected = 0;
    // The auto-trade row has the cursor, rather than one of the peers.
    bool m_onToggle = false;
    float m_sweep = 0.0f;
    float m_pulse = 0.0f;
};

} // namespace

std::unique_ptr<Scene> makeNearbyScene()
{
    return std::unique_ptr<Scene>(new NearbyScene());
}

} // namespace nxp
