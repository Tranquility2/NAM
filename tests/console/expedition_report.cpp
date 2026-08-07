#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "expedition.h"
#include "expedition_report.h"
#include "game_event.h"
#include "journal.h"
#include "level_tier.h"
#include "map.h"
#include "move_outcome.h"
#include "objective.h"
#include "renderer.h"
#include "settings.h"
#include "terrain.h"
#include "visibility.h"

using namespace nam::console;

namespace {

// A single-level journal context: the landmark name is all these cases need.
JournalContext ctx(const std::string& landmark_name) {
    JournalContext context;
    context.landmark_name = landmark_name;
    return context;
}

GameEvent moved_event(std::uint64_t sequence, Direction direction, Terrain terrain,
                      Coordinates to) {
    MoveAttemptedEvent move;
    move.direction = direction;
    move.outcome.result = MoveResult::moved;
    move.outcome.terrain = terrain;
    move.outcome.to = to;
    return GameEvent{sequence, move};
}

GameEvent blocked_event(std::uint64_t sequence, Direction direction) {
    MoveAttemptedEvent move;
    move.direction = direction;
    move.outcome.result = MoveResult::blocked_by_terrain;
    return GameEvent{sequence, move};
}

Map row_map(const std::string& glyphs, Coordinates spawn) {
    std::vector<Terrain> cells;
    cells.reserve(glyphs.size());
    for (const char glyph : glyphs) {
        cells.push_back(*terrain_from_symbol(glyph));
    }
    return Map(glyphs.size(), 1, std::move(cells), spawn);
}

LevelObjective make_objective(Coordinates exit_cell, std::string name, ObjectiveStatus status,
                              std::uint64_t route_length, std::uint64_t reachable_cells) {
    LevelObjective objective;
    objective.exit_cell = exit_cell;
    objective.name = std::move(name);
    objective.status = status;
    objective.minimum_route_length = route_length;
    objective.total_reachable_walkable_cells = reachable_cells;
    return objective;
}

// The report's logical lines as one searchable block, matching how both the
// plain renderer and a reader see them.
std::string join_lines(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line;
        text.push_back('\n');
    }
    return text;
}

std::string route_map_first_row(const ExpeditionReport& report) {
    const std::vector<std::string> lines = format_report_lines(report);
    for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
        if (lines[i] == "ROUTE MAP") {
            return lines[i + 1];
        }
    }
    return std::string();
}

ExpeditionReport build_report(const LevelObjective& objective, const Map& map,
                              const VisibilityMap& visibility, const Journal& journal,
                              const RouteHistory& route, std::uint64_t move_count,
                              std::uint64_t attempt_count) {
    return build_expedition_report(objective, map, visibility, journal, route,
                                   world_identity_from(Settings{}), move_count, attempt_count);
}

// A level whose only variable is the bonus that was in force, so a case can vary
// the bonus and read back exactly what the report says about it.
ExpeditionReport bonus_report(ExpeditionBonus applied, ExpeditionBonus earned,
                              std::uint64_t move_count) {
    const Map map = row_map("......", Coordinates{0, 0});
    const LevelObjective objective =
        make_objective(Coordinates{5, 0}, "Glass River Exit", ObjectiveStatus::completed, 5, 6);
    VisibilityMap visibility(6, 1);
    visibility.reveal_square(Coordinates{2, 0}, 8);
    Journal journal;
    RouteHistory route(Coordinates{0, 0});

    ExpeditionCarryover carryover;
    carryover.levels_completed = 1;
    carryover.total_levels = 2;
    carryover.expedition_completed = false;
    carryover.next_tier = LevelTier::medium;
    carryover.discoveries_found = 2;
    carryover.discovery_total = 2;
    carryover.applied_bonus = applied;
    carryover.earned_bonus = earned;

    return build_expedition_report(objective, map, visibility, journal, route,
                                   world_identity_from(Settings{}), move_count, move_count,
                                   carryover);
}

}  // namespace

