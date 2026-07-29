#include "game_state.h"

#include <algorithm>
#include <optional>
#include <utility>

GameState::GameState(Map map)
    : map_(std::move(map)),
      objective_(create_beacon_objective(map_, maximum_stamina)),
      actor_position_(map_.spawn()),
      visibility_(map_.width(), map_.height()) {
    // map_ is declared before objective_, actor_position_, and visibility_, so it
    // is fully constructed here and map_.spawn()/width()/height() and the beacon
    // placement read the moved-into member, not the moved-from argument. Reveal
    // the initial sight square once the actor position and visibility buffer are
    // initialized, using the radius for the spawn terrain so an initial hill or
    // mountain sees farther at once.
    visibility_.reveal_square(actor_position_, visibility_radius());
    // Start with the objective's deterministic minimum required provisions plus
    // one spare, so a feasible expedition always has a margin. The clamp to
    // uint32 is exact for the small provision magnitudes involved.
    starting_provisions_ =
        static_cast<std::uint32_t>(objective_.minimum_required_provisions) + 1u;
    provisions_ = starting_provisions_;
}

MoveOutcome GameState::peek(Direction direction) const {
    const Coordinates from = actor_position_;
    const Coordinates target = from + direction_delta(direction);

    if (!map_.contains(target)) {
        return {MoveResult::blocked_by_boundary, from, from, map_.terrain_at(from),
                0, stamina_, stamina_, 0, time_, time_};
    }

    const Terrain destination = map_.terrain_at(target);
    const std::optional<std::uint32_t> cost = stamina_cost_of(destination);
    const std::optional<std::uint32_t> hours = travel_hours_of(destination);
    if (!cost.has_value() || !hours.has_value()) {
        return {MoveResult::blocked_by_terrain, from, from, destination,
                0, stamina_, stamina_, 0, time_, time_};
    }

    // Daylight is validated before stamina (REQ-004): when both are insufficient
    // the result is blocked_by_daylight. The walkable-destination results carry the
    // stamina cost and travel hours so a frontend never re-derives them.
    if (!time_.fits(*hours)) {
        return {MoveResult::blocked_by_daylight, from, from, destination,
                *cost, stamina_, stamina_, *hours, time_, time_};
    }

    if (stamina_ < *cost) {
        return {MoveResult::blocked_by_stamina, from, from, destination,
                *cost, stamina_, stamina_, *hours, time_, time_};
    }

    // Affordability is established, so the unsigned subtraction cannot underflow.
    // A successful move advances daylight by the destination terrain's travel
    // hours before objective and ending transitions are evaluated (REQ-005).
    ExpeditionTime time_after = time_;
    time_after.daylight_hours_used += *hours;
    return {MoveResult::moved, from, target, destination,
            *cost, stamina_, stamina_ - *cost, *hours, time_, time_after};
}

GameEvent GameState::move(Direction direction) {
    // peek remains the single source of movement outcomes; move only commits the
    // result and wraps it in an ordered event.
    const MoveOutcome outcome = peek(direction);

    // Capture the objective status before any commit so the emitted event brackets
    // the exact objective state around this command.
    const ObjectiveStatus objective_before = objective_.status;
    ObjectiveTransition objective_transition = ObjectiveTransition::none;

    if (outcome.result == MoveResult::moved) {
        actor_position_ = outcome.to;
        stamina_ = outcome.stamina_after;
        time_ = outcome.time_after;
        // Only a successful move refreshes visibility, and only after the actor
        // position, stamina, and daylight are committed. The radius is selected
        // from the destination terrain the actor now stands on, so elevated
        // terrain reveals farther. Blocked attempts and peek leave fog, stamina,
        // and time unchanged.
        visibility_.reveal_square(actor_position_, visibility_radius());
        // Advance the objective only after position, stamina, time, and visibility
        // have committed, so a discovered beacon or completed return reflects the
        // fully updated state. Blocked attempts never advance the objective.
        objective_transition = advance_objective(objective_, actor_position_);
    }

    GameEvent event;
    event.sequence = next_event_sequence_;
    event.data = MoveAttemptedEvent{direction, outcome,
                                    ObjectiveUpdate{objective_before, objective_.status,
                                                    objective_transition}};
    // Evaluate the ending after the command is fully processed: completion always
    // takes precedence, so evaluate_ending() returns none on the completing move.
    event.ending = evaluate_ending();
    ++next_event_sequence_;
    return event;
}

