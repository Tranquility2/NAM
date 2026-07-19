#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "app_state.h"
#include "coordinates.h"
#include "map.h"
#include "move_outcome.h"
#include "renderer.h"
#include "terminal.h"
#include "terrain.h"

using namespace nam::console;

namespace {

Map open_map(std::size_t width, std::size_t height) {
    return Map(width, height, std::vector<Terrain>(width * height, Terrain::open),
               Coordinates{0, 0});
}

RenderInput make_input(const Map& map) {
    RenderInput input;
    input.map = &map;
    input.actor = map.spawn();
    input.terrain = map.terrain_at(map.spawn());
    input.move_count = 3;
    input.attempt_count = 5;
    input.message = "Moved onto open ground.";
    input.recent = {RecentMove{Direction::up, MoveResult::moved},
                    RecentMove{Direction::left, MoveResult::blocked_by_terrain}};
    return input;
}

// Remove CSI escape sequences so the visible width of a row can be measured.
std::string strip_ansi(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() &&
                   !(static_cast<unsigned char>(text[i]) >= 0x40 &&
                     static_cast<unsigned char>(text[i]) <= 0x7e)) {
                ++i;
            }
            if (i < text.size()) {
                ++i;  // consume the final byte
            }
        } else {
            out.push_back(text[i]);
            ++i;
        }
    }
    return out;
}

bool contains_glyph(const Frame& frame, char glyph) {
    for (const std::string& row : frame) {
        if (strip_ansi(row).find(glyph) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool any_esc(const Frame& frame) {
    for (const std::string& row : frame) {
        if (row.find('\x1b') != std::string::npos) {
            return true;
        }
    }
    return false;
}

RenderConfig plain_config() { return RenderConfig{false, false, false, false}; }
RenderConfig color_config() { return RenderConfig{true, true, false, true}; }

}  // namespace

TEST_SUITE("console") {

TEST_CASE("a standard terminal yields a full-height glyph-bearing frame") {
    const Map map = open_map(8, 4);
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(make_input(map), TerminalSize{80, 24});

    CHECK(frame.size() == 24);
    CHECK(contains_glyph(frame, actor_glyph));
    for (const std::string& row : frame) {
        CHECK(strip_ansi(row).size() <= 80);
    }
}

TEST_CASE("a window below the minimum shows a bounded 'too small' panel") {
    const Map map = open_map(8, 4);
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(make_input(map), TerminalSize{11, 5});

    CHECK(frame.size() == 5);
    bool mentions_small = false;
    for (const std::string& row : frame) {
        if (row.find("Window") != std::string::npos) {
            mentions_small = true;
        }
        CHECK(row.size() <= 11);
    }
    CHECK(mentions_small);
}

TEST_CASE("a compact terminal still fits within its bounds") {
    const Map map = open_map(8, 4);
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(make_input(map), TerminalSize{20, 10});

    CHECK(frame.size() == 10);
    CHECK(contains_glyph(frame, actor_glyph));
    for (const std::string& row : frame) {
        CHECK(strip_ansi(row).size() <= 20);
    }
}

TEST_CASE("a map larger than the viewport is scrolled and never overflowed") {
    const Map map = open_map(120, 60);
    RenderInput input = make_input(map);
    input.actor = Coordinates{60, 30};  // centre of the large map.
    input.terrain = map.terrain_at(input.actor);

    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(input, TerminalSize{40, 16});

    CHECK(frame.size() == 16);
    CHECK(contains_glyph(frame, actor_glyph));  // the actor stays in view.
    for (const std::string& row : frame) {
        CHECK(strip_ansi(row).size() <= 40);
    }
}

TEST_CASE("the frame is exactly rows tall across a range of sizes") {
    const Map map = open_map(30, 12);
    const Renderer renderer(plain_config());
    for (int rows = 6; rows <= 40; rows += 7) {
        for (int cols = 16; cols <= 100; cols += 21) {
            const Frame frame = renderer.render(make_input(map), TerminalSize{cols, rows});
            CHECK(frame.size() == static_cast<std::size_t>(rows));
            for (const std::string& row : frame) {
                CHECK(strip_ansi(row).size() <= static_cast<std::size_t>(cols));
            }
        }
    }
}

TEST_CASE("an unknown terminal size falls back to a sane default") {
    const Map map = open_map(8, 4);
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(make_input(map), TerminalSize{0, 0});
    CHECK(frame.size() == 24);  // 80x24 fallback.
}

TEST_CASE("no-colour rendering never emits an escape byte") {
    const Map map = open_map(12, 6);
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(make_input(map), TerminalSize{80, 24});
    CHECK_FALSE(any_esc(frame));
}

TEST_CASE("colour rendering stays visually bounded despite escape codes") {
    const Map map = open_map(12, 6);
    const Renderer renderer(color_config());
    const Frame frame = renderer.render(make_input(map), TerminalSize{80, 24});
    CHECK(any_esc(frame));            // colour is actually applied,
    CHECK(frame.size() == 24);        // but the frame height is unchanged
    for (const std::string& row : frame) {
        CHECK(strip_ansi(row).size() <= 80);  // and visible width stays bounded.
    }
}

TEST_CASE("debug mode adds a diagnostics line in the standard layout") {
    const Map map = open_map(8, 4);
    const Renderer renderer(RenderConfig{false, false, true, false});
    const Frame frame = renderer.render(make_input(map), TerminalSize{80, 24});
    bool has_debug = false;
    for (const std::string& row : frame) {
        if (row.find("Debug") != std::string::npos) {
            has_debug = true;
        }
    }
    CHECK(has_debug);
}

TEST_CASE("plain-mode text is self-contained and ANSI-free") {
    const Map map = open_map(8, 4);
    const Renderer renderer(plain_config());
    const std::string text = renderer.render_plain(make_input(map));

    CHECK(text.find('\x1b') == std::string::npos);
    CHECK(text.find("Pos") != std::string::npos);
    CHECK(text.find("Moves:") != std::string::npos);
    CHECK(text.find(actor_glyph) != std::string::npos);
}

TEST_CASE("rendering is a pure function of its input") {
    const Map map = open_map(10, 5);
    const Renderer renderer(color_config());
    const RenderInput input = make_input(map);
    CHECK(renderer.render(input, TerminalSize{50, 20}) ==
          renderer.render(input, TerminalSize{50, 20}));
}

}  // TEST_SUITE("console")
