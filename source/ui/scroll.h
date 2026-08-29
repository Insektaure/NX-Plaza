#pragma once

#include "gfx/types.h"

namespace nxp::ui {

// One axis of scrolling.
//
// Every list used to derive its scroll position from the selected index, which
// works for a d-pad and is impossible for a finger: a drag has nothing to
// select. So the offset is the authoritative state here, and the cursor and
// the finger are two things that move it - a cursor move recentres it, a drag
// sets it directly and a flick keeps it going.
class ScrollView {
public:
    // Called every frame from draw(), where the geometry is known.
    void setBounds(float viewport, float content);

    // Scroll by `delta` content pixels. Positive moves towards the end of the
    // content, so a finger moving *down* the screen passes `-touch.dy`.
    void drag(float delta, float dt);

    // The finger lifted: carry on at whatever speed it was moving.
    void release();

    // Kill any motion, e.g. because the cursor has taken over.
    void stop();

    // A finger just went down. If the view was still coasting from a flick,
    // this stops it and returns true, meaning "that touch was a brake" - the
    // caller should not also treat the gesture as a tap, because the row under
    // the finger is not the row that was there when the frame was drawn.
    bool absorbPress();

    // Still moving under its own momentum.
    bool coasting() const;

    void update(float dt);

    // Put `center` (a content coordinate) in the viewport, biased slightly
    // above the middle the way the eye expects a cursor to sit.
    void centerOn(float center);

    // True when this content coordinate range is fully on screen.
    bool visible(float top, float bottom) const;

    float offset() const { return m_offset; }
    float viewport() const { return m_viewport; }
    float content() const { return m_content; }
    float maxOffset() const;
    bool scrollable() const { return maxOffset() > 0.001f; }

    // For the scrollbar.
    float progress() const;
    float visibleFraction() const;

private:
    void clamp();

    float m_offset = 0.0f;
    float m_target = 0.0f;
    float m_velocity = 0.0f;
    float m_viewport = 0.0f;
    float m_content = 0.0f;
    bool m_dragging = false;
};

} // namespace nxp::ui
