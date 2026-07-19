#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "coordinates.h"
#include "terrain.h"

// A rectangular grid of terrain plus the spawn point an actor should start on.
//
// Cells are stored in a single contiguous, value-owned buffer in row-major
// order (index = y * width + x). This removes all manual allocation, gives
// automatic copy/move semantics, and maps naturally onto tile rendering.
class Map {
public:
    // Precondition: cells.size() == width * height, width > 0, height > 0, and
    // spawn is inside the bounds. Violations throw std::invalid_argument; the
    // parser guarantees them, so well-formed callers never trigger the throw.
    Map(std::size_t width, std::size_t height, std::vector<Terrain> cells, Coordinates spawn);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] Coordinates spawn() const noexcept { return spawn_; }

    // True when the coordinate lies inside the grid. Must be checked before
    // terrain_at, which has this as a precondition.
    [[nodiscard]] bool contains(Coordinates position) const noexcept;

    // Terrain at a contained coordinate. Precondition: contains(position).
    [[nodiscard]] Terrain terrain_at(Coordinates position) const;

    // Serialize the terrain to text, one row per line separated by '\n'. Row
    // separators are inserted explicitly; there is no trailing newline and the
    // buffer is sized exactly, so it can never overflow.
    [[nodiscard]] std::string to_string() const;

    // As to_string(), but the cell at `actor` is rendered as `actor_glyph`
    // instead of its terrain symbol. Precondition: contains(actor).
    [[nodiscard]] std::string to_string(Coordinates actor, char actor_glyph) const;

private:
    [[nodiscard]] std::size_t index(Coordinates position) const noexcept;
    [[nodiscard]] std::string serialize(const Coordinates* actor, char actor_glyph) const;

    std::size_t width_;
    std::size_t height_;
    std::vector<Terrain> cells_;
    Coordinates spawn_;
};
