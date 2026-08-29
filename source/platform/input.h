#pragma once

#include <switch.h>

namespace nxp {

// The finger, in design-space coordinates.
//
// The panel only exists in handheld mode --docked, the console reports no
// touches at all and every field here stays false, so nothing needs to check
// the operation mode.
struct Touch {
    bool down = false;     // a finger is on the panel right now
    bool pressed = false;  // it went down this frame
    bool released = false; // it came up this frame
    bool dragged = false;  // it has moved beyond the tap slop since going down

    float x = 0.0f, y = 0.0f;           // where it is
    float startX = 0.0f, startY = 0.0f; // where it went down
    float dx = 0.0f, dy = 0.0f;         // movement since the last frame
    float held = 0.0f;                  // seconds since it went down

    // A release that never turned into a drag: the gesture that means "this".
    bool tapped() const { return released && !dragged; }
};

// A frame of input, with the analogue stick folded into the same directional
// events as the d-pad and an auto-repeat suitable for list navigation.
struct Input {
    uint64_t down = 0;
    uint64_t held = 0;
    uint64_t up = 0;

    float stickX = 0.0f;
    float stickY = 0.0f;

    bool navUp = false;
    bool navDown = false;
    bool navLeft = false;
    bool navRight = false;

    bool pressed(HidNpadButton button) const { return (down & button) != 0; }
    bool holding(HidNpadButton button) const { return (held & button) != 0; }

    bool accept() const { return pressed(HidNpadButton_A); }
    bool back() const { return pressed(HidNpadButton_B); }

    Touch touch;
};

class InputTracker {
public:
    // `designWidth`/`designHeight` are the coordinate space touches are
    // reported in, so scenes never see panel pixels.
    void init(float designWidth, float designHeight);
    void update(float dt);

    const Input& state() const { return m_state; }

    // Drops the gesture in progress and ignores the panel until every finger
    // is off it.
    //
    // Needed after a system applet has been on screen: the touch that
    // dismissed the software keyboard is still down when we get control back,
    // and without this it would be delivered as a fresh tap on whatever
    // happens to be underneath it.
    void forgetTouch();

private:
    void applyRepeat(float dt);
    void updateTouch(float dt);

    PadState m_pad {};
    Input m_state;

    float m_designWidth = 1920.0f;
    float m_designHeight = 1080.0f;
    bool m_touchWasDown = false;
    bool m_ignoreTouchUntilRelease = false;

    float m_repeatTimer = 0.0f;
    int m_repeatDirection = -1; // index into the four directions, -1 = none
    bool m_repeatFired = false;
};

} // namespace nxp
