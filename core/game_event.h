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
// rule-level result computed by the core, including the outcome terrain, the
// stamina cost that terrain required, and the stamina before and after the
// attempt. A blocked attempt (boundary, terrain, or insufficient stamina) still
// carries these fields with an unchanged before/after value, so consumers never
// re-derive movement cost from the map.
struct MoveAttemptedEvent {
    Direction direction{};
    MoveOutcome outcome{};
};

// The single event a rest command produces. Rest is its own command family: it
// never moves the actor, never touches the map or visibility, and never counts
// as a movement attempt. The payload carries only the stamina transition so a
// frontend can describe the recovery without re-deriving the cap. A rest at full
// stamina still emits this event with `stamina_recovered == 0` and
// `stamina_before == stamina_after`, so every rest consumes exactly one
// contiguous sequence number like every other command.
struct RestedEvent {
    std::uint32_t stamina_before{};
    std::uint32_t stamina_recovered{};
    std::uint32_t stamina_after{};
};

// The payload of a GameEvent. A variant so command families can add their own
// event types without widening a single struct. Consumers must dispatch on the
// active alternative (std::get_if / std::visit) rather than assuming movement:
// the sequence type and default alternative stay MoveAttemptedEvent, but rest
// commands carry a RestedEvent instead.
using GameEventData = std::variant<MoveAttemptedEvent, RestedEvent>;

// One ordered event. `sequence` starts at 0 for a new GameState and increases by
// exactly one per emitted event, so the stream is a total order in
// command-processing order.
struct GameEvent {
    std::uint64_t sequence = 0;
    GameEventData data{};
};
