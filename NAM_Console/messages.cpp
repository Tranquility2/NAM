#include "messages.h"

namespace nam::console {

namespace {

[[nodiscard]] std::string cardinal_direction_name(Direction direction) {
    switch (direction) {
        case Direction::up:    return "north";
        case Direction::down:  return "south";
        case Direction::left:  return "west";
        case Direction::right: return "east";
    }
    return "north";
}

}  // namespace

std::string terrain_name(Terrain terrain) {
    switch (terrain) {
        case Terrain::open:          return "open ground";
        case Terrain::mountain:      return "mountain";
        case Terrain::shallow_water: return "shallow water";
        case Terrain::fields:        return "fields";
        case Terrain::hill:          return "hill";
        case Terrain::forest:        return "forest";
        case Terrain::deep_water:    return "deep water";
        case Terrain::cliff:         return "cliff";
    }
    return "unknown";
}

char direction_letter(Direction direction) noexcept {
    switch (direction) {
        case Direction::up:    return 'U';
        case Direction::down:  return 'D';
        case Direction::left:  return 'L';
        case Direction::right: return 'R';
    }
    return '?';
}

std::string direction_name(Direction direction) {
    switch (direction) {
        case Direction::up:    return "up";
        case Direction::down:  return "down";
        case Direction::left:  return "left";
        case Direction::right: return "right";
    }
    return "?";
}

std::string describe_move(const MoveOutcome& outcome) {
    switch (outcome.result) {
        case MoveResult::moved:
            return "Moved onto " + terrain_name(outcome.terrain) + ". Sight " +
                   std::to_string(visibility_radius_of(outcome.terrain)) + ".";
        case MoveResult::blocked_by_boundary:
            return "Blocked by the edge of the map.";
        case MoveResult::blocked_by_terrain:
            return "Blocked by " + terrain_name(outcome.terrain) + ".";
    }
    return "Nothing happened.";
}

std::string describe_discovery_found(std::uint32_t found, std::uint32_t total) {
    return "Found a discovery (" + std::to_string(found) + " of " + std::to_string(total) + ").";
}

std::string describe_vantage_reached(VantageKind kind) {
    switch (kind) {
        case VantageKind::cairn:
            return "Reached a cairn; the nearby land came into view.";
        case VantageKind::lookout:
            return "Climbed a lookout; the surrounding land came into view.";
        case VantageKind::summit:
            return "Gained the summit; the whole valley came into view.";
    }
    return "Reached a vantage point; the surrounding land came into view.";
}

std::string describe_map_error(const MapLoadError& error) {
    std::string text = "Could not load map";
    if (!error.source.empty()) {
        text += " '" + error.source + "'";
    }
    if (error.line != 0) {
        text += " at line " + std::to_string(error.line);
        if (error.column != 0) {
            text += ", column " + std::to_string(error.column);
        }
    }
    text += ": ";
    text += error.message.empty() ? std::string(to_string(error.code)) : error.message;
    text += '.';
    return text;
}

std::string objective_line(const LevelObjective& objective) {
    switch (objective.status) {
        case ObjectiveStatus::seeking_landmark:
            return "Objective: Reach " + objective.name + " (*).";
        case ObjectiveStatus::seeking_exit:
            return "Objective: Reach the exit to the " +
                   cardinal_direction_name(objective.exit_bearing) + " (*).";
        case ObjectiveStatus::completed:
            return "Level complete: reached the exit beyond " + objective.name + ".";
    }
    return "Objective: Reach " + objective.name + " (*).";
}

std::string goal_line(const LevelObjective& objective) {
    switch (objective.status) {
        case ObjectiveStatus::seeking_landmark:
            return "Goal: reach " + objective.name;
        case ObjectiveStatus::seeking_exit:
            return "Goal: exit " + cardinal_direction_name(objective.exit_bearing);
        case ObjectiveStatus::completed:
            return "Goal: complete";
    }
    return "Goal: reach " + objective.name;
}

std::string describe_landmark_discovered(const std::string& name) {
    return "Reached " + name + ". Exit direction revealed.";
}

std::string describe_level_completed(const std::string& name) {
    return "Level complete: reached the exit after " + name + ".";
}

std::string describe_spawn_landmark(const std::string& name) {
    return "Level complete: " + name + " and the exit are at spawn.";
}

std::string discovery_reminder() {
    return "Landmark discovered. Press Enter or use a movement command to continue.";
}

std::string completion_reminder() {
    return "Run complete. Press Enter or q to exit.";
}

std::string describe_level_started(LevelTier tier, std::uint32_t level_number,
                                   std::uint32_t total_levels, ExpeditionBonus bonus) {
    std::string text = std::string(to_string(tier)) + " level (" +
                       std::to_string(level_number) + " of " + std::to_string(total_levels) + ").";
    if (bonus == ExpeditionBonus::keen_eye) {
        text += " Keen eye: discoveries here are worth double.";
    }
    return text;
}

std::string restored_completion_message(const std::string& name) {
    return "Level complete beyond " + name + ".";
}

}  // namespace nam::console
