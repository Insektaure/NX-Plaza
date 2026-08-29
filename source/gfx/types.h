#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nxp {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    static constexpr Color hex(uint32_t rgb, float alpha = 1.0f)
    {
        return Color {
            static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
            static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(rgb & 0xFF) / 255.0f,
            alpha
        };
    }

    // HSL, because the procedural portraits and the "your week" bars pick
    // colours by hue rotation.
    static Color hsl(float hueDegrees, float s, float l, float alpha = 1.0f);

    constexpr Color withAlpha(float alpha) const { return Color { r, g, b, alpha }; }
    constexpr Color scaleAlpha(float factor) const { return Color { r, g, b, a * factor }; }

    Color mix(const Color& other, float t) const
    {
        t = std::min(std::max(t, 0.0f), 1.0f);
        return Color {
            r + (other.r - r) * t,
            g + (other.g - g) * t,
            b + (other.b - b) * t,
            a + (other.a - a) * t
        };
    }

    uint32_t packed() const
    {
        auto q = [](float v) -> uint32_t {
            float c = std::min(std::max(v, 0.0f), 1.0f);
            return static_cast<uint32_t>(c * 255.0f + 0.5f);
        };
        return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
    }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    constexpr float right() const { return x + w; }
    constexpr float bottom() const { return y + h; }
    constexpr float centerX() const { return x + w * 0.5f; }
    constexpr float centerY() const { return y + h * 0.5f; }

    constexpr Rect inset(float d) const { return Rect { x + d, y + d, w - 2 * d, h - 2 * d }; }
    constexpr Rect inset(float dx, float dy) const { return Rect { x + dx, y + dy, w - 2 * dx, h - 2 * dy }; }
    constexpr Rect offset(float dx, float dy) const { return Rect { x + dx, y + dy, w, h }; }

    constexpr Rect withW(float nw) const { return Rect { x, y, nw, h }; }
    constexpr Rect withH(float nh) const { return Rect { x, y, w, nh }; }

    // Slices `amount` off an edge and shrinks this rect. Lets a column of rows
    // be laid out without arithmetic soup.
    Rect cutTop(float amount)
    {
        Rect out { x, y, w, std::min(amount, h) };
        y += out.h;
        h -= out.h;
        return out;
    }

    Rect cutLeft(float amount)
    {
        Rect out { x, y, std::min(amount, w), h };
        x += out.w;
        w -= out.w;
        return out;
    }

    Rect cutBottom(float amount)
    {
        float take = std::min(amount, h);
        h -= take;
        return Rect { x, y + h, w, take };
    }

    constexpr bool contains(float px, float py) const
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }

    Rect intersect(const Rect& o) const
    {
        float nx = std::max(x, o.x);
        float ny = std::max(y, o.y);
        float nr = std::min(right(), o.right());
        float nb = std::min(bottom(), o.bottom());
        return Rect { nx, ny, std::max(0.0f, nr - nx), std::max(0.0f, nb - ny) };
    }

    constexpr bool empty() const { return w <= 0.0f || h <= 0.0f; }
};

enum class Align : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Middle, Bottom };

} // namespace nxp
