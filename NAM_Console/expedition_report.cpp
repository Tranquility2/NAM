#include "expedition_report.h"

#include <cstddef>
#include <variant>

#include "exploration.h"
#include "world_generation.h"

namespace nam::console {

namespace {

// The overlay glyphs used on the route map and named in the legend. Kept together
// so the map builder and the legend can never disagree.
constexpr char route_final_glyph = 'F';
constexpr char route_beacon_glyph = 'B';
constexpr char route_spawn_glyph = 'S';
constexpr char route_traveled_glyph = '*';
constexpr char route_unexplored_glyph = '?';

// Singular/plural helper for the story prose. `count` chooses the wording; the
// number itself is added by the caller.
[[nodiscard]] std::string plural(std::uint64_t count, const char* singular, const char* many) {
    return count == 1 ? std::string(singular) : std::string(many);
}

}  // namespace

void RouteHistory::record_event(const GameEvent& event) {
    if (const auto* move = std::get_if<MoveAttemptedEvent>(&event.data)) {
        if (move->outcome.result == MoveResult::moved) {
            cells_.push_back(move->outcome.to);
        }
    }
    // A blocked movement or any non-movement event appends nothing.
}

bool RouteHistory::traveled(Coordinates position) const noexcept {
    for (const Coordinates cell : cells_) {
        if (cell == position) {
            return true;
        }
    }
    return false;
}

WorldIdentity world_identity_from(const Settings& settings) {
    WorldIdentity identity;
    if (settings.map_path) {
        identity.source = WorldSource::file_map;
        identity.map_path = *settings.map_path;
    } else if (settings.seed_text) {
        identity.source = WorldSource::text_seed;
        identity.seed_text = *settings.seed_text;
        // A text seed carries its numeric hash identity; recompute it if the
        // caller has not resolved it yet so the two forms always agree.
        identity.numeric_seed =
            settings.numeric_seed.value_or(hash_seed_text(*settings.seed_text));
    } else if (settings.numeric_seed) {
        identity.source = WorldSource::numeric_seed;
        identity.numeric_seed = *settings.numeric_seed;
    } else {
        identity.source = WorldSource::built_in;
    }
    return identity;
}

ExpeditionReport build_expedition_report(
    const LevelObjective& objective, const Map& map, const VisibilityMap& visibility,
    const Journal& journal, const RouteHistory& route, const WorldIdentity& identity,
    std::uint64_t move_count, std::uint64_t attempt_count, std::uint32_t final_stamina,
    std::uint32_t max_stamina) {
    // Derive stamina spent from the completed structured journal only (REQ-143).
    std::uint64_t stamina_spent = 0;
    for (const JournalEntry& entry : journal.entries()) {
        if (const auto* travel = std::get_if<TravelEntry>(&entry.data)) {
            stamina_spent += travel->stamina_spent;
        }
    }

    // Blocked attempts are total movement attempts minus successful moves, via
    // comparison-before-subtraction so the count never underflows.
    const std::uint64_t blocked_attempts =
        attempt_count > move_count ? attempt_count - move_count : 0;

    // Whether the landmark was reached: any non-seeking status means the actor
    // entered the landmark at least once.
    const bool beacon_discovered = objective.status != ObjectiveStatus::seeking_landmark;

    // Core-owned exploration counts: explored from the map and fog snapshot, total
    // from the objective's reachable-cell property.
    const std::uint64_t explored =
        count_explored_reachable_walkable_cells(map, visibility);
    const std::uint64_t total = objective.total_reachable_walkable_cells;

    CompletedScoreInput score_input;
    score_input.optimal_route_cost = objective.minimum_route_stamina_cost;
    score_input.actual_stamina_spent = stamina_spent;
    score_input.blocked_attempts = blocked_attempts;

    ExpeditionReport report{map, visibility};
    report.beacon_name = objective.name;
    report.spawn = map.spawn();
    report.beacon = objective.beacon;
    report.final_position = route.final_position();
    report.route = route.cells();
    report.identity = identity;
    report.score = compute_completed_score(score_input);
    report.journal = journal;
    report.move_count = move_count;
    report.attempt_count = attempt_count;
    report.blocked_attempts = blocked_attempts;
    report.actual_stamina_spent = stamina_spent;
    report.optimal_route_cost = objective.minimum_route_stamina_cost;
    report.final_stamina = final_stamina;
    report.max_stamina = max_stamina;
    report.explored_reachable_cells = explored;
    report.total_reachable_cells = total;
    report.beacon_discovered = beacon_discovered;
    return report;
}

std::string format_report_result(const ExpeditionReport&) {
    return "Result: level complete.";
}

std::string format_report_story(const ExpeditionReport& report) {
    const std::uint64_t moves = report.move_count;
    const std::uint64_t stamina = report.actual_stamina_spent;
    const std::uint64_t blocked = report.blocked_attempts;
    return "The level beyond " + report.beacon_name + " is complete. The party made " +
           std::to_string(moves) + " successful " + plural(moves, "move", "moves") + ", spent " +
           std::to_string(stamina) + " stamina, and hit " + std::to_string(blocked) + " blocked " +
           plural(blocked, "move", "moves") + ", earning a final score of " +
           std::to_string(report.score.value) + " out of " +
           std::to_string(completed_score_maximum) + ".";
}

std::vector<std::string> format_report_statistics(const ExpeditionReport& report) {
    std::vector<std::string> lines;
    lines.emplace_back("STATISTICS");
    lines.push_back("Score: " + std::to_string(report.score.value) + " / " +
                    std::to_string(completed_score_maximum));
    lines.push_back("Moves: " + std::to_string(report.move_count));
    lines.push_back("Move attempts: " + std::to_string(report.attempt_count));
    lines.push_back("Blocked moves: " + std::to_string(report.blocked_attempts));
    lines.push_back("Stamina spent: " + std::to_string(report.actual_stamina_spent));
    lines.push_back("Optimal route cost: " + std::to_string(report.optimal_route_cost));
    lines.push_back("Final stamina: " + std::to_string(report.final_stamina) + "/" +
                    std::to_string(report.max_stamina));
    lines.push_back("Explored reachable terrain: " +
                    std::to_string(report.explored_reachable_cells) + " / " +
                    std::to_string(report.total_reachable_cells));
    lines.push_back(std::string("Landmark reached: ") + (report.beacon_discovered ? "yes" : "no"));
    return lines;
}

std::vector<std::string> format_report_identity(const ExpeditionReport& report) {
    const WorldIdentity& identity = report.identity;
    std::vector<std::string> lines;
    lines.emplace_back("WORLD");
    switch (identity.source) {
        case WorldSource::text_seed:
            lines.emplace_back("Source: Tiny World (text seed)");
            lines.push_back("Seed text: " + format_seed_for_display(identity.seed_text));
            lines.push_back("Seed number: " + std::to_string(identity.numeric_seed));
            lines.push_back("Replay: nam_console --seed-number " +
                            std::to_string(identity.numeric_seed));
            return lines;
        case WorldSource::numeric_seed:
            lines.emplace_back("Source: Tiny World (numeric seed)");
            lines.push_back("Seed number: " + std::to_string(identity.numeric_seed));
            lines.push_back("Replay: nam_console --seed-number " +
                            std::to_string(identity.numeric_seed));
            return lines;
        case WorldSource::file_map:
            lines.emplace_back("Source: map file");
            lines.push_back("Map: " + format_seed_for_display(identity.map_path));
            lines.push_back("Run again: nam_console --map " +
                            format_seed_for_display(identity.map_path));
            return lines;
        case WorldSource::built_in:
            lines.emplace_back("Source: built-in map");
            lines.emplace_back("Run again: nam_console");
            return lines;
    }
    // Exhaustive switch above; the fallback keeps the function total for every
    // compiler in the portability baseline.
    lines.emplace_back("Source: built-in map");
    lines.emplace_back("Run again: nam_console");
    return lines;
}

std::vector<std::string> format_report_route_map(const ExpeditionReport& report) {
    const Map& map = report.map;
    const int width = static_cast<int>(map.width());
    const int height = static_cast<int>(map.height());

    // A traveled mask over the full map so per-cell overlay lookup stays O(1).
    std::vector<bool> traveled(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                               false);
    for (const Coordinates cell : report.route) {
        if (cell.x >= 0 && cell.x < width && cell.y >= 0 && cell.y < height) {
            traveled[static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(cell.x)] = true;
        }
    }

    std::vector<std::string> lines;
    lines.emplace_back("ROUTE MAP");
    for (int y = 0; y < height; ++y) {
        std::string row;
        row.reserve(static_cast<std::size_t>(width));
        for (int x = 0; x < width; ++x) {
            const Coordinates cell{x, y};
            // Overlay priority: final position, exit, spawn, any traveled cell,
            // then fog/terrain (REQ-153).
            if (cell == report.final_position) {
                row.push_back(route_final_glyph);
            } else if (cell == report.beacon) {
                row.push_back(route_beacon_glyph);
            } else if (cell == report.spawn) {
                row.push_back(route_spawn_glyph);
            } else if (traveled[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(x)]) {
                row.push_back(route_traveled_glyph);
            } else if (report.visibility.at(cell) == CellVisibility::unexplored) {
                row.push_back(route_unexplored_glyph);
            } else {
                row.push_back(symbol_of(map.terrain_at(cell)));
            }
        }
        lines.push_back(std::move(row));
    }
    return lines;
}

std::vector<std::string> format_report_legend() {
    return {std::string("ROUTE LEGEND"),
            std::string(1, route_final_glyph) + " = final position",
            std::string(1, route_beacon_glyph) + " = exit",
            std::string(1, route_spawn_glyph) + " = spawn",
            std::string(1, route_traveled_glyph) + " = traveled cell",
            std::string(1, route_unexplored_glyph) + " = unexplored"};
}

std::vector<std::string> format_report_lines(const ExpeditionReport& report) {
    std::vector<std::string> lines;
    const auto append = [&lines](const std::vector<std::string>& section) {
        lines.insert(lines.end(), section.begin(), section.end());
    };

    lines.emplace_back("EXPEDITION REPORT");
    lines.emplace_back();
    lines.push_back(format_report_result(report));
    lines.push_back(format_report_story(report));
    lines.emplace_back();
    append(format_report_statistics(report));
    lines.emplace_back();
    append(format_report_identity(report));
    lines.emplace_back();
    append(format_report_route_map(report));
    lines.emplace_back();
    append(format_report_legend());
    lines.emplace_back();

    lines.emplace_back("EXPEDITION JOURNAL");
    const std::vector<JournalEntry>& entries = report.journal.entries();
    if (entries.empty()) {
        lines.emplace_back("(No journal entries yet.)");
    } else {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            lines.push_back(std::to_string(i + 1) + ". " + format_entry(entries[i]));
        }
    }
    return lines;
}

}  // namespace nam::console
