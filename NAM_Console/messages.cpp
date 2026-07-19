#include "messages.h"

namespace nam::console {

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
        case MoveResult::moved:
            return "Moved onto " + terrain_name(outcome.terrain) + ".";
        case MoveResult::blocked_by_boundary:
            return "Blocked by the edge of the map.";
        case MoveResult::blocked_by_terrain:
            return "Blocked by " + terrain_name(outcome.terrain) + ".";
    }
    return "Nothing happened.";
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

}  // namespace nam::console
