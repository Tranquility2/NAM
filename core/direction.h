#pragma once

#include <optional>

#include "coordinates.h"

// A cardinal movement direction. Replaces the old mutable global moveAxis map;
// the per-direction offset is now a total constexpr function.
enum class Direction : unsigned char {
    up,
    down,
    left,
    right,
};

// The grid offset produced by a single step in the given direction. The y axis
// grows downward to match row-major map storage, so "up" is a negative y step.
[[nodiscard]] constexpr Coordinates direction_delta(Direction direction) noexcept {
    switch (direction) {
        case Direction::up:    return {0, -1};
        case Direction::down:  return {0, 1};
        case Direction::left:  return {-1, 0};
        case Direction::right: return {1, 0};
    }
    return {0, 0};
}

// The inverse of direction_delta: the direction of a single cardinal step, or
// nullopt for any other offset (including a zero or diagonal one).
[[nodiscard]] constexpr std::optional<Direction> direction_of(Coordinates delta) noexcept {
    if (delta.x == 0 && delta.y == -1) return Direction::up;
    if (delta.x == 0 && delta.y == 1) return Direction::down;
    if (delta.x == -1 && delta.y == 0) return Direction::left;
    if (delta.x == 1 && delta.y == 0) return Direction::right;
    return std::nullopt;
}
