#pragma once

// A signed 2D grid position. Signed so that intermediate results of movement
// (for example stepping left from column 0) can be represented and then
// validated against a map before being converted into an unsigned index.
struct Coordinates {
    int x = 0;
    int y = 0;
};

[[nodiscard]] constexpr Coordinates operator+(Coordinates left, Coordinates right) noexcept {
    return {left.x + right.x, left.y + right.y};
}

constexpr Coordinates& operator+=(Coordinates& left, Coordinates right) noexcept {
    left.x += right.x;
    left.y += right.y;
    return left;
}

[[nodiscard]] constexpr bool operator==(Coordinates left, Coordinates right) noexcept {
    return left.x == right.x && left.y == right.y;
}

[[nodiscard]] constexpr bool operator!=(Coordinates left, Coordinates right) noexcept {
    return !(left == right);
}