GameEvent GameState::rest() {
    const Terrain terrain = actor_terrain();
    const std::uint32_t stamina_before = stamina_;
    const std::uint32_t provisions_before = provisions_;
    const ExpeditionTime time_before = time_;

    RestResult result = RestResult::recovered;
    std::uint32_t recovered = 0;
    if (stamina_before >= maximum_stamina) {
        // A heroic no-op: stamina is already full, so nothing changes.
        result = RestResult::already_full;
    } else if (!time_.fits(emergency_rest_hours)) {
        // Below full but too little daylight remains for a rest: nothing changes.
        result = RestResult::blocked_by_daylight;
    } else if (provisions_ == 0) {
        // Below full with daylight but out of provisions: nothing changes.
        result = RestResult::no_provisions;
    } else {
        // Below full with daylight and a provision to spend: recover the terrain
        // amount capped at the maximum and consume the rest's daylight. Every
        // walkable terrain has a recovery value; the fallback of 0 keeps the
        // arithmetic total for a hypothetical wall cell.
        const std::optional<std::uint32_t> terrain_recovery = rest_recovery_of(terrain);
        const std::uint32_t available = maximum_stamina - stamina_before;
        recovered = std::min(terrain_recovery.value_or(0), available);
        stamina_ = stamina_before + recovered;
        --provisions_;
        time_.daylight_hours_used += emergency_rest_hours;
        result = RestResult::recovered;
    }

    // Rest never moves the actor or refreshes visibility, so map and fog are left
    // exactly as they were; only stamina, provisions, and daylight can change.
    GameEvent event;
    event.sequence = next_event_sequence_;
    event.data = RestedEvent{result,          terrain,           stamina_before, stamina_,
                             recovered,        provisions_before, provisions_,
                             TimeUpdate{time_before, time_}};
    event.ending = evaluate_ending();
    ++next_event_sequence_;
    return event;
}

GameEvent GameState::camp() {
    const Terrain terrain = actor_terrain();
    const CampKind kind = camp_kind_of(terrain);
    const std::uint32_t provision_cost = camp_provision_cost_of(kind);
    const std::uint32_t stamina_before = stamina_;
    const std::uint32_t provisions_before = provisions_;
    const ExpeditionTime time_before = time_;

    // A camp is eligible only after at least one daylight hour has elapsed this day
    // or stamina is below the cap (REQ-010).
    const bool eligible = time_.daylight_hours_used >= 1u || stamina_ < maximum_stamina;

    CampResult result = CampResult::camped;
    if (!eligible) {
        result = CampResult::ineligible;
    } else if (provisions_ < provision_cost) {
        result = CampResult::no_provisions;
    } else {
        // A successful camp or bivouac spends its provisions, applies its stamina
        // result, and ends the day. On the deadline day the stored day stays at the
        // deadline with the day fully spent, so no fictitious day beyond the
        // deadline is ever exposed (REQ-023); the ending evaluator then reports
        // overdue. On any earlier day the next day begins at zero daylight used.
        provisions_ -= provision_cost;
        stamina_ = camp_stamina_result_of(kind, maximum_stamina);
        if (time_.day >= objective_.deadline_days) {
            time_.day = objective_.deadline_days;
            time_.daylight_hours_used = time_.daylight_hours_per_day;
        } else {
            time_.day += 1u;
            time_.daylight_hours_used = 0u;
        }
        result = CampResult::camped;
    }

    // Camp never moves the actor or refreshes visibility.
    GameEvent event;
    event.sequence = next_event_sequence_;
    event.data = CampedEvent{result,     kind,           terrain, TimeUpdate{time_before, time_},
                             stamina_before, stamina_,    provisions_before, provisions_,
                             provision_cost};
    event.ending = evaluate_ending();
    ++next_event_sequence_;
    return event;
}

bool GameState::has_move_continuation() const {
    for (const Direction direction :
         {Direction::up, Direction::down, Direction::left, Direction::right}) {
        if (peek(direction).result == MoveResult::moved) {
            return true;
        }
    }
    return false;
}

bool GameState::has_rest_continuation() const {
    return stamina_ < maximum_stamina && time_.fits(emergency_rest_hours) && provisions_ > 0u;
}

bool GameState::has_camp_continuation() const {
    const bool eligible = time_.daylight_hours_used >= 1u || stamina_ < maximum_stamina;
    if (!eligible) {
        return false;
    }
    const CampKind kind = camp_kind_of(actor_terrain());
    return provisions_ >= camp_provision_cost_of(kind);
}

ExpeditionEndingTransition GameState::evaluate_ending() const {
    // Completion always takes precedence over every failure (REQ-020).
    if (objective_.status == ObjectiveStatus::completed) {
        return ExpeditionEndingTransition::none;
    }

    // On the final allowed day, only a successful move or emergency rest can
    // continue: camping would end the day and still finish overdue. Declare overdue
    // when the day is exhausted or no move/rest continuation fits (REQ-021 /
    // REQ-022).
    if (time_.day >= objective_.deadline_days) {
        if (time_.day_exhausted() || (!has_move_continuation() && !has_rest_continuation())) {
            return ExpeditionEndingTransition::overdue;
        }
        return ExpeditionEndingTransition::none;
    }

    // On earlier days, declare resource rescue when no successful move, emergency
    // rest, or eligible affordable camp/bivouac remains (REQ-024). This includes
    // being stuck on water or mountain with fewer than two provisions.
    if (!has_move_continuation() && !has_rest_continuation() && !has_camp_continuation()) {
        return ExpeditionEndingTransition::rescued;
    }
    return ExpeditionEndingTransition::none;
}

std::string GameState::render(char actor_glyph) const {
    return map_.to_string(actor_position_, actor_glyph);
}
