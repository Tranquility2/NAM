#pragma once

#include <cstdint>
#include <variant>

#include "direction.h"
#include "move_outcome.h"

// Frontend-neutral, ordered events emitted by the core as commands are
// processed. Events are plain value types: no presentation text, no ownership
// indirection, and no terminal or localization concerns. A frontend consumes
// them to update its own view; the core imposes no retention policy and stores
// no history of its own.

// The single event a movement command produces, whether the actor moved or was
// blocked. `direction` is the requested command; `outcome` preserves the exact
// rule-level result (result, coordinates, and terrain) computed by the core.
struct MoveAttemptedEvent {
    Direction direction{};
    MoveOutcome outcome{};
};

// The payload of a GameEvent. A variant so future command families can add their
// own event types without widening a single struct.
using GameEventData = std::variant<MoveAttemptedEvent>;

// One ordered event. `sequence` starts at 0 for a new GameState and increases by
// exactly one per emitted event, so the stream is a total order in
// command-processing order.
struct GameEvent {
    std::uint64_t sequence = 0;
    GameEventData data{};
};
