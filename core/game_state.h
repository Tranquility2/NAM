#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "level_feature.h"
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
    // and a successful move advances it after position and visibility commit. Frontends only present this state and react to the typed
    // transitions carried on movement events.
    [[nodiscard]] const LevelObjective& objective() const noexcept { return objective_; }

    // True once the actor has reached the exit after discovering the landmark, or
    // the map had a single reachable cell at spawn.
    [[nodiscard]] bool objective_completed() const noexcept {
        return objective_.status == ObjectiveStatus::completed;
    }

    // The authored content this level carries, in placement order. Empty for
    // handcrafted maps, which have no level template.
    [[nodiscard]] const std::vector<LevelFeature>& features() const noexcept {
        return map_.layout().features;
    }

    // The authored content on a cell, if any. Cells carry at most one feature.
    [[nodiscard]] std::optional<LevelFeatureKind> feature_at(Coordinates position) const;

    // How many distinct discoveries the actor has entered. Vantage points are
    // stateless one-off reveals, so only discoveries are counted.
    [[nodiscard]] std::uint32_t discoveries_found() const noexcept { return discoveries_found_; }

    // How many discoveries this level placed, i.e. the denominator of the
    // discovery statistic.
    [[nodiscard]] std::uint32_t discovery_total() const noexcept { return discovery_total_; }

    // Compute the outcome of moving one step without changing any state.
    [[nodiscard]] MoveOutcome peek(Direction direction) const;

    // Attempt to move one step and emit exactly one ordered event describing the
    // attempt. The destination is validated for bounds and terrain passability
    // only: a walkable in-bounds step always succeeds, because no meter can block
    // movement. A blocked or out-of-bounds move leaves all state unchanged —
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
    std::uint64_t next_event_sequence_ = 0;
    // Parallel to map_.layout().features: true once that feature has been entered.
    // Both kinds fire exactly once and are then inert, so this is the whole of the
    // core's feature state. peek() never reads it, which is what keeps peek() a
    // pure function of position.
    std::vector<bool> feature_resolved_;
    std::uint32_t discoveries_found_ = 0;
    std::uint32_t discovery_total_ = 0;
};
