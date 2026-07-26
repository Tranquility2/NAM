#pragma once

#include <cstdint>
#include <variant>

#include "direction.h"
#include "expedition_time.h"
#include "move_outcome.h"
#include "objective.h"
#include "terrain.h"

// Frontend-neutral, ordered events emitted by the core as commands are
// processed. Events are plain value types: no presentation text, no ownership
// indirection, and no terminal or localization concerns. A frontend consumes
// them to update its own view; the core imposes no retention policy and stores
// no history of its own.

// The typed change a single command causes to the expedition ending lifecycle
// (REQ-026). `none` means the expedition continues; `rescued` marks the command
// after which, on a day before the deadline, the objective is still incomplete
// and no successful move, emergency rest, or eligible affordable camp or bivouac
// remains, so a resource-rescue party is dispatched; `overdue` marks the command
// after which, on the final allowed day, the objective is still incomplete and no
// successful continuation fits, so a late retrieval party collects the explorer.
// It is carried on every command event so frontends never re-derive the ending.
enum class ExpeditionEndingTransition {
    none,
    rescued,
    overdue,
};

// Whether a camp action is a normal overnight camp or an emergency bivouac. The
// kind is a pure function of the terrain camped on (REQ-011 / REQ-012): open,
// fields, and hills support a normal camp; water and mountains force a bivouac.
enum class CampKind {
    normal,
    bivouac,
};

// The camp kind forced by the terrain an actor stands on. Open, fields, and hills
// permit a normal camp; water and mountains force an emergency bivouac. Both wall
// variants are unoccupiable, so they fall back to a normal camp to keep the switch
// total; an actor can never stand on a wall to camp. This is the single source of
// truth shared by planning and the live GameState so the two can never drift.
[[nodiscard]] constexpr CampKind camp_kind_of(Terrain terrain) noexcept {
    switch (terrain) {
        case Terrain::open:            return CampKind::normal;
        case Terrain::fields:          return CampKind::normal;
        case Terrain::hill:            return CampKind::normal;
        case Terrain::water:           return CampKind::bivouac;
        case Terrain::mountain:        return CampKind::bivouac;
        case Terrain::wall_horizontal: return CampKind::normal;
        case Terrain::wall_vertical:   return CampKind::normal;
    }
    return CampKind::normal;
}

// The provisions a successful camp of this kind consumes (REQ-011 / REQ-012): a
// normal camp costs 1 provision, an emergency bivouac costs 2.
[[nodiscard]] constexpr std::uint32_t camp_provision_cost_of(CampKind kind) noexcept {
    return kind == CampKind::bivouac ? 2u : 1u;
}

// The stamina a successful camp of this kind leaves the actor with (REQ-011 /
// REQ-012 / REQ-013): a normal camp restores stamina to the cap; a bivouac sets
// stamina to exactly 10, representing poor overnight recovery, whether the actor
// began the night above or below that value.
[[nodiscard]] constexpr std::uint32_t camp_stamina_result_of(CampKind kind,
                                                             std::uint32_t max_stamina) noexcept {
    return kind == CampKind::bivouac ? 10u : max_stamina;
}

// The single event a movement command produces, whether the actor moved or was
// blocked. `direction` is the requested command; `outcome` preserves the exact
// rule-level result computed by the core, including the outcome terrain, the
// stamina cost and travel hours that terrain required, the stamina before and
// after, and the expedition time before and after. A blocked attempt (boundary,
// terrain, insufficient daylight, or insufficient stamina) still carries these
// fields with unchanged before/after values, so consumers never re-derive
// movement cost or time from the map.
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
//   * recovered          — stamina was below full, daylight remained, and a
//                          provision was spent to recover rest_recovery_of(terrain),
//                          capped at the stamina maximum, consuming
//                          emergency_rest_hours of daylight.
//   * already_full       — stamina was already full, so nothing changed (a heroic
//                          no-op).
//   * blocked_by_daylight— stamina was below full but fewer than
//                          emergency_rest_hours of daylight remained, so nothing
//                          changed.
//   * no_provisions      — stamina was below full and daylight remained but no
//                          provision remained, so nothing changed.
enum class RestResult {
    recovered,
    already_full,
    blocked_by_daylight,
    no_provisions,
};

// The single event a rest command produces. The payload carries the typed
// outcome, the terrain rested on, the before/after stamina and provisions, and
// the before/after expedition time so a frontend can describe the rest, the
// recovery, the provision change, and the daylight spent without re-deriving any
// of them. Every rest still emits exactly one event and consumes exactly one
// contiguous sequence number like every other command.
struct RestedEvent {
    RestResult result{};
    Terrain terrain{};
    std::uint32_t stamina_before{};
    std::uint32_t stamina_after{};
    std::uint32_t stamina_recovered{};
    std::uint32_t provisions_before{};
    std::uint32_t provisions_after{};
    TimeUpdate time{};
};

// The typed outcome of a camp command (REQ-009 through REQ-014):
//   * camped       — an eligible, affordable camp or bivouac ended the day. `kind`
//                    distinguishes a normal camp from an emergency bivouac; the
//                    day, daylight, stamina, and provision fields bracket the
//                    change and `provision_cost` records what it consumed.
//   * ineligible   — no daylight hour had elapsed and stamina was full, so the
//                    camp was not yet allowed; nothing changed.
//   * no_provisions— the camp or bivouac was eligible but too few provisions
//                    remained to pay its cost; nothing changed.
enum class CampResult {
    camped,
    ineligible,
    no_provisions,
};

// The single event a camp command produces. It carries the typed result, the camp
// kind, the terrain camped on, the before/after expedition time, the before/after
// stamina and provisions, and the provision cost, so a frontend can describe the
// overnight without re-deriving any of it. `kind` is meaningful on every result
// so the frontend can distinguish an attempted bivouac from an attempted normal
// camp even when it failed. Camp never moves the actor, changes the map, or
// refreshes visibility, and always consumes exactly one sequence number.
struct CampedEvent {
    CampResult result{};
    CampKind kind{};
    Terrain terrain{};
    TimeUpdate time{};
    std::uint32_t stamina_before{};
    std::uint32_t stamina_after{};
    std::uint32_t provisions_before{};
    std::uint32_t provisions_after{};
    std::uint32_t provision_cost{};
};

// The payload of a GameEvent. A variant so command families can add their own
// event types without widening a single struct. Consumers must dispatch on the
// active alternative (std::get_if / std::visit) rather than assuming movement:
// the sequence type and default alternative stay MoveAttemptedEvent, but rest
// commands carry a RestedEvent and camp commands carry a CampedEvent instead.
using GameEventData = std::variant<MoveAttemptedEvent, RestedEvent, CampedEvent>;

// One ordered event. `sequence` starts at 0 for a new GameState and increases by
// exactly one per emitted event, so the stream is a total order in
// command-processing order. `ending` is the typed expedition-ending transition
// this command caused (none unless the command ended the run in a resource rescue
// or an overdue outcome), carried on the event so frontends can end the run
// without re-deriving the ending.
struct GameEvent {
    std::uint64_t sequence = 0;
    GameEventData data{};
    ExpeditionEndingTransition ending = ExpeditionEndingTransition::none;
};
