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
// rule-level result computed by the core, including the outcome terrain, the
// stamina the terrain charged, the stamina it gave back, and the stamina before
// and after. A blocked attempt (boundary or impassable terrain) still carries
// these fields with unchanged before/after values, so consumers never re-derive
// movement cost from the map.
//
// `objective_update` nests the typed level-objective change for this exact
// command: its before/after status brackets the objective around the move, and
// its transition is landmark_discovered on the move that first enters the
// landmark, level_completed on the move that enters the exit, and none
// otherwise. It is default-initialized so existing two-field aggregate
// construction of a movement event remains valid.
struct MoveAttemptedEvent {
    Direction direction{};
    MoveOutcome outcome{};
    ObjectiveUpdate objective_update{};
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
