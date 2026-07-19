#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <variant>

#include "coordinates.h"
#include "game_state.h"
#include "map.h"
#include "map_parser.h"

namespace {

Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

std::size_t count_newlines(const std::string& text) {
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("to_string inserts explicit row separators and no trailing newline") {
    const Map map = make_map("NAM-MAP 1\nwidth 3\nheight 2\nspawn 0 0\n---\n.@~\n^x.\n");
    const std::string text = map.to_string();
    CHECK(text == ".@~\n^x.");
    // One separator between the two rows, and none at the ends.
    CHECK(count_newlines(text) == map.height() - 1);
    CHECK(text.front() != '\n');
    CHECK(text.back() != '\n');
}

TEST_CASE("rendered output has exactly height rows of width columns") {
    const Map map = builtin_map();
    const std::string text = map.to_string();
    CHECK(count_newlines(text) == map.height() - 1);

    std::size_t rows = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t next = text.find('\n', start);
        const std::size_t end = (next == std::string::npos) ? text.size() : next;
        CHECK(end - start == map.width());  // every visible row is width cells.
        ++rows;
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }
    CHECK(rows == map.height());
}

TEST_CASE("the actor overlay replaces only its own cell") {
    const Map map = make_map("NAM-MAP 1\nwidth 3\nheight 2\nspawn 0 0\n---\n.@~\n^x.\n");
    CHECK(map.to_string({0, 0}, 'O') == "O@~\n^x.");
    CHECK(map.to_string({2, 1}, 'O') == ".@~\n^xO");
}

TEST_CASE("rendering never mutates the map") {
    const Map map = make_map("NAM-MAP 1\nwidth 3\nheight 2\nspawn 0 0\n---\n.@~\n^x.\n");
    const std::string first = map.to_string();
    const std::string second = map.to_string({1, 1}, 'O');
    const std::string third = map.to_string();
    // The plain serialization is unchanged after an overlaid render.
    CHECK(first == third);
    CHECK(second != third);
    CHECK(map.terrain_at({1, 1}) == Terrain::fields);
}

TEST_CASE("GameState::render overlays the actor without moving it") {
    GameState state(make_map("NAM-MAP 1\nwidth 3\nheight 2\nspawn 0 0\n---\n.@~\n^x.\n"));
    const std::string frame = state.render('O');
    CHECK(frame == "O@~\n^x.");
    CHECK(state.actor_position() == Coordinates{0, 0});
    // Rendering twice is pure.
    CHECK(state.render('O') == frame);
}

}  // TEST_SUITE("game")
