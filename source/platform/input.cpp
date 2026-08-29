#include "platform/input.h"

#include <cmath>

namespace nxp {

namespace {
    constexpr float kFirstRepeatDelay = 0.34f;
    constexpr float kRepeatInterval = 0.09f;

    // The touchscreen reports in panel pixels regardless of what the GPU is
    // rendering at, and the panel is 1280x720.
    constexpr float kPanelWidth = 1280.0f;
    constexpr float kPanelHeight = 720.0f;

    // How far a finger may travel and still count as a tap rather than a drag,
    // in design-space pixels. Roughly a fingertip's own wobble.
    constexpr float kTapSlop = 18.0f;
}

void InputTracker::init(float designWidth, float designHeight)
{
    m_designWidth = designWidth;
    m_designHeight = designHeight;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&m_pad);
    hidInitializeTouchScreen();
}

void InputTracker::update(float dt)
{
    padUpdate(&m_pad);

    m_state.down = padGetButtonsDown(&m_pad);
    m_state.held = padGetButtons(&m_pad);
    m_state.up = padGetButtonsUp(&m_pad);

    HidAnalogStickState stick = padGetStickPos(&m_pad, 0);
    m_state.stickX = static_cast<float>(stick.x) / 32767.0f;
    m_state.stickY = static_cast<float>(stick.y) / 32767.0f;

    applyRepeat(dt);
    updateTouch(dt);
}

void InputTracker::forgetTouch()
{
    m_ignoreTouchUntilRelease = true;
    m_state.touch = Touch {};
    m_touchWasDown = false;
}

void InputTracker::updateTouch(float dt)
{
    Touch& touch = m_state.touch;

    HidTouchScreenState screen {};
    bool down = hidGetTouchScreenStates(&screen, 1) > 0 && screen.count > 0;

    if (m_ignoreTouchUntilRelease) {
        if (down) {
            touch = Touch {};
            m_touchWasDown = false;
            return;
        }
        m_ignoreTouchUntilRelease = false;
    }

    touch.pressed = down && !m_touchWasDown;
    touch.released = !down && m_touchWasDown;
    touch.down = down;

    if (down) {
        // Only the first finger is followed. The interface has no two-finger
        // gesture, and picking the lowest finger index keeps a resting thumb
        // from stealing the point.
        float x = static_cast<float>(screen.touches[0].x) * m_designWidth / kPanelWidth;
        float y = static_cast<float>(screen.touches[0].y) * m_designHeight / kPanelHeight;

        if (touch.pressed) {
            touch.startX = x;
            touch.startY = y;
            touch.dx = 0.0f;
            touch.dy = 0.0f;
            touch.held = 0.0f;
            touch.dragged = false;
        } else {
            touch.dx = x - touch.x;
            touch.dy = y - touch.y;
            touch.held += dt;
        }

        touch.x = x;
        touch.y = y;

        float travelX = x - touch.startX;
        float travelY = y - touch.startY;
        if (travelX * travelX + travelY * travelY > kTapSlop * kTapSlop)
            touch.dragged = true;
    } else {
        // Position is left where the finger lifted, so a release can still be
        // matched against the thing under it.
        touch.dx = 0.0f;
        touch.dy = 0.0f;
    }

    m_touchWasDown = down;
}

void InputTracker::applyRepeat(float dt)
{
    // libnx already folds the sticks into the AnyUp/AnyDown/... masks with its
    // own deadzone, so directional navigation only has to add auto-repeat.
    int direction = -1;
    if (m_state.held & HidNpadButton_AnyUp)
        direction = 0;
    else if (m_state.held & HidNpadButton_AnyDown)
        direction = 1;
    else if (m_state.held & HidNpadButton_AnyLeft)
        direction = 2;
    else if (m_state.held & HidNpadButton_AnyRight)
        direction = 3;

    if (direction != m_repeatDirection) {
        // A brand new direction fires immediately, then waits out the delay.
        m_repeatDirection = direction;
        m_repeatTimer = kFirstRepeatDelay;
        m_repeatFired = direction >= 0;
    } else if (direction >= 0) {
        m_repeatTimer -= dt;
        m_repeatFired = m_repeatTimer <= 0.0f;
        if (m_repeatFired)
            m_repeatTimer = kRepeatInterval;
    } else {
        m_repeatFired = false;
    }

    m_state.navUp = m_repeatFired && direction == 0;
    m_state.navDown = m_repeatFired && direction == 1;
    m_state.navLeft = m_repeatFired && direction == 2;
    m_state.navRight = m_repeatFired && direction == 3;
}

} // namespace nxp