TEST_SUITE("expedition_report") {

TEST_CASE("route history starts at spawn and appends only successful destinations") {
    RouteHistory route(Coordinates{0, 0});
    route.record_event(moved_event(0, Direction::right, Terrain::open, Coordinates{1, 0}));
    route.record_event(blocked_event(1, Direction::right));
    route.record_event(moved_event(2, Direction::right, Terrain::open, Coordinates{2, 0}));

    const std::vector<Coordinates>& cells = route.cells();
    REQUIRE(cells.size() == 3);
    CHECK(cells[0] == Coordinates{0, 0});
    CHECK(cells[1] == Coordinates{1, 0});
    CHECK(cells[2] == Coordinates{2, 0});
    CHECK(route.final_position() == Coordinates{2, 0});
    CHECK(route.traveled(Coordinates{1, 0}));
    CHECK_FALSE(route.traveled(Coordinates{3, 0}));
}

TEST_CASE("the route map applies F over X over S over star over fog terrain") {
    const Map map = row_map(".....", Coordinates{0, 0});
    const LevelObjective objective =
        make_objective(Coordinates{3, 0}, "Test Exit", ObjectiveStatus::completed, 6, 5);
    VisibilityMap visibility(5, 1);
    visibility.reveal_square(Coordinates{1, 0}, 2);

    Journal journal;
    RouteHistory route(Coordinates{0, 0});
    route.record_event(moved_event(0, Direction::right, Terrain::open, Coordinates{1, 0}));
    route.record_event(moved_event(1, Direction::right, Terrain::open, Coordinates{2, 0}));

    const ExpeditionReport report =
        build_report(objective, map, visibility, journal, route, 2, 2);
    CHECK(report.final_position == Coordinates{2, 0});
    CHECK(route_map_first_row(report) == "S*FX?");
}

TEST_CASE("an interlude report stops after the expedition totals") {
    // An interlude is read between two levels: it should fit a screen and be
    // acknowledged with one key. The journal is reachable at any time with `j`,
    // the route map describes ground the player is about to leave, and the replay
    // identity only becomes actionable once the run is over.
    const Map map = row_map("....", Coordinates{0, 0});
    const LevelObjective objective =
        make_objective(Coordinates{3, 0}, "Glass River Exit", ObjectiveStatus::completed, 3, 4);
    VisibilityMap visibility(4, 1);
    visibility.reveal_square(Coordinates{1, 0}, 8);

    Journal journal;
    RouteHistory route(Coordinates{0, 0});
    journal.record_event(moved_event(0, Direction::right, Terrain::open, Coordinates{1, 0}),
                         ctx(objective.name));

    ExpeditionCarryover carryover;
    carryover.levels_completed = 1;
    carryover.total_levels = 2;
    carryover.expedition_completed = false;
    carryover.next_tier = LevelTier::medium;

    const ExpeditionReport report =
        build_expedition_report(objective, map, visibility, journal, route,
                                world_identity_from(Settings{}), 3, 3, carryover);
    CHECK(is_interlude_report(report));

    const std::vector<std::string> lines = format_report_lines(report);
    const std::string text = join_lines(lines);
    CHECK(text.find("EXPEDITION REPORT") != std::string::npos);
    CHECK(text.find("Result: Medium level ahead. 1 of 2 complete.") != std::string::npos);
    CHECK(text.find("STATISTICS") != std::string::npos);
    CHECK(text.find("EXPEDITION\n") != std::string::npos);

    CHECK(text.find("WORLD") == std::string::npos);
    CHECK(text.find("ROUTE MAP") == std::string::npos);
    CHECK(text.find("ROUTE LEGEND") == std::string::npos);
    CHECK(text.find("EXPEDITION JOURNAL") == std::string::npos);
    CHECK_FALSE(lines.back().empty());
}

TEST_CASE("the last report of an expedition carries every section") {
    const Map map = row_map("....", Coordinates{0, 0});
    const LevelObjective objective =
        make_objective(Coordinates{3, 0}, "Glass River Exit", ObjectiveStatus::completed, 3, 4);
    VisibilityMap visibility(4, 1);
    visibility.reveal_square(Coordinates{1, 0}, 8);

    Journal journal;
    RouteHistory route(Coordinates{0, 0});

    ExpeditionCarryover carryover;
    carryover.levels_completed = 2;
    carryover.total_levels = 2;
    carryover.expedition_completed = true;

    const ExpeditionReport report =
        build_expedition_report(objective, map, visibility, journal, route,
                                world_identity_from(Settings{}), 3, 3, carryover);
    CHECK_FALSE(is_interlude_report(report));

    const std::string text = join_lines(format_report_lines(report));
    CHECK(text.find("WORLD") != std::string::npos);
    CHECK(text.find("ROUTE MAP") != std::string::npos);
    CHECK(text.find("ROUTE LEGEND") != std::string::npos);
    CHECK(text.find("EXPEDITION JOURNAL") != std::string::npos);
}

TEST_CASE("a standalone level is its own whole expedition and is never an interlude") {
    const Map map = row_map("....", Coordinates{0, 0});
    const LevelObjective objective =
        make_objective(Coordinates{3, 0}, "Glass River Exit", ObjectiveStatus::completed, 3, 4);
    VisibilityMap visibility(4, 1);
    Journal journal;
    RouteHistory route(Coordinates{0, 0});

    const ExpeditionReport report =
        build_report(objective, map, visibility, journal, route, 3, 3);
    CHECK_FALSE(is_interlude_report(report));
    CHECK(join_lines(format_report_lines(report)).find("EXPEDITION JOURNAL") != std::string::npos);
}

TEST_CASE("completed report shows result story statistics and legend wording") {
    // Six cells, which is a budget of four: the run below stays exactly on it.
    const Map map = row_map("......", Coordinates{0, 0});
    const LevelObjective objective =
        make_objective(Coordinates{3, 0}, "Glass River Exit", ObjectiveStatus::completed, 3, 6);
    VisibilityMap visibility(6, 1);
    visibility.reveal_square(Coordinates{1, 0}, 8);

    Journal journal;
    RouteHistory route(Coordinates{0, 0});
    for (std::uint64_t i = 0; i < 3; ++i) {
        const GameEvent event =
            moved_event(i, Direction::right, Terrain::open, Coordinates{static_cast<int>(i) + 1, 0});
        journal.record_event(event, ctx(objective.name));
        route.record_event(event);
    }

    const ExpeditionReport report =
        build_report(objective, map, visibility, journal, route, /*move_count=*/3,
                     /*attempt_count=*/4);

    CHECK(format_report_result(report) == "Result: level complete.");
    CHECK(format_report_story(report) ==
          "The party found Glass River Exit and reached the exit in 3 moves.");

    const std::vector<std::string> stats = format_report_statistics(report);
    REQUIRE(stats.size() == 7);
    CHECK(stats[0] == "STATISTICS");
    CHECK(stats[1] == "Score: 2000 of 2000 possible");
    CHECK(stats[2] == "Exit: 1000 of 1000, reached");
    CHECK(stats[3] == "Explored: 800 of 800, 6 of 6 cells (100%)");
    CHECK(stats[4] == "Discoveries: none placed on this level");
    CHECK(stats[5] == "Budget: 200 of 200, 3 moves of 4 budgeted");
    CHECK(stats[6] == "Moves: 3 moves, par 1 missed, 1 blocked");

    // A standalone level has no separate expedition section to repeat itself in.
    CHECK(format_report_expedition(report).empty());

    const std::vector<std::string> legend = format_report_legend();
    REQUIRE(legend.size() == 6);
    CHECK(legend[1] == "F = final position");
}

TEST_CASE("a route past the soft budget loses five points per move over it") {
    const Map map = row_map("....", Coordinates{0, 0});
    const LevelObjective objective =
        make_objective(Coordinates{3, 0}, "North Ridge", ObjectiveStatus::completed, 1, 4);
    VisibilityMap visibility(4, 1);
    visibility.reveal_square(Coordinates{1, 0}, 8);

    Journal journal;
    RouteHistory route(Coordinates{0, 0});
    for (std::uint64_t i = 0; i < 3; ++i) {
        const GameEvent event =
            moved_event(i, Direction::right, Terrain::open, Coordinates{static_cast<int>(i) + 1, 0});
        journal.record_event(event, ctx(objective.name));
        route.record_event(event);
    }

    // A four-cell level has a two-move budget, so ten moves is eight past it, and
    // the fully uncovered map still pays out in full.
    const ExpeditionReport report =
        build_report(objective, map, visibility, journal, route, 10, 10);
    CHECK(report.score.move_budget == 2);
    CHECK(report.score.moves_over_budget == 8);
    CHECK(report.score.exploration_value == completed_exploration_maximum);
    CHECK(report.score.budget_value == 160);
    CHECK(report.score.value == 1000 + 800 + 160);
}

TEST_CASE("the move line uses singular grammar for a single move") {
    const Map map = row_map("......", Coordinates{0, 0});
    const LevelObjective objective =
        make_objective(Coordinates{3, 0}, "North Ridge", ObjectiveStatus::completed, 1, 6);
    VisibilityMap visibility(6, 1);
    visibility.reveal_square(Coordinates{0, 0}, 1);

    Journal journal;
    RouteHistory route(Coordinates{0, 0});
    const GameEvent first = moved_event(0, Direction::right, Terrain::open, Coordinates{1, 0});
    journal.record_event(first, ctx(objective.name));
    route.record_event(first);

    const ExpeditionReport report =
        build_report(objective, map, visibility, journal, route, 1, 1);

    const std::vector<std::string> stats = format_report_statistics(report);
    REQUIRE(stats.size() == 7);
    CHECK(stats[5] == "Budget: 200 of 200, 1 move of 4 budgeted");
    CHECK(stats[6] == "Moves: 1 move, par 1 beaten, 0 blocked");
}

TEST_CASE("report line sections appear in the required order") {
    const Map map = row_map("...", Coordinates{0, 0});
    const LevelObjective objective =
        make_objective(Coordinates{2, 0}, "Test Exit", ObjectiveStatus::completed, 4, 3);
    VisibilityMap visibility(3, 1);
    visibility.reveal_square(Coordinates{1, 0}, 8);
    Journal journal;
    journal.record_initial_completion(objective.name);
    RouteHistory route(Coordinates{0, 0});

    const ExpeditionReport report =
        build_report(objective, map, visibility, journal, route, 0, 0);
    const std::vector<std::string> lines = format_report_lines(report);

    const auto index_of = [&lines](const std::string& header) -> int {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i] == header) return static_cast<int>(i);
        }
        return -1;
    };

    const int banner = index_of("EXPEDITION REPORT");
    const int stats = index_of("STATISTICS");
    const int world = index_of("WORLD");
    const int route_map = index_of("ROUTE MAP");
    const int legend = index_of("ROUTE LEGEND");
    const int journal_header = index_of("EXPEDITION JOURNAL");
    CHECK(banner == 0);
    CHECK(banner < stats);
    CHECK(stats < world);
    CHECK(world < route_map);
    CHECK(route_map < legend);
    CHECK(legend < journal_header);
}

