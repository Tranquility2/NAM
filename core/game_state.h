#pragma once

#include <cstdint>
#include <string>

#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "map.h"
#include "move_outcome.h"
#include "terrain.h"
#include "visibility.h"

// The mutable game world: an owned Map (terrain) plus the actor's position.
// Keeping the actor separate from the Map is what lets the same terrain be
// shared, serialized, or reloaded without entangling actor state.
class GameState {
public:
    // Start the actor on the map's spawn point.
    explicit GameState(Map map);

    [[nodiscard]] const Map& map() const noexcept { return map_; }
    [[nodiscard]] Coordinates actor_position() const noexcept { return actor_position_; }
    [[nodiscard]] Terrain actor_terrain() const { return map_.terrain_at(actor_position_); }

    // The exploration/sight radius revealed around the actor. Kept here as a
    // core constant so later terrain, weather, daylight, or equipment modifiers
    // can replace the fixed value without moving visibility state into a
    // frontend. A radius of 2 yields the clipped 5x5 sight square.
    static constexpr int base_visibility_radius = 2;

    // Frontend-neutral exploration memory around the actor. Its dimensions match
    // the owned Map, and the actor cell is always currently visible.
    [[nodiscard]] const VisibilityMap& visibility() const noexcept { return visibility_; }

    // Compute the outcome of moving one step without changing any state.
    [[nodiscard]] MoveOutcome peek(Direction direction) const;

    // Attempt to move one step and emit exactly one ordered event describing the
    // attempt. The destination is validated for both bounds and terrain
    // passability before the actor position is committed, so a blocked or
    // out-of-bounds move leaves all state unchanged — but still emits an event
    // and consumes a sequence number. The returned event carries the requested
    // direction and the full MoveOutcome.
    [[nodiscard]] GameEvent move(Direction direction);

    // Render the map with the actor drawn as `actor_glyph`. The glyph is a
    // frontend choice; the core imposes no presentation of its own.
    [[nodiscard]] std::string render(char actor_glyph = '+') const;

private:
    Map map_;
    Coordinates actor_position_;
    VisibilityMap visibility_;
    std::uint64_t next_event_sequence_ = 0;
};
