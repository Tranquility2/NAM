#include "game_state.h"

#include <algorithm>
#include <optional>
#include <utility>

GameState::GameState(Map map)
    : map_(std::move(map)),
      actor_position_(map_.spawn()),
      visibility_(map_.width(), map_.height()) {
    // map_ is declared before actor_position_ and visibility_, so it is fully
    // constructed here and map_.spawn()/width()/height() read the moved-into
    // member, not the moved-from argument. Reveal the initial sight square once
    // the actor position and visibility buffer are initialized.
    visibility_.reveal_square(actor_position_, base_visibility_radius);
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
    if (outcome.result == MoveResult::moved) {
        actor_position_ = outcome.to;
        stamina_ = outcome.stamina_after;
        // Only a successful move refreshes visibility, and only after the actor
        // position and stamina are committed. Blocked attempts and peek leave
        // fog and stamina unchanged.
        visibility_.reveal_square(actor_position_, base_visibility_radius);
    }

    GameEvent event;
    event.sequence = next_event_sequence_;
    event.data = MoveAttemptedEvent{direction, outcome};
    ++next_event_sequence_;
    return event;
}

GameEvent GameState::rest() {
    // The invariant stamina_ <= maximum_stamina holds after every command, so the
    // unsigned subtraction that computes remaining capacity cannot underflow.
    const std::uint32_t stamina_before = stamina_;
    const std::uint32_t available = maximum_stamina - stamina_before;
    const std::uint32_t recovered = std::min(rest_recovery, available);
    stamina_ = stamina_before + recovered;

    // Rest never moves the actor or refreshes visibility, so map and fog are left
    // exactly as they were; only stamina changes.
    GameEvent event;
    event.sequence = next_event_sequence_;
    event.data = RestedEvent{stamina_before, recovered, stamina_};
    ++next_event_sequence_;
    return event;
}

std::string GameState::render(char actor_glyph) const {
    return map_.to_string(actor_position_, actor_glyph);
}
