#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "app_state.h"
#include "coordinates.h"
#include "map.h"
#include "move_outcome.h"
#include "renderer.h"
#include "terminal.h"
#include "terrain.h"
#include "visibility.h"

using namespace nam::console;

namespace {

Map open_map(std::size_t width, std::size_t height) {
    return Map(width, height, std::vector<Terrain>(width * height, Terrain::open),
               Coordinates{0, 0});
}

// A single fully-revealed visibility grid large enough to cover every layout
// test map. Existing layout tests predate fog and expect the whole map drawn,
// so make_input references this all-visible grid; fog-specific tests below build
// their own controlled VisibilityMap instead.
const VisibilityMap& full_visibility() {
    static const VisibilityMap revealed = [] {
        VisibilityMap map(256, 256);
        map.reveal_square(Coordinates{0, 0}, 512);
        return map;
    }();
    return revealed;
}

RenderInput make_input(const Map& map) {
    RenderInput input;
    input.map = &map;
    input.visibility = &full_visibility();
    input.actor = map.spawn();
    input.terrain = map.terrain_at(map.spawn());
    input.move_count = 3;
    input.attempt_count = 5;
    input.stamina = 7;
    input.max_stamina = 12;
    input.message = "Moved onto open ground for 1 stamina.";
    input.recent = {RecentMove{Direction::up}, RecentMove{Direction::left}};
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

// Concatenate a frame's raw rows so escape sequences can be searched globally.
std::string join_raw(const Frame& frame) {
    std::string out;
    for (const std::string& row : frame) {
        out += row;
        out.push_back('\n');
    }
    return out;
}

// Concatenate a frame's rows with ANSI stripped so HUD text can be searched and
// field ordering checked across the whole frame.
std::string join_visible(const Frame& frame) {
    std::string out;
    for (const std::string& row : frame) {
        out += strip_ansi(row);
        out.push_back('\n');
    }
    return out;
}

// The first line of plain text (the top map row), without its newline.
std::string first_line(const std::string& text) {
    const std::size_t nl = text.find('\n');
    return nl == std::string::npos ? text : text.substr(0, nl);
}

// A fog scene on an 11x1 map: after two reveals, x0..3 are remembered, x4..8 are
// visible, and x9..10 are unexplored. Distinctive terrain marks each state so
// leakage of a hidden glyph is detectable.
struct FogScene {
    Map map;
    VisibilityMap visibility;
};

FogScene make_fog_scene() {
    std::vector<Terrain> cells(11, Terrain::open);
    cells[1] = Terrain::fields;     // x1: remembered, expect 'x'.
    cells[6] = Terrain::water;      // x6: visible, expect '~'.
    cells[10] = Terrain::mountain;  // x10: unexplored, '@' must never appear.
    Map map(11, 1, std::move(cells), Coordinates{4, 0});
    VisibilityMap visibility(11, 1);
    visibility.reveal_square(Coordinates{2, 0}, 2);  // x0..4 visible.
    visibility.reveal_square(Coordinates{6, 0}, 2);  // demote x0..3, x4..8 visible.
    return {std::move(map), std::move(visibility)};
}

RenderInput fog_input(const FogScene& scene) {
    RenderInput input;
    input.map = &scene.map;
    input.visibility = &scene.visibility;
    input.actor = Coordinates{4, 0};  // guaranteed visible.
    input.terrain = scene.map.terrain_at(input.actor);
    input.move_count = 0;
    input.attempt_count = 0;
    input.message = "x";
    return input;
}

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

TEST_CASE("standard status shows stamina before terrain and moves and keeps bounds") {
    const Map map = open_map(8, 4);
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(make_input(map), TerminalSize{80, 24});
    const std::string visible = join_visible(frame);
    const std::size_t stam = visible.find("Stamina: 7/12");
    const std::size_t terr = visible.find("Terrain:");
    const std::size_t moves = visible.find("Moves:");
    REQUIRE(stam != std::string::npos);
    REQUIRE(terr != std::string::npos);
    REQUIRE(moves != std::string::npos);
    CHECK(stam < terr);   // stamina precedes terrain,
    CHECK(terr < moves);  // which precedes the move count.
    CHECK(frame.size() == 24);        // frame bounds unchanged,
    CHECK_FALSE(any_esc(frame));      // and plain rendering stays ANSI-free.
}

TEST_CASE("compact status places stamina after position and before terrain and moves") {
    const Map map = open_map(8, 4);
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(make_input(map), TerminalSize{30, 10});
    const std::string visible = join_visible(frame);
    const std::size_t pos = visible.find("(0,0)");
    const std::size_t stam = visible.find("S:7/12");
    const std::size_t terr = visible.find("open ground");
    const std::size_t moves = visible.find("M:3");
    REQUIRE(pos != std::string::npos);
    REQUIRE(stam != std::string::npos);
    REQUIRE(terr != std::string::npos);
    REQUIRE(moves != std::string::npos);
    CHECK(pos < stam);    // stamina immediately follows the position,
    CHECK(stam < terr);   // precedes terrain,
    CHECK(terr < moves);  // and the move count.
    CHECK(frame.size() == 10);
    for (const std::string& row : frame) {
        CHECK(strip_ansi(row).size() <= 30);
    }
}

TEST_CASE("stamina stays visible at the minimum compact width") {
    const Map map = open_map(8, 4);
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(make_input(map), TerminalSize{12, 6});
    const std::string visible = join_visible(frame);
    CHECK(visible.find("S:7/12") != std::string::npos);  // still visible before clipping.
    CHECK(frame.size() == 6);
    for (const std::string& row : frame) {
        CHECK(strip_ansi(row).size() <= 12);
    }
}

TEST_CASE("plain-mode status exposes stamina before terrain and moves") {
    const Map map = open_map(8, 4);
    const Renderer renderer(plain_config());
    const std::string text = renderer.render_plain(make_input(map));
    const std::size_t stam = text.find("Stamina: 7/12");
    const std::size_t terr = text.find("Terrain:");
    const std::size_t moves = text.find("Moves:");
    REQUIRE(stam != std::string::npos);
    REQUIRE(terr != std::string::npos);
    REQUIRE(moves != std::string::npos);
    CHECK(stam < terr);
    CHECK(terr < moves);
    CHECK(text.find('\x1b') == std::string::npos);  // plain mode stays ANSI-free.
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

TEST_CASE("unexplored cells render as spaces and never leak terrain") {
    // TASK-019 / TEST-010: the far mountain '@' is unexplored and must be a
    // blank, while remembered 'x' and visible '~' terrain are present.
    const FogScene scene = make_fog_scene();
    const Renderer renderer(plain_config());
    const std::string text = renderer.render_plain(fog_input(scene));
    const std::string row = first_line(text);

    CHECK(row == ".x..O.~..  ");
    CHECK(row.find('@') == std::string::npos);   // hidden terrain never leaks.
    CHECK(row.find('x') != std::string::npos);   // remembered terrain present.
    CHECK(row.find('~') != std::string::npos);   // visible terrain present.
    CHECK(text.find('\x1b') == std::string::npos);  // plain mode stays ANSI-free.
}

TEST_CASE("remembered cells are dim under ANSI, even without colour") {
    // TASK-019 / TEST-012: SGR dim appears whenever ANSI is enabled.
    const FogScene scene = make_fog_scene();

    const Renderer colored(color_config());
    const Frame colored_frame = colored.render(fog_input(scene), TerminalSize{40, 24});
    CHECK(join_raw(colored_frame).find("\033[2m") != std::string::npos);

    const Renderer ansi_no_color(RenderConfig{false, true, false, false});
    const Frame mono_frame = ansi_no_color.render(fog_input(scene), TerminalSize{40, 24});
    CHECK(join_raw(mono_frame).find("\033[2m") != std::string::npos);

    // Hidden terrain still never appears once escapes are stripped.
    CHECK_FALSE(contains_glyph(colored_frame, '@'));
}

TEST_CASE("visible terrain is never dimmed") {
    // TASK-019 / TEST-013: a fully-visible scene emits no dim sequence.
    const Map map = open_map(8, 4);
    const Renderer renderer(color_config());
    const Frame frame = renderer.render(make_input(map), TerminalSize{40, 24});
    CHECK(join_raw(frame).find("\033[2m") == std::string::npos);
}

TEST_CASE("ANSI-disabled fog output contains no escape byte") {
    // TASK-019 / TEST-011 companion.
    const FogScene scene = make_fog_scene();
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(fog_input(scene), TerminalSize{40, 24});
    CHECK_FALSE(any_esc(frame));
}

TEST_CASE("actor styling resets and stripped fog rows keep exact width") {
    // TASK-019 / TEST-014, TEST-015: the actor is drawn and reset, no dim leaks
    // past it, and stripped rows stay within the viewport width.
    const FogScene scene = make_fog_scene();
    const Renderer renderer(color_config());
    const Frame frame = renderer.render(fog_input(scene), TerminalSize{40, 24});

    CHECK(contains_glyph(frame, actor_glyph));   // actor drawn.
    const std::string raw = join_raw(frame);
    CHECK(raw.find("\033[0m") != std::string::npos);  // styles are reset.

    bool saw_map_row = false;
    for (const std::string& row : frame) {
        const std::string visible = strip_ansi(row);
        CHECK(visible.size() <= 40);
        if (visible.find(".x..O.~..") != std::string::npos) {
            saw_map_row = true;
        }
    }
    CHECK(saw_map_row);
}

TEST_CASE("no-colour ANSI actor emits no actor style but keeps remembered dim") {
    // Regression: with use_ansi=true and use_color=false (real --no-color /
    // NO_COLOR), remembered terrain must still dim (ESC[2m) while the actor
    // reverts to a bare glyph with no bold (ESC[1m), reverse (ESC[7m), or
    // bright-yellow (ESC[93m) escape.
    const FogScene scene = make_fog_scene();  // remembered x0..3 and the actor.
    const Renderer ansi_no_color(RenderConfig{false, true, false, false});
    const Frame frame = ansi_no_color.render(fog_input(scene), TerminalSize{40, 24});
    const std::string raw = join_raw(frame);

    CHECK(raw.find("\033[2m") != std::string::npos);   // remembered dim survives.
    CHECK(raw.find("\033[1m") == std::string::npos);    // no actor bold.
    CHECK(raw.find("\033[7m") == std::string::npos);    // no reverse emphasis.
    CHECK(raw.find("\033[93m") == std::string::npos);   // no actor colour.
    CHECK(contains_glyph(frame, actor_glyph));          // actor still drawn.
}

TEST_CASE("recent history renders only upper-case successful direction letters") {
    // TASK-014 / TEST-012: every recent entry is a successful move, so the HUD
    // recent line shows only upper-case direction letters and never a lower-case
    // blocked marker.
    const Map map = open_map(8, 4);
    RenderInput input = make_input(map);
    input.recent = {RecentMove{Direction::up}, RecentMove{Direction::down},
                    RecentMove{Direction::left}, RecentMove{Direction::right}};
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(input, TerminalSize{80, 24});
    const std::string visible = join_visible(frame);

    const std::size_t recent = visible.find("Recent:");
    REQUIRE(recent != std::string::npos);
    const std::string tail = visible.substr(recent);
    const std::size_t line_end = tail.find('\n');
    const std::string recent_line = tail.substr(0, line_end);

    CHECK(recent_line.find('U') != std::string::npos);
    CHECK(recent_line.find('D') != std::string::npos);
    CHECK(recent_line.find('L') != std::string::npos);
    CHECK(recent_line.find('R') != std::string::npos);
    // No lower-case blocked markers survive: every entry is a real move.
    for (const char lower : {'u', 'd', 'l', 'r'}) {
        CHECK(recent_line.find(lower) == std::string::npos);
    }
}

}  // TEST_SUITE("console")
