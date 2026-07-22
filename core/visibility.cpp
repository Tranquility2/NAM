#include "visibility.h"

#include <algorithm>
#include <cassert>

VisibilityMap::VisibilityMap(std::size_t width, std::size_t height)
    : width_(width),
      height_(height),
      cells_(width * height, CellVisibility::unexplored) {}

bool VisibilityMap::contains(Coordinates position) const noexcept {
    return position.x >= 0 && position.y >= 0 &&
           static_cast<std::size_t>(position.x) < width_ &&
           static_cast<std::size_t>(position.y) < height_;
}

std::size_t VisibilityMap::index(Coordinates position) const noexcept {
    return static_cast<std::size_t>(position.y) * width_ +
           static_cast<std::size_t>(position.x);
}

CellVisibility VisibilityMap::at(Coordinates position) const {
    assert(contains(position) && "at requires an in-bounds coordinate");
    return cells_[index(position)];
}

void VisibilityMap::reveal_square(Coordinates center, int radius) {
    // Demote everything currently visible to remembered before recomputing the
    // new sight square, so cells leaving the square persist as memory.
    for (CellVisibility& cell : cells_) {
        if (cell == CellVisibility::visible) {
            cell = CellVisibility::remembered;
        }
    }

    if (width_ == 0 || height_ == 0) {
        return;
    }

    // GameState always supplies a non-negative radius and an in-bounds center,
    // so the arithmetic below can work entirely in std::size_t without any
    // signed intermediates.
    assert(radius >= 0 && "reveal_square requires a non-negative radius");
    assert(contains(center) && "reveal_square requires an in-bounds center");

    const std::size_t r = static_cast<std::size_t>(radius);
    const std::size_t cx = static_cast<std::size_t>(center.x);
    const std::size_t cy = static_cast<std::size_t>(center.y);

    // Inclusive size_t bounds clipped to [0, extent - 1] on each axis. Lower
    // bounds subtract from the center (guarded so they never wrap below 0).
    // Upper bounds add only after capping the radius at the remaining distance
    // to the edge, so the addition is bounded by extent - 1 and cannot overflow.
    const std::size_t x_begin = cx > r ? cx - r : 0;
    const std::size_t x_end = cx + std::min(r, (width_ - 1) - cx);
    const std::size_t y_begin = cy > r ? cy - r : 0;
    const std::size_t y_end = cy + std::min(r, (height_ - 1) - cy);

    // Process each inclusive end and break afterwards, so no counter is ever
    // incremented past the bound (which would wrap at SIZE_MAX in a <= loop).
    for (std::size_t y = y_begin;; ++y) {
        const std::size_t row = y * width_;
        for (std::size_t x = x_begin;; ++x) {
            cells_[row + x] = CellVisibility::visible;
            if (x == x_end) {
                break;
            }
        }
        if (y == y_end) {
            break;
        }
    }
}