TEST_CASE("plain report block ends with one newline has no ansi and is deterministic") {
    const Map map = row_map("...", Coordinates{0, 0});
    const LevelObjective objective =
        make_objective(Coordinates{2, 0}, "Test Exit", ObjectiveStatus::completed, 4, 3);
    VisibilityMap visibility(3, 1);
    visibility.reveal_square(Coordinates{1, 0}, 8);
    Journal journal;
    journal.record_initial_completion(objective.name);
    RouteHistory route(Coordinates{0, 0});

    const ExpeditionReport report =
        build_report(objective, map, visibility, journal, route, 0, 0);

    const Renderer renderer(RenderConfig{false, false, false, false});
    const std::string first = renderer.render_report_plain(report);
    const std::string second = renderer.render_report_plain(report);
    CHECK(first == second);
    CHECK(first.find('\x1b') == std::string::npos);
    REQUIRE(first.size() >= 2);
    CHECK(first.back() == '\n');
    CHECK(first[first.size() - 2] != '\n');
}

}  // TEST_SUITE("expedition_report")

TEST_SUITE("expedition_report_bonus") {

// The report used to hardcode "keen eye" in both bonus sentences, so a surveyor
// or pathfinder run was told it had earned and spent a bonus it never held. Each
// bonus must name itself and describe its own effect.
TEST_CASE("every bonus names itself in the report rather than borrowing keen eye's name") {
    struct Expectation {
        ExpeditionBonus bonus;
        const char* name;
        const char* clause;
    };
    const Expectation expectations[] = {
        {ExpeditionBonus::keen_eye, "keen eye", "discoveries are worth double"},
        {ExpeditionBonus::surveyor, "surveyor", "viewpoints see further"},
        {ExpeditionBonus::pathfinder, "pathfinder", "budget award is worth double"},
    };

    for (const Expectation& expected : expectations) {
        const ExpeditionReport report = bonus_report(expected.bonus, expected.bonus, 1);
        const std::string text = join_lines(format_report_expedition(report));

        CHECK(text.find("Bonus spent: " + std::string(expected.name) + ". This level's " +
                        expected.clause + ".") != std::string::npos);
        CHECK(text.find("Bonus earned: " + std::string(expected.name) + ". The next level's " +
                        expected.clause + ".") != std::string::npos);

        // No other bonus may be named, which is what the old hardcoded wording
        // violated for two of the three.
        for (const Expectation& other : expectations) {
            if (other.bonus == expected.bonus) continue;
            CHECK(text.find(other.name) == std::string::npos);
        }
    }
}

// The report recomputes the level score for display while the core computes the
// banked one. It once passed only the discovery multiplier, so a pathfinder level
// showed a budget award half the size of the one it was actually paid.
TEST_CASE("the displayed score applies both carried multipliers just as the core does") {
    const ExpeditionReport plain = bonus_report(ExpeditionBonus::none, ExpeditionBonus::none, 1);
    const ExpeditionReport pathfinder =
        bonus_report(ExpeditionBonus::pathfinder, ExpeditionBonus::none, 1);
    const ExpeditionReport keen =
        bonus_report(ExpeditionBonus::keen_eye, ExpeditionBonus::none, 1);

    CHECK(plain.score.budget_value == completed_budget_award);
    CHECK(pathfinder.score.budget_value == completed_budget_award * 2);
    CHECK(pathfinder.score.value == plain.score.value + completed_budget_award);

    CHECK(keen.score.discovery_value == plain.score.discovery_value * 2);
    CHECK(keen.score.budget_value == completed_budget_award);
}

// Every score component is shown with what it earned and the most it could have
// earned, so a player can see which bucket they left points in.
TEST_CASE("the breakdown shows par and the cost of running over budget") {
    // Six reachable cells give a budget of four and a par of one, so nine moves
    // is five past the budget and well past par.
    const ExpeditionReport over = bonus_report(ExpeditionBonus::none, ExpeditionBonus::none, 9);
    REQUIRE(over.score.move_budget == 4);
    REQUIRE(over.score.par_moves == 1);
    REQUIRE(over.score.moves_over_budget == 5);

    const std::string text = join_lines(format_report_statistics(over));
    CHECK(text.find("Budget: 175 of 200, 9 moves of 4 budgeted, 5 moves over cost 25") !=
          std::string::npos);
    CHECK(text.find("Moves: 9 moves, par 1 missed, 0 blocked") != std::string::npos);

    // Beating par is the pathfinder test, so the line has to say so.
    const ExpeditionReport under = bonus_report(ExpeditionBonus::none, ExpeditionBonus::none, 1);
    const std::string under_text = join_lines(format_report_statistics(under));
    CHECK(under_text.find("Moves: 1 move, par 1 beaten, 0 blocked") != std::string::npos);
    CHECK(under_text.find("Budget: 200 of 200, 1 move of 4 budgeted") != std::string::npos);
    CHECK(under_text.find("over cost") == std::string::npos);
}

// The multiplier is stated where it is applied, so a bonus that paid off is
// visible in the line whose number it changed.
TEST_CASE("a doubled component says which bonus doubled it") {
    const ExpeditionReport keen =
        bonus_report(ExpeditionBonus::keen_eye, ExpeditionBonus::none, 1);
    const std::string keen_text = join_lines(format_report_statistics(keen));
    CHECK(keen_text.find("Discoveries: 800 of 800, 2 of 2 found, keen eye multiplied by 2") !=
          std::string::npos);

    const ExpeditionReport pathfinder =
        bonus_report(ExpeditionBonus::pathfinder, ExpeditionBonus::none, 1);
    const std::string pathfinder_text = join_lines(format_report_statistics(pathfinder));
    CHECK(pathfinder_text.find(
              "Budget: 400 of 400, 1 move of 4 budgeted, pathfinder multiplied by 2") !=
          std::string::npos);

    // The possible total moves with the bonus, so the score is compared against
    // what this run could really have scored: 1000 exit, 800 explored, 400 for
    // two discoveries, and a doubled 400 budget award.
    CHECK(pathfinder_text.find("of 2600 possible") != std::string::npos);
}

}  // TEST_SUITE("expedition_report_bonus")
