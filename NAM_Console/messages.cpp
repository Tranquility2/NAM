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
        case Terrain::open:            return "open ground";
        case Terrain::mountain:        return "mountain";
        case Terrain::water:           return "water";
        case Terrain::fields:          return "fields";
        case Terrain::hill:            return "hill";
        case Terrain::wall_horizontal: return "wall";
        case Terrain::wall_vertical:   return "wall";
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
        case MoveResult::moved: {
            std::string line = "Moved onto " + terrain_name(outcome.terrain) + " for " +
                               std::to_string(outcome.stamina_cost) + " stamina and " +
                               std::to_string(outcome.travel_hours) +
                               (outcome.travel_hours == 1 ? " hour." : " hours.");
            if (outcome.stamina_recovered > 0) {
                line += " Recovered " + std::to_string(outcome.stamina_recovered) + " stamina.";
            }
            return line;
        }
        case MoveResult::blocked_by_boundary:
            return "Blocked by the edge of the map.";
        case MoveResult::blocked_by_terrain:
            return "Blocked by " + terrain_name(outcome.terrain) + ".";
    }
    return "Nothing happened.";
}

std::string describe_rest(const RestedEvent& rested) {
    switch (rested.result) {
        case RestResult::recovered:
            return "Made emergency camp on " + terrain_name(rested.terrain) + " and recovered " +
                   std::to_string(rested.stamina_recovered) + " stamina. Provisions left: " +
                   std::to_string(rested.provisions_after) + ".";
        case RestResult::already_full:
            return "A heroic rest is attempted. Your stamina remains heroically full.";
        case RestResult::blocked_by_daylight:
            return "Too little daylight remains to rest. Make camp or press on.";
        case RestResult::no_provisions:
            return "No provisions left to rest. Stamina holds at " +
                   std::to_string(rested.stamina_after) + ".";
    }
    return "A heroic rest is attempted. Your stamina remains heroically full.";
}

std::string describe_camp(const CampedEvent& camped) {
    switch (camped.result) {
        case CampResult::camped:
            if (camped.kind == CampKind::bivouac) {
                return "Bivouacked on " + terrain_name(camped.terrain) + ". A rough night: stamina " +
                       std::to_string(camped.stamina_after) + ", provisions " +
                       std::to_string(camped.provisions_after) + ". Day " +
                       std::to_string(camped.time.after.day) + " begins.";
            }
            return "Made camp on " + terrain_name(camped.terrain) + ". Stamina restored to " +
                   std::to_string(camped.stamina_after) + ", provisions " +
                   std::to_string(camped.provisions_after) + ". Day " +
                   std::to_string(camped.time.after.day) + " begins.";
        case CampResult::ineligible:
            return "Too early to camp. Travel a while or tire first.";
        case CampResult::no_provisions:
            if (camped.kind == CampKind::bivouac) {
                return "Not enough provisions to bivouac here: need " +
                       std::to_string(camped.provision_cost) + ", have " +
                       std::to_string(camped.provisions_before) + ".";
            }
            return "Not enough provisions to make camp: need " +
                   std::to_string(camped.provision_cost) + ", have " +
                   std::to_string(camped.provisions_before) + ".";
    }
    return "Too early to camp. Travel a while or tire first.";
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

std::string objective_line(const BeaconObjective& objective) {
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

std::string goal_line(const BeaconObjective& objective) {
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

std::string describe_beacon_discovered(const std::string& name) {
    return "Reached " + name + ". Exit direction revealed.";
}

std::string describe_expedition_completed(const std::string& name) {
    return "Level complete: reached the exit after " + name + ".";
}

std::string describe_spawn_beacon(const std::string& name) {
    return "Level complete: " + name + " and the exit are at spawn.";
}

std::string discovery_reminder() {
    return "Landmark discovered. Press Enter or use a movement command to continue.";
}

std::string completion_reminder() {
    return "Run complete. Press Enter or q to exit.";
}

std::string restored_completion_message(const std::string& name) {
    return "Level complete beyond " + name + ".";
}

std::string restored_rescue_message(const std::string& name) {
    return "Rescued: the " + name + " expedition ran out of provisions and ended early.";
}

std::string restored_overdue_message(const std::string& name) {
    return "Overdue: the " + name + " route missed its level deadline and was collected late.";
}

}  // namespace nam::console
