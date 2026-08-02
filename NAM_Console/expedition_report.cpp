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
constexpr char route_exit_glyph = 'X';
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
    std::uint64_t move_count, std::uint64_t attempt_count,
    const ExpeditionCarryover& carryover) {
    // Blocked attempts are total movement attempts minus successful moves, via
    // comparison-before-subtraction so the count never underflows.
    const std::uint64_t blocked_attempts =
        attempt_count > move_count ? attempt_count - move_count : 0;

    // Core-owned exploration counts: explored from the map and fog snapshot, total
    // from the objective's reachable-cell property.
    const std::uint64_t explored =
        count_explored_reachable_walkable_cells(map, visibility);
    const std::uint64_t total = objective.total_reachable_walkable_cells;

    CompletedScoreInput score_input;
    score_input.explored_reachable_cells = explored;
    score_input.total_reachable_cells = total;
    score_input.actual_moves = move_count;
    score_input.discoveries_found = carryover.discoveries_found;
    score_input.discovery_multiplier = discovery_multiplier_of(carryover.applied_bonus);

    ExpeditionReport report{map, visibility};
    report.landmark_name = objective.name;
    report.spawn = map.spawn();
    report.exit_cell = objective.exit_cell;
    report.final_position = route.final_position();
    report.route = route.cells();
    report.identity = identity;
    report.score = compute_completed_score(score_input);
    report.journal = journal;
    report.move_count = move_count;
    report.blocked_attempts = blocked_attempts;
    report.explored_reachable_cells = explored;
    report.total_reachable_cells = total;
    report.carryover = carryover;
    // A standalone level is its own whole expedition, so its running total is
    // simply this level's score rather than a separately tracked zero.
    if (report.carryover.expedition_score == 0 && report.carryover.total_levels <= 1u) {
        report.carryover.expedition_score = report.score.value;
        report.carryover.expedition_discoveries_found = carryover.discoveries_found;
        report.carryover.expedition_discoveries_available = carryover.discovery_total;
    }
    return report;
}

std::string format_report_result(const ExpeditionReport& report) {
    if (report.carryover.total_levels <= 1u) {
        return "Result: level complete.";
    }
    if (report.carryover.expedition_completed) {
        return "Result: expedition complete. All " +
               std::to_string(report.carryover.total_levels) + " levels finished.";
    }
    return "Result: " + std::string(to_string(report.carryover.next_tier
                                                  ? *report.carryover.next_tier
                                                  : LevelTier::small)) +
           " level ahead. " + std::to_string(report.carryover.levels_completed) + " of " +
           std::to_string(report.carryover.total_levels) + " complete.";
}

std::string format_report_story(const ExpeditionReport& report) {
    const std::uint64_t moves = report.move_count;
    return "The party found " + report.landmark_name + " and reached the exit in " +
           std::to_string(moves) + " " + plural(moves, "move", "moves") + ".";
}

std::vector<std::string> format_report_statistics(const ExpeditionReport& report) {
    std::vector<std::string> lines;
    lines.emplace_back("STATISTICS");
    lines.push_back("Score: " + std::to_string(report.score.value) + " (exit " +
                    std::to_string(report.score.completion_value) + ", explored " +
                    std::to_string(report.score.exploration_value) + " / " +
                    std::to_string(completed_exploration_maximum) + ", discoveries " +
                    std::to_string(report.score.discovery_value) + ", budget " +
                    std::to_string(report.score.budget_value) + ")");
    lines.push_back("Discoveries: " + std::to_string(report.carryover.discoveries_found) + " / " +
                    std::to_string(report.carryover.discovery_total));
    lines.push_back("Explored: " + std::to_string(report.explored_reachable_cells) + " / " +
                    std::to_string(report.total_reachable_cells) + " cells (" +
                    std::to_string(report.score.explored_percent) + "%)");
    lines.push_back("Moves: " + std::to_string(report.move_count) + " " +
                    plural(report.move_count, "move", "moves") + " of " +
                    std::to_string(report.score.move_budget) + " budgeted, " +
                    std::to_string(report.blocked_attempts) + " blocked");
    return lines;
}

std::vector<std::string> format_report_expedition(const ExpeditionReport& report) {
    // A standalone level is its own whole expedition, so repeating its figures
    // under a second heading would say nothing new.
    if (report.carryover.total_levels <= 1u) return {};

    std::vector<std::string> lines;
    lines.emplace_back("EXPEDITION");
    lines.push_back("Score: " + std::to_string(report.carryover.expedition_score) + " over " +
                    std::to_string(report.carryover.levels_completed) + " of " +
                    std::to_string(report.carryover.total_levels) + " levels");
    lines.push_back("Discoveries: " +
                    std::to_string(report.carryover.expedition_discoveries_found) + " / " +
                    std::to_string(report.carryover.expedition_discoveries_available));
    if (report.carryover.applied_bonus != ExpeditionBonus::none) {
        lines.emplace_back("Bonus spent: keen eye doubled this level's discoveries.");
    }
    // A bonus is only worth announcing while there is a next level to carry it to.
    if (!report.carryover.expedition_completed &&
        report.carryover.earned_bonus != ExpeditionBonus::none) {
        lines.emplace_back("Bonus earned: keen eye. The next level's discoveries are worth double.");
    }
    return lines;
}

std::vector<std::string> format_report_identity(const ExpeditionReport& report) {
    const WorldIdentity& identity = report.identity;
    std::vector<std::string> lines;
    lines.emplace_back("WORLD");
    switch (identity.source) {
        case WorldSource::text_seed:
            lines.emplace_back("Source: expedition (text seed)");
            lines.push_back("Seed text: " + format_seed_for_display(identity.seed_text));
            lines.push_back("Seed number: " + std::to_string(identity.numeric_seed));
            lines.push_back("Replay: nam_console --seed-number " +
                            std::to_string(identity.numeric_seed));
            return lines;
        case WorldSource::numeric_seed:
            lines.emplace_back("Source: expedition (numeric seed)");
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
            } else if (cell == report.exit_cell) {
                row.push_back(route_exit_glyph);
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
            std::string(1, route_exit_glyph) + " = exit",
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
    const std::vector<std::string> expedition = format_report_expedition(report);
    if (!expedition.empty()) {
        append(expedition);
        lines.emplace_back();
    }
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
