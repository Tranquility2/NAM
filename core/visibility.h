#pragma once

#include <cstddef>
#include <vector>

#include "coordinates.h"

// Three-state exploration memory for a rectangular map, stored independently of
// terrain so any frontend can share identical fog-of-war rules. Each cell is
// one of:
//   * unexplored — never seen; a frontend should reveal nothing about it.
//   * remembered — seen previously but outside current sight; terrain is known
//     but not currently observed.
//   * visible    — currently within sight.
enum class CellVisibility : unsigned char {
    unexplored,
    remembered,
    visible,
};

// A value-owned grid of CellVisibility mirroring a Map's dimensions. Cells are
// stored in a single contiguous, row-major buffer (index = y * width + x) using
// the same coordinate containment and indexing conventions as Map. The class
// owns no terrain and imposes no presentation policy; it is pure semantic state.
class VisibilityMap {
public:
    // Every cell starts unexplored. Dimensions should match the owning Map.
    VisibilityMap(std::size_t width, std::size_t height);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }

    // True when the coordinate lies inside the grid. Must be checked before
    // at(), which has this as a precondition.
    [[nodiscard]] bool contains(Coordinates position) const noexcept;

    // Visibility at a contained coordinate. Precondition: contains(position),
    // asserted consistently with Map::terrain_at.
    [[nodiscard]] CellVisibility at(Coordinates position) const;

    // Refresh sight around `center`: first demote every currently visible cell
    // to remembered, then mark every in-bounds cell whose x and y are each
    // within `radius` of `center` as visible. The square is clipped to all map
    // edges without signed/unsigned underflow or out-of-bounds indexing.
    void reveal_square(Coordinates center, int radius);

private:
    [[nodiscard]] std::size_t index(Coordinates position) const noexcept;

    std::size_t width_;
    std::size_t height_;
    std::vector<CellVisibility> cells_;
};
