#include "game_state.h"

#include <algorithm>
#include <optional>
#include <utility>

GameState::GameState(Map map)
    : map_(std::move(map)),
      objective_(create_level_objective(map_)),
      actor_position_(map_.spawn()),
      visibility_(map_.width(), map_.height()) {
    // map_ is declared before objective_, actor_position_, and visibility_, so it
    // is fully constructed here and map_.spawn()/width()/height() and the exit
    // placement read the moved-into member, not the moved-from argument. Reveal
    // the initial sight square once the actor position and visibility buffer are
    // initialized, using the radius for the spawn terrain so an initial hill or
    // mountain sees farther at once.
    visibility_.reveal_square(actor_position_, visibility_radius());

    feature_resolved_.assign(features().size(), false);
    for (const LevelFeature& feature : features()) {
        if (feature.kind == LevelFeatureKind::discovery) {
            ++discovery_total_;
        }
    }
}

std::optional<LevelFeatureKind> GameState::feature_at(Coordinates position) const {
    for (const LevelFeature& feature : features()) {
        if (feature.position == position) {
            return feature.kind;
        }
    }
    return std::nullopt;
}

MoveOutcome GameState::peek(Direction direction) const {
    const Coordinates from = actor_position_;
    const Coordinates target = from + direction_delta(direction);

    if (!map_.contains(target)) {
        return {MoveResult::blocked_by_boundary, from, from, map_.terrain_at(from), std::nullopt};
    }

    const Terrain destination = map_.terrain_at(target);
    if (!is_walkable(destination)) {
        return {MoveResult::blocked_by_terrain, from, from, destination, std::nullopt};
    }

    // Movement is fluid: an in-bounds walkable destination always succeeds. No
    // meter gates a step, so terrain shapes a route through the sight it grants
    // rather than through a cost it charges.
    return {MoveResult::moved, from, target, destination, feature_at(target)};
}

GameEvent GameState::move(Direction direction) {
    // peek remains the single source of movement outcomes; move only commits the
    // result and wraps it in an ordered event.
    const MoveOutcome outcome = peek(direction);

    // Capture the objective status before any commit so the emitted event brackets
    // the exact objective state around this command.
    const ObjectiveStatus objective_before = objective_.status;
    ObjectiveTransition objective_transition = ObjectiveTransition::none;
    bool discovery_recorded = false;
    bool wide_reveal_granted = false;

    if (outcome.result == MoveResult::moved) {
        actor_position_ = outcome.to;
        // Only a successful move refreshes visibility, and only after the actor
        // position is committed. The radius is selected from the destination
        // terrain the actor now stands on, so elevated terrain reveals farther.
        // Blocked attempts and peek leave fog unchanged.
        visibility_.reveal_square(actor_position_, visibility_radius());
        // Advance the objective only after position and visibility have committed,
        // so a discovered landmark or completed level reflects the fully updated
        // state. Blocked attempts never advance the objective.
        objective_transition = advance_objective(objective_, actor_position_);
        if (objective_transition == ObjectiveTransition::landmark_discovered) {
            wide_reveal_granted = true;
        }

        // Resolve the authored content on the destination. Both kinds fire exactly
        // once and are then inert, which is the only feature state the core keeps.
        // Resolution is committed after movement, so a recorded discovery or a
        // fired vantage point always describes a position the actor holds.
        const std::vector<LevelFeature>& placed = features();
        for (std::size_t index = 0; index < placed.size(); ++index) {
            if (placed[index].position != actor_position_ || feature_resolved_[index]) {
                continue;
            }
            feature_resolved_[index] = true;
            switch (placed[index].kind) {
                case LevelFeatureKind::discovery:
                    ++discoveries_found_;
                    discovery_recorded = true;
                    break;
                case LevelFeatureKind::vantage_point:
                    wide_reveal_granted = true;
                    break;
            }
        }

        // A wide reveal is applied last, over the terrain square already revealed.
        // reveal_square demotes what it does not cover, and the wide square always
        // contains the terrain square, so the second call widens sight without
        // forgetting anything the first call showed.
        if (wide_reveal_granted) {
            visibility_.reveal_square(actor_position_, wide_reveal_radius);
        }
    }

    GameEvent event;
    event.sequence = next_event_sequence_;
    event.data = MoveAttemptedEvent{direction, outcome,
                                    ObjectiveUpdate{objective_before, objective_.status,
                                                    objective_transition},
                                    discovery_recorded, wide_reveal_granted};
    ++next_event_sequence_;
    return event;
}

std::string GameState::render(char actor_glyph) const {
    return map_.to_string(actor_position_, actor_glyph);
}
