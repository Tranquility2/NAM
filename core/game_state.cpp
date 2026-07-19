#include "game_state.h"

#include <utility>

GameState::GameState(Map map)
    : map_(std::move(map)), actor_position_(map_.spawn()) {
    // map_ is declared before actor_position_, so it is fully constructed here
    // and map_.spawn() reads the moved-into member, not the moved-from argument.
}

MoveOutcome GameState::peek(Direction direction) const {
    const Coordinates from = actor_position_;
    const Coordinates target = from + direction_delta(direction);

    if (!map_.contains(target)) {
        return {MoveResult::blocked_by_boundary, from, from, map_.terrain_at(from)};
    }

    const Terrain destination = map_.terrain_at(target);
    if (!is_walkable(destination)) {
        return {MoveResult::blocked_by_terrain, from, from, destination};
    }

    return {MoveResult::moved, from, target, destination};
}

MoveOutcome GameState::move(Direction direction) {
    const MoveOutcome outcome = peek(direction);
    if (outcome.result == MoveResult::moved) {
        actor_position_ = outcome.to;
    }
    return outcome;
}

std::string GameState::render(char actor_glyph) const {
    return map_.to_string(actor_position_, actor_glyph);
}
