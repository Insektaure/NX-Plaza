#include "ui/scroll.h"

#include <algorithm>
#include <cmath>

namespace nxp::ui {

namespace {
    // How quickly a flick runs out of speed. e^(-6t): about a sixth of the
    // speed left after a quarter of a second.
    constexpr float kFlickDecay = 6.0f;

    // Below this, in design pixels per second, a flick has stopped.
    constexpr float kFlickFloor = 24.0f;

    // How fast the offset chases a cursor move.
    constexpr float kFollowRate = 14.0f;

    // Where a recentred item sits in the viewport. Slightly above the middle,
    // because a list reads downwards and the eye wants to see what is next.
    constexpr float kCenterBias = 0.42f;
}

void ScrollView::setBounds(float viewport, float content)
{
    m_viewport = std::max(viewport, 0.0f);
    m_content = std::max(content, 0.0f);
    clamp();
}

float ScrollView::maxOffset() const
{
    return std::max(0.0f, m_content - m_viewport);
}

void ScrollView::clamp()
{
    float limit = maxOffset();
    m_offset = std::min(std::max(m_offset, 0.0f), limit);
    m_target = std::min(std::max(m_target, 0.0f), limit);
}

void ScrollView::drag(float delta, float dt)
{
    m_dragging = true;
    m_offset += delta;

    float before = m_offset;
    clamp();
    // Hitting an end during a drag also kills the speed, so releasing there
    // does not fling into a wall.
    bool clamped = std::fabs(before - m_offset) > 0.001f;

    if (dt > 0.0001f) {
        float instant = clamped ? 0.0f : delta / dt;
        // Smoothed, so one jittery sample cannot launch the list.
        m_velocity = m_velocity * 0.6f + instant * 0.4f;
    }

    m_target = m_offset;
}

void ScrollView::release()
{
    m_dragging = false;
    if (std::fabs(m_velocity) < kFlickFloor)
        m_velocity = 0.0f;
}

void ScrollView::stop()
{
    m_dragging = false;
    m_velocity = 0.0f;
    m_target = m_offset;
}

bool ScrollView::absorbPress()
{
    if (!coasting())
        return false;
    stop();
    return true;
}

bool ScrollView::coasting() const
{
    return std::fabs(m_velocity) >= kFlickFloor;
}

void ScrollView::update(float dt)
{
    if (m_dragging || dt <= 0.0f)
        return;

    if (coasting()) {
        m_offset += m_velocity * dt;
        m_velocity *= std::exp(-dt * kFlickDecay);

        float before = m_offset;
        clamp();
        if (std::fabs(before - m_offset) > 0.001f)
            m_velocity = 0.0f; // ran into an end
        m_target = m_offset;
        return;
    }

    m_velocity = 0.0f;
    m_offset += (m_target - m_offset) * std::min(1.0f, dt * kFollowRate);
    if (std::fabs(m_target - m_offset) < 0.25f)
        m_offset = m_target;
}

void ScrollView::centerOn(float center)
{
    m_velocity = 0.0f;
    m_target = center - m_viewport * kCenterBias;
    clamp();
}

bool ScrollView::visible(float top, float bottom) const
{
    return top >= m_offset && bottom <= m_offset + m_viewport;
}

float ScrollView::progress() const
{
    float limit = maxOffset();
    return limit > 0.0f ? m_offset / limit : 0.0f;
}

float ScrollView::visibleFraction() const
{
    return m_content > 0.0f ? std::min(1.0f, m_viewport / m_content) : 1.0f;
}

} // namespace nxp::ui
