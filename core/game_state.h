#pragma once

#include <cstdint>
#include <string>

#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "map.h"
#include "move_outcome.h"
#include "objective.h"
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

    // The stamina a new expedition starts with and can never exceed in this
    // release. Kept here as a core constant so movement affordability, replay,
    // SDL, and tests share one authoritative maximum.
    static constexpr std::uint32_t maximum_stamina = 20;

    // The actor's current stamina and the fixed maximum. Stamina never refuses a
    // step: a successful move charges the destination terrain's cost with a
    // saturating subtraction and immediately adds back that terrain's
    // passive_recovery_of amount, so easy ground restores the meter and rough
    // ground drains it. Reaching the level landmark for the first time restores
    // it completely.
    [[nodiscard]] std::uint32_t stamina() const noexcept { return stamina_; }
    [[nodiscard]] std::uint32_t max_stamina() const noexcept { return maximum_stamina; }

    // The exploration/sight radius revealed around the actor is selected from
    // the actor's current terrain via visibility_radius_of, so terrain, replay,
    // SDL, and tests share one authoritative mapping instead of a fixed literal.
    // Base terrain (open/fields/water) uses radius 2 for the clipped 5x5 sight
    // square; hills use radius 3 (7x7) and mountains radius 4 (9x9). Later
    // weather or equipment modifiers can compose onto this baseline without
    // moving visibility state into a frontend.
    static constexpr int base_visibility_radius = visibility_radius_of(Terrain::open);
    static constexpr int hill_visibility_radius = visibility_radius_of(Terrain::hill);
    static constexpr int mountain_visibility_radius = visibility_radius_of(Terrain::mountain);

    // The sight radius for the actor's current terrain. A move refreshes fog
    // using this value after committing position and stamina, so elevated
    // terrain reveals farther the moment the actor stands on it.
    [[nodiscard]] int visibility_radius() const { return visibility_radius_of(actor_terrain()); }

    // Frontend-neutral exploration memory around the actor. Its dimensions match
    // the owned Map, and the actor cell is always currently visible.
    [[nodiscard]] const VisibilityMap& visibility() const noexcept { return visibility_; }

    // The core-owned level objective: its landmark, exit, generated name, and
    // current status. Every GameState receives one deterministic objective,
    // and a successful move advances it after position, stamina, and visibility
    // commit. Frontends only present this state and react to the typed
    // transitions carried on movement events.
    [[nodiscard]] const LevelObjective& objective() const noexcept { return objective_; }

    // True once the actor has reached the exit after discovering the landmark, or
    // the map had a single reachable cell at spawn.
    [[nodiscard]] bool objective_completed() const noexcept {
        return objective_.status == ObjectiveStatus::completed;
    }

    // Compute the outcome of moving one step without changing any state.
    [[nodiscard]] MoveOutcome peek(Direction direction) const;

    // Attempt to move one step and emit exactly one ordered event describing the
    // attempt. The destination is validated for bounds and terrain passability
    // only: a walkable in-bounds step always succeeds, because stamina can never
    // block movement. A blocked or out-of-bounds move leaves all state unchanged —
    // but still emits an event and consumes a sequence number. The returned event
    // carries the requested direction and the full MoveOutcome.
    [[nodiscard]] GameEvent move(Direction direction);

    // Render the map with the actor drawn as `actor_glyph`. The glyph is a
    // frontend choice; the core imposes no presentation of its own.
    [[nodiscard]] std::string render(char actor_glyph = '+') const;

private:
    Map map_;
    // Declared immediately after map_ and initialized from the constructed map_
    // (never the moved-from constructor argument), so exit placement reads a
    // fully valid map.
    LevelObjective objective_;
    Coordinates actor_position_;
    VisibilityMap visibility_;
    std::uint32_t stamina_ = maximum_stamina;
    std::uint64_t next_event_sequence_ = 0;
};
