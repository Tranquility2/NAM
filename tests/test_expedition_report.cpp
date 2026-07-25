#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "coordinates.h"
#include "direction.h"
#include "expedition_report.h"
#include "game_event.h"
#include "journal.h"
#include "map.h"
#include "move_outcome.h"
#include "objective.h"
#include "renderer.h"
#include "settings.h"
#include "terrain.h"
#include "visibility.h"

using namespace nam::console;

namespace {

GameEvent moved_event(std::uint64_t sequence, Direction direction, Terrain terrain,
                      std::uint32_t cost, Coordinates to) {
    MoveAttemptedEvent move;
    move.direction = direction;
    move.outcome.result = MoveResult::moved;
    move.outcome.terrain = terrain;
    move.outcome.stamina_cost = cost;
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

BeaconObjective make_objective(Coordinates beacon, std::string name, ObjectiveStatus status,
                               std::uint64_t round_trip, std::uint64_t minimum_provisions,
                               std::uint64_t reachable_cells) {
    BeaconObjective objective;
    objective.beacon = beacon;
    objective.name = std::move(name);
    objective.status = status;
    objective.minimum_round_trip_stamina_cost = round_trip;
    objective.minimum_required_provisions = minimum_provisions;
    objective.total_reachable_walkable_cells = reachable_cells;
    return objective;
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

ExpeditionReport build_report(ExpeditionResult result, const BeaconObjective& objective,
                              const Map& map, const VisibilityMap& visibility,
                              const Journal& journal, const RouteHistory& route,
                              std::uint64_t move_count, std::uint64_t attempt_count,
                              std::uint32_t final_stamina, std::uint32_t max_stamina,
                              std::uint32_t starting_provisions,
                              std::uint32_t remaining_provisions) {
    return build_expedition_report(result, objective, map, visibility, journal, route,
                                   world_identity_from(Settings{}), move_count, attempt_count,
                                   final_stamina, max_stamina, starting_provisions,
                                   remaining_provisions);
}

}  // namespace

TEST_SUITE("console") {

TEST_CASE("route history starts at spawn and appends only successful destinations") {
    RouteHistory route(Coordinates{0, 0});
    route.record_event(moved_event(0, Direction::right, Terrain::open, 1, Coordinates{1, 0}));
    route.record_event(blocked_event(1, Direction::right));
    route.record_event(GameEvent{2, RestedEvent{}});
    route.record_event(moved_event(3, Direction::right, Terrain::open, 1, Coordinates{2, 0}));

    const std::vector<Coordinates>& cells = route.cells();
    REQUIRE(cells.size() == 3);
    CHECK(cells[0] == Coordinates{0, 0});
    CHECK(cells[1] == Coordinates{1, 0});
    CHECK(cells[2] == Coordinates{2, 0});
    CHECK(route.final_position() == Coordinates{2, 0});
    CHECK(route.traveled(Coordinates{1, 0}));
    CHECK_FALSE(route.traveled(Coordinates{3, 0}));
}

TEST_CASE("the route map applies F over B over S over star over fog terrain") {
    const Map map = row_map(".....", Coordinates{0, 0});
    const BeaconObjective objective =
        make_objective(Coordinates{3, 0}, "Test Beacon", ObjectiveStatus::completed, 6, 1, 5);
    VisibilityMap visibility(5, 1);
    visibility.reveal_square(Coordinates{1, 0}, 2);

    Journal journal;
    RouteHistory route(Coordinates{0, 0});
    route.record_event(moved_event(0, Direction::right, Terrain::open, 1, Coordinates{1, 0}));
    route.record_event(moved_event(1, Direction::right, Terrain::open, 1, Coordinates{2, 0}));

    const ExpeditionReport report =
        build_report(ExpeditionResult::completed, objective, map, visibility, journal, route, 2, 2,
                     18, 20, 2, 1);
    CHECK(report.final_position == Coordinates{2, 0});
    CHECK(route_map_first_row(report) == "S*FB?");
}

TEST_CASE("completed report shows result story new statistics and legend wording") {
    const Map map = row_map("....", Coordinates{0, 0});
    const BeaconObjective objective = make_objective(Coordinates{3, 0}, "Glass River Beacon",
                                                     ObjectiveStatus::completed, 6, 1, 4);
    VisibilityMap visibility(4, 1);
    visibility.reveal_square(Coordinates{1, 0}, 8);

    Journal journal;
    RouteHistory route(Coordinates{0, 0});
    for (std::uint64_t i = 0; i < 3; ++i) {
        const GameEvent event =
            moved_event(i, Direction::right, Terrain::open, 1, Coordinates{static_cast<int>(i) + 1, 0});
        journal.record_event(event, objective.name);
        route.record_event(event);
    }

    const ExpeditionReport report =
        build_report(ExpeditionResult::completed, objective, map, visibility, journal, route,
                     /*move_count=*/3, /*attempt_count=*/4, /*final_stamina=*/17,
                     /*max_stamina=*/20, /*starting_provisions=*/2,
                     /*remaining_provisions=*/1);

    CHECK(format_report_result(report) == "Result: expedition complete.");
    CHECK(format_report_story(report) ==
          "The Glass River Beacon expedition is complete. The party made 3 successful moves, "
          "spent 3 stamina, used 1 provision, and hit 1 blocked move, earning a final score of "
          "980 out of 1000.");

    const std::vector<std::string> stats = format_report_statistics(report);
    REQUIRE(stats.size() == 14);
    CHECK(stats[0] == "STATISTICS");
    CHECK(stats[1] == "Score: 980 / 1000");
    CHECK(stats[2] == "Moves: 3");
    CHECK(stats[3] == "Move attempts: 4");
    CHECK(stats[4] == "Blocked moves: 1");
    CHECK(stats[5] == "Provisions used: 1");
    CHECK(stats[6] == "Provisions remaining: 1");
    CHECK(stats[7] == "Provisions starting: 2");
    CHECK(stats[8] == "Minimum provisions required: 1");
    CHECK(stats[9] == "Stamina spent: 3");
    CHECK(stats[10] == "Optimal round-trip cost: 6");
    CHECK(stats[11] == "Final stamina: 17/20");
    CHECK(stats[12] == "Explored reachable terrain: 4 / 4");
    CHECK(stats[13] == "Beacon reached: yes");

    const std::vector<std::string> legend = format_report_legend();
    REQUIRE(legend.size() == 6);
    CHECK(legend[1] == "F = final position");
}

TEST_CASE("rescued report uses rescued result story and 750 score ceiling") {
    const Map map = row_map("....", Coordinates{0, 0});
    const BeaconObjective objective = make_objective(Coordinates{3, 0}, "North Ridge",
                                                     ObjectiveStatus::returning_to_spawn, 6, 1, 4);
    VisibilityMap visibility(4, 1);
    visibility.reveal_square(Coordinates{1, 0}, 1);

    Journal journal;
    RouteHistory route(Coordinates{0, 0});
    const GameEvent first = moved_event(0, Direction::right, Terrain::open, 1, Coordinates{1, 0});
    const GameEvent second = moved_event(1, Direction::right, Terrain::open, 1, Coordinates{2, 0});
    journal.record_event(first, objective.name);
    journal.record_event(second, objective.name);
    route.record_event(first);
    route.record_event(second);

    const ExpeditionReport report =
        build_report(ExpeditionResult::rescued, objective, map, visibility, journal, route,
                     /*move_count=*/2, /*attempt_count=*/3, /*final_stamina=*/0,
                     /*max_stamina=*/20, /*starting_provisions=*/2,
                     /*remaining_provisions=*/0);

    CHECK(format_report_result(report) == "Result: rescued after running out of provisions.");
    CHECK(format_report_story(report) ==
          "The North Ridge expedition ended early. Out of provisions after 2 moves and 2 "
          "provisions, the party signaled for an embarrassingly early pickup, earning a "
          "rescued score of 605 out of 750.");

    const std::vector<std::string> stats = format_report_statistics(report);
    REQUIRE(stats.size() == 14);
    CHECK(stats[1] == "Score: 605 / 750");
    CHECK(stats[13] == "Beacon reached: yes");
}

TEST_CASE("rescued report marks beacon reached no when objective was never discovered") {
    const Map map = row_map("....", Coordinates{0, 0});
    const BeaconObjective objective = make_objective(Coordinates{3, 0}, "North Ridge",
                                                     ObjectiveStatus::seeking_beacon, 6, 1, 4);
    VisibilityMap visibility(4, 1);
    visibility.reveal_square(Coordinates{0, 0}, 1);

    Journal journal;
    RouteHistory route(Coordinates{0, 0});
    const GameEvent first = moved_event(0, Direction::right, Terrain::open, 1, Coordinates{1, 0});
    journal.record_event(first, objective.name);
    route.record_event(first);

    const ExpeditionReport report =
        build_report(ExpeditionResult::rescued, objective, map, visibility, journal, route,
                     /*move_count=*/1, /*attempt_count=*/1, /*final_stamina=*/0,
                     /*max_stamina=*/20, /*starting_provisions=*/1,
                     /*remaining_provisions=*/0);

    const std::vector<std::string> stats = format_report_statistics(report);
    REQUIRE(stats.size() == 14);
    CHECK(stats[1] == "Score: 250 / 750");
    CHECK(stats[13] == "Beacon reached: no");
}

TEST_CASE("report line sections appear in the required order") {
    const Map map = row_map("...", Coordinates{0, 0});
    const BeaconObjective objective =
        make_objective(Coordinates{2, 0}, "Test Beacon", ObjectiveStatus::completed, 4, 0, 3);
    VisibilityMap visibility(3, 1);
    visibility.reveal_square(Coordinates{1, 0}, 8);
    Journal journal;
    journal.record_initial_completion(objective.name);
    RouteHistory route(Coordinates{0, 0});

    const ExpeditionReport report =
        build_report(ExpeditionResult::completed, objective, map, visibility, journal, route, 0, 0,
                     20, 20, 1, 1);
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
    const BeaconObjective objective =
        make_objective(Coordinates{2, 0}, "Test Beacon", ObjectiveStatus::completed, 4, 0, 3);
    VisibilityMap visibility(3, 1);
    visibility.reveal_square(Coordinates{1, 0}, 8);
    Journal journal;
    journal.record_initial_completion(objective.name);
    RouteHistory route(Coordinates{0, 0});

    const ExpeditionReport report =
        build_report(ExpeditionResult::completed, objective, map, visibility, journal, route, 0, 0,
                     20, 20, 1, 1);

    const Renderer renderer(RenderConfig{false, false, false, false});
    const std::string first = renderer.render_report_plain(report);
    const std::string second = renderer.render_report_plain(report);
    CHECK(first == second);
    CHECK(first.find('\x1b') == std::string::npos);
    REQUIRE(first.size() >= 2);
    CHECK(first.back() == '\n');
    CHECK(first[first.size() - 2] != '\n');
}

}  // TEST_SUITE("console")
