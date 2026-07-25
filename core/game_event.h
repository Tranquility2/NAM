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

// The typed change a single command causes to the rescue lifecycle. `none` means
// the actor is not stranded after this command; `stranded` marks the command
// after which the objective is still incomplete, provisions are exhausted, and no
// adjacent cardinal move is walkable and affordable, so the run ends in a rescue.
// It is carried on the command event so frontends never re-derive strandedness.
enum class RescueTransition {
    none,
    stranded,
};

// The single event a movement command produces, whether the actor moved or was
// blocked. `direction` is the requested command; `outcome` preserves the exact
// rule-level result computed by the core, including the outcome terrain, the
// stamina cost that terrain required, and the stamina before and after the
// attempt. A blocked attempt (boundary, terrain, or insufficient stamina) still
// carries these fields with an unchanged before/after value, so consumers never
// re-derive movement cost from the map.
//
// `objective_update` nests the typed beacon-objective change for this exact
// command: its before/after status brackets the objective around the move, and
// its transition is beacon_discovered on the move that first enters the beacon,
// expedition_completed on the move that returns to spawn, and none otherwise.
// It is default-initialized so existing two-field aggregate construction of a
// movement event remains valid.
struct MoveAttemptedEvent {
    Direction direction{};
    MoveOutcome outcome{};
    ObjectiveUpdate objective_update{};
};

// The typed outcome of a rest command. Rest never moves the actor, never touches
// the map or visibility, and never counts as a movement attempt:
//   * recovered    — stamina was below full and a provision was spent to recover
//                    rest_recovery_of(terrain), capped at the stamina maximum.
//   * already_full — stamina was already full, so no provision was spent and no
//                    stamina changed (a heroic no-op).
//   * no_provisions— stamina was below full but no provision remained, so no
//                    resource state changed.
enum class RestResult {
    recovered,
    already_full,
    no_provisions,
};

// The single event a rest command produces. The payload carries the typed
// outcome, the terrain rested on, and the before/after stamina and provisions so
// a frontend can describe the rest, the recovery, and the provision change
// without re-deriving any of them. Every rest still emits exactly one event and
// consumes exactly one contiguous sequence number like every other command.
struct RestedEvent {
    RestResult result{};
    Terrain terrain{};
    std::uint32_t stamina_before{};
    std::uint32_t stamina_after{};
    std::uint32_t stamina_recovered{};
    std::uint32_t provisions_before{};
    std::uint32_t provisions_after{};
};

// The payload of a GameEvent. A variant so command families can add their own
// event types without widening a single struct. Consumers must dispatch on the
// active alternative (std::get_if / std::visit) rather than assuming movement:
// the sequence type and default alternative stay MoveAttemptedEvent, but rest
// commands carry a RestedEvent instead.
using GameEventData = std::variant<MoveAttemptedEvent, RestedEvent>;

// One ordered event. `sequence` starts at 0 for a new GameState and increases by
// exactly one per emitted event, so the stream is a total order in
// command-processing order. `rescue` is the typed rescue lifecycle transition
// this command caused (none unless the command left the actor stranded), carried
// on the event so frontends can end the run without re-deriving strandedness.
struct GameEvent {
    std::uint64_t sequence = 0;
    GameEventData data{};
    RescueTransition rescue = RescueTransition::none;
};
