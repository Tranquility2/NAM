#include "map.h"

#include <cassert>
#include <stdexcept>
#include <utility>

Map::Map(std::size_t width, std::size_t height, std::vector<Terrain> cells, Coordinates spawn)
    : width_(width), height_(height), cells_(std::move(cells)), spawn_(spawn) {
    if (width_ == 0 || height_ == 0) {
        throw std::invalid_argument("Map dimensions must be non-zero");
    }
    if (cells_.size() != width_ * height_) {
        throw std::invalid_argument("Map cell count does not match dimensions");
    }
    if (!contains(spawn_)) {
        throw std::invalid_argument("Map spawn is outside the bounds");
    }
}

bool Map::contains(Coordinates position) const noexcept {
    return position.x >= 0 && position.y >= 0 &&
           static_cast<std::size_t>(position.x) < width_ &&
           static_cast<std::size_t>(position.y) < height_;
}

std::size_t Map::index(Coordinates position) const noexcept {
    return static_cast<std::size_t>(position.y) * width_ + static_cast<std::size_t>(position.x);
}

Terrain Map::terrain_at(Coordinates position) const {
    assert(contains(position) && "terrain_at requires an in-bounds coordinate");
    return cells_[index(position)];
}

std::string Map::to_string() const {
    return serialize(nullptr, '\0');
}

std::string Map::to_string(Coordinates actor, char actor_glyph) const {
    assert(contains(actor) && "to_string requires an in-bounds actor");
    return serialize(&actor, actor_glyph);
}

std::string Map::serialize(const Coordinates* actor, char actor_glyph) const {
    std::string out;
    // width * height terrain glyphs plus one separator after every row except
    // the last: reserve the exact upper bound so the buffer never reallocates
    // past its size or overflows.
    out.reserve(width_ * height_ + height_);

    for (std::size_t y = 0; y < height_; ++y) {
        if (y != 0) {
            out.push_back('\n');
        }
        for (std::size_t x = 0; x < width_; ++x) {
            const Coordinates here{static_cast<int>(x), static_cast<int>(y)};
            if (actor != nullptr && *actor == here) {
                out.push_back(actor_glyph);
            } else {
                out.push_back(symbol_of(cells_[index(here)]));
            }
        }
    }

    return out;
}
