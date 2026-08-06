#pragma once

#include <cstdint>
#include <variant>

#include "direction.h"
#include "move_outcome.h"
#include "objective.h"
#include "terrain.h"

// Frontend-neutral, ordered events emitted by the core as commands are
// processed. Events are plain value types: no presentation text, no ownership
// indirection, and no terminal or localization concerns. A frontend consumes
// them to update its own view; the core imposes no retention policy and stores
// no history of its own.

// The single event a movement command produces, whether the actor moved or was
// blocked. `direction` is the requested command; `outcome` preserves the exact
// rule-level result computed by the core, including the outcome terrain and any
// authored content on the destination. A blocked attempt (boundary or impassable
// terrain) still carries these fields, so consumers never re-derive the result
// from the map.
//
// `objective_update` nests the typed level-objective change for this exact
// command: its before/after status brackets the objective around the move, and
// its transition is landmark_discovered on the move that first enters the
// landmark, level_completed on the move that enters the exit, and none
// otherwise. It is default-initialized so existing two-field aggregate
// construction of a movement event remains valid.
//
// `discovery_recorded` is true only on the move that first enters a discovery
// cell. Re-entering an already-found discovery is an ordinary step, so a frontend
// can log the moment once without tracking which cells it has already seen.
//
// `wide_reveal_radius` is the radius of the one-off reveal this move granted, or
// zero when it granted none. A vantage point fires one on first entry, sized by
// what kind of viewpoint it turned out to be (see vantage.h), and reaching the
// level's named landmark fires one of `landmark_reveal_radius`. Carrying the
// radius rather than a flag lets a frontend, and the acceptance gate, say exactly
// how far the level opened up without re-deriving which cells changed.
struct MoveAttemptedEvent {
    Direction direction{};
    MoveOutcome outcome{};
    ObjectiveUpdate objective_update{};
    bool discovery_recorded = false;
    int wide_reveal_radius = 0;

    // Whether this move opened the level up at all.
    [[nodiscard]] constexpr bool granted_wide_reveal() const noexcept {
        return wide_reveal_radius > 0;
    }
};

// The payload of a GameEvent. A variant so command families can add their own
// event types without widening a single struct. Consumers dispatch on the active
// alternative (std::get_if / std::visit) rather than assuming movement, which
// keeps the stream open to future command families.
using GameEventData = std::variant<MoveAttemptedEvent>;

// One ordered event. `sequence` starts at 0 for a new GameState and increases by
// exactly one per emitted event, so the stream is a total order in
// command-processing order.
struct GameEvent {
    std::uint64_t sequence = 0;
    GameEventData data{};
};
