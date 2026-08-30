#pragma once

#include <stdint.h>
#include <stddef.h>

namespace BursztynUI {

struct Point {
    int x{0};
    int y{0};

    constexpr Point() = default;
    constexpr Point(int _x, int _y) : x(_x), y(_y) {}

    constexpr Point operator+(const Point& o) const { return {x + o.x, y + o.y}; }
    constexpr Point operator-(const Point& o) const { return {x - o.x, y - o.y}; }
    constexpr bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Point& o) const { return !(*this == o); }
};

struct Size {
    int width{0};
    int height{0};

    constexpr Size() = default;
    constexpr Size(int w, int h) : width(w < 0 ? 0 : w), height(h < 0 ? 0 : h) {}

    constexpr bool operator==(const Size& o) const { return width == o.width && height == o.height; }
    constexpr bool operator!=(const Size& o) const { return !(*this == o); }
};

struct Thickness {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};

    constexpr Thickness() = default;
    constexpr Thickness(int all) : left(all), top(all), right(all), bottom(all) {}
    constexpr Thickness(int horiz, int vert) : left(horiz), top(vert), right(horiz), bottom(vert) {}
    constexpr Thickness(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b) {}

    constexpr int horizontal() const { return left + right; }
    constexpr int vertical() const { return top + bottom; }
};

struct Rect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    constexpr Rect() = default;
    constexpr Rect(int _x, int _y, int _w, int _h)
        : x(_x), y(_y), width(_w < 0 ? 0 : _w), height(_h < 0 ? 0 : _h) {}
    constexpr Rect(Point pt, Size sz)
        : x(pt.x), y(pt.y), width(sz.width), height(sz.height) {}

    constexpr int right() const { return x + width; }
    constexpr int bottom() const { return y + height; }
    constexpr Point position() const { return {x, y}; }
    constexpr Size size() const { return {width, height}; }

    constexpr bool is_empty() const { return width <= 0 || height <= 0; }

    constexpr bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    constexpr bool contains(Point pt) const { return contains(pt.x, pt.y); }

    constexpr bool intersects(const Rect& o) const {
        return !(x + width <= o.x || o.x + o.width <= x ||
                 y + height <= o.y || o.y + o.height <= y);
    }

    Rect intersection(const Rect& o) const {
        if (!intersects(o)) return {0, 0, 0, 0};
        int nx = x > o.x ? x : o.x;
        int ny = y > o.y ? y : o.y;
        int nr = (x + width) < (o.x + o.width) ? (x + width) : (o.x + o.width);
        int nb = (y + height) < (o.y + o.height) ? (y + height) : (o.y + o.height);
        return {nx, ny, nr - nx, nb - ny};
    }

    Rect union_with(const Rect& o) const {
        if (is_empty()) return o;
        if (o.is_empty()) return *this;
        int nx = x < o.x ? x : o.x;
        int ny = y < o.y ? y : o.y;
        int nr = (x + width) > (o.x + o.width) ? (x + width) : (o.x + o.width);
        int nb = (y + height) > (o.y + o.height) ? (y + height) : (o.y + o.height);
        return {nx, ny, nr - nx, nb - ny};
    }

    Rect deflated(Thickness t) const {
        return {x + t.left, y + t.top,
                width - t.horizontal() < 0 ? 0 : width - t.horizontal(),
                height - t.vertical() < 0 ? 0 : height - t.vertical()};
    }

    Rect inflated(Thickness t) const {
        return {x - t.left, y - t.top, width + t.horizontal(), height + t.vertical()};
    }
};

enum class HorizontalAlignment {
    Start,
    Center,
    End,
    Stretch
};

enum class VerticalAlignment {
    Start,
    Center,
    End,
    Stretch
};

enum class Orientation {
    Horizontal, // Row
    Vertical    // Column
};

template <typename T>
constexpr T clamp(T val, T min_v, T max_v) {
    return (val < min_v) ? min_v : ((val > max_v) ? max_v : val);
}

} // namespace BursztynUI
