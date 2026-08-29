#include "app.h"
#include "core/util.h"
#include "scenes/scene.h"
#include "ui/mii_render.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace nxp {

namespace {

// One console on the radar, close up.
//
// A peer is not a crossing: nothing has been traded yet and there is no button
// that would make it happen sooner, so this view has nothing to press. It
// exists because the list rows are small, the faces are the point, and "who is
// actually out there" is the question the Nearby screen is answering.
class PeerScene final : public Scene {
public:
    explicit PeerScene(const Peer& peer)
        : m_peer(peer)
    {
    }

    void update(App& app, const Input& input, float) override
    {
        // Keeping a copy of a face is the one thing this screen can do: a peer
        // is not a crossing, so there is nothing here to trade or to change.
        if (input.pressed(HidNpadButton_Y)) {
            promptSaveMii(app, m_peer.face(), m_peer.handle);
            return;
        }

        // Nothing else here changes anything, so every way out is the way out.
        if (input.back() || input.accept()) {
            app.popOverlay();
            return;
        }

        // Taps land on the card's own zone and do nothing; the app dismisses an
        // overlay when a touch misses every zone it declares. Taking the tap is
        // what stops it carrying over to the next frame.
        TouchTarget tap;
        app.takeTap(tap);
    }

    void draw(App& app, Renderer& r) override
    {
        app.hint("B", "close");
        app.hint("Y", "save face");

        constexpr float kWidth = 1000.0f;
        constexpr float kHeight = 620.0f;
        Rect box { Renderer::DesignWidth * 0.5f - kWidth * 0.5f,
            Renderer::DesignHeight * 0.5f - kHeight * 0.5f - 20.0f, kWidth, kHeight };

        // Its own zone, so a touch on the card does not fall through to the
        // dismiss zone the app puts behind every overlay.
        app.touchZone(box, Zone_Card);

        r.roundRect(box, theme::r5, theme::bg1);
        r.strokeRect(box, theme::r5, theme::stroke, theme::stroke2);

        Rect inner = box.inset(theme::s7, theme::s7);

        // ---- the face, on a stage of its own
        //
        // 420 rather than 340: the stage is clipped, and at 340 the widest
        // hairstyles were sliced down both sides - a hundred and fifty of the
        // hair-by-face-shape combinations lost up to 39px. The figure itself is
        // limited by the stage's height, so the extra width is all clearance.
        float stageWidth = 420.0f;
        Rect stage { inner.x, inner.y, stageWidth, inner.h };
        r.pushClip(stage);
        ui::miiStage(r, stage, m_peer.face(), 0,
            ui::StageFigure { 0.52f, 0.58f, 0.30f, 0.52f, 0.06f, 0.17f },
            theme::r3, theme::r3);
        r.popClip();
        r.strokeRect(stage, theme::r3, theme::stroke, theme::stroke1);

        Rect text { stage.right() + theme::s7, inner.y,
            inner.right() - stage.right() - theme::s7, inner.h };
        float y = text.y;

        // ---- who they are
        TextStyle eyebrow;
        eyebrow.size = theme::textXs;
        eyebrow.weight = FontWeight::Bold;
        eyebrow.color = theme::teal;
        eyebrow.tracking = theme::trackingWider;
        eyebrow.uppercase = true;
        r.text(text.x, y, "awake near you", eyebrow);
        y += eyebrow.size * theme::leadingNormal + theme::s3;

        TextStyle name;
        name.size = theme::text2xl;
        name.weight = FontWeight::Bold;
        name.color = theme::fg1;
        name.tracking = theme::trackingTight;
        name.leading = theme::leadingTight;
        std::string handle = m_peer.handle.empty() ? std::string("Someone") : m_peer.handle;
        r.text(text.x, y, r.ellipsize(handle, name, text.w), name);
        y += name.size * theme::leadingTight + theme::s5;

        // ---- the two facts we actually have about them
        y = drawFact(r, text, y, "How close", m_peer.proximityLabel());
        if (!m_peer.playing.empty())
            y = drawFact(r, text, y, "Playing", m_peer.playing);
        y = drawFact(r, text, y, "Right now", stateLabel(), stateColour());

        // ---- and the thing worth saying plainly
        TextStyle note;
        note.size = theme::textSm;
        note.color = theme::fg3;
        note.leading = theme::leadingNormal;
        r.textWrapped(Rect { text.x, text.bottom() - 96.0f, text.w, 96.0f },
            "Passes trade on their own with everyone in range. There is nothing to press "
            "here, and nothing to wait for.",
            note, 3);
    }

private:
    enum Zone : int {
        Zone_Card = Touch_SceneBase,
    };

    // A label above its value, the way the passport's stat cards read.
    float drawFact(Renderer& r, const Rect& box, float y, const char* label,
        const std::string& value, Color valueColour = theme::fg1)
    {
        TextStyle small;
        small.size = theme::textXs;
        small.weight = FontWeight::Bold;
        small.color = theme::fg4;
        small.tracking = theme::trackingWide;
        small.uppercase = true;
        r.text(box.x, y, label, small);
        y += small.size * theme::leadingNormal + 2.0f;

        TextStyle body;
        body.size = theme::textBase;
        body.color = valueColour;
        r.text(box.x, y, r.ellipsize(value, body, box.w), body);
        return y + body.size * theme::leadingSnug + theme::s4;
    }

    std::string stateLabel() const
    {
        switch (m_peer.state) {
        case Peer::State_Exchanging:
            return "Trading passes";
        case Peer::State_Passed:
            return "You have already crossed";
        case Peer::State_OutOfRange:
            return "Drifted out of range";
        default:
            return "Waiting for the next round";
        }
    }

    Color stateColour() const
    {
        switch (m_peer.state) {
        case Peer::State_Passed:
            return theme::teal;
        case Peer::State_OutOfRange:
            return theme::fg4;
        default:
            return theme::fg1;
        }
    }

    Peer m_peer;
};

} // namespace

std::unique_ptr<Scene> makePeerScene(const Peer& peer)
{
    return std::make_unique<PeerScene>(peer);
}

} // namespace nxp
