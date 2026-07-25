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
                0, stamina_, stamina_};
    }

    const Terrain destination = map_.terrain_at(target);
    const std::optional<std::uint32_t> cost = stamina_cost_of(destination);
    if (!cost.has_value()) {
        return {MoveResult::blocked_by_terrain, from, from, destination, 0, stamina_, stamina_};
    }

    if (stamina_ < *cost) {
        return {MoveResult::blocked_by_stamina, from, from, destination, *cost, stamina_, stamina_};
    }

    // Affordability is established, so the unsigned subtraction cannot underflow.
    return {MoveResult::moved, from, target, destination, *cost, stamina_, stamina_ - *cost};
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
        // Only a successful move refreshes visibility, and only after the actor
        // position and stamina are committed. The radius is selected from the
        // destination terrain the actor now stands on, so elevated terrain
        // reveals farther. Blocked attempts and peek leave fog and stamina
        // unchanged.
        visibility_.reveal_square(actor_position_, visibility_radius());
        // Advance the objective only after position, stamina, and visibility have
        // committed, so a discovered beacon or completed return reflects the
        // fully updated state. Blocked attempts never advance the objective.
        objective_transition = advance_objective(objective_, actor_position_, map_.spawn());
    }

    GameEvent event;
    event.sequence = next_event_sequence_;
    event.data = MoveAttemptedEvent{direction, outcome,
                                    ObjectiveUpdate{objective_before, objective_.status,
                                                    objective_transition}};
    // Detect a rescue after the command is fully processed: completion always
    // takes precedence, so stranded() is false on the completing move.
    event.rescue = stranded() ? RescueTransition::stranded : RescueTransition::none;
    ++next_event_sequence_;
    return event;
}

bool GameState::stranded() const {
    if (objective_.status == ObjectiveStatus::completed) {
        return false;
    }
    if (provisions_ > 0) {
        return false;
    }
    // Stranded only when no in-bounds adjacent cardinal move is walkable and
    // affordable with the current stamina. peek() is pure, so this leaves all
    // state unchanged.
    for (const Direction direction : {Direction::up, Direction::down, Direction::left,
                                      Direction::right}) {
        if (peek(direction).result == MoveResult::moved) {
            return false;
        }
    }
    return true;
}

GameEvent GameState::rest() {
    const Terrain terrain = actor_terrain();
    const std::uint32_t stamina_before = stamina_;
    const std::uint32_t provisions_before = provisions_;

    RestResult result = RestResult::recovered;
    std::uint32_t recovered = 0;
    if (stamina_before >= maximum_stamina) {
        // A heroic no-op: stamina is already full, so no provision is spent.
        result = RestResult::already_full;
    } else if (provisions_ == 0) {
        // Below full but out of provisions: no resource state changes.
        result = RestResult::no_provisions;
    } else {
        // Below full with a provision to spend: recover the terrain amount capped
        // at the maximum. Every walkable terrain has a recovery value; the
        // fallback of 0 keeps the arithmetic total for a hypothetical wall cell.
        const std::optional<std::uint32_t> terrain_recovery = rest_recovery_of(terrain);
        const std::uint32_t available = maximum_stamina - stamina_before;
        recovered = std::min(terrain_recovery.value_or(0), available);
        stamina_ = stamina_before + recovered;
        --provisions_;
        result = RestResult::recovered;
    }

    // Rest never moves the actor or refreshes visibility, so map and fog are left
    // exactly as they were; only stamina and provisions can change.
    GameEvent event;
    event.sequence = next_event_sequence_;
    event.data = RestedEvent{result,       terrain,          stamina_before, stamina_,
                             recovered,     provisions_before, provisions_};
    event.rescue = stranded() ? RescueTransition::stranded : RescueTransition::none;
    ++next_event_sequence_;
    return event;
}

std::string GameState::render(char actor_glyph) const {
    return map_.to_string(actor_position_, actor_glyph);
}
