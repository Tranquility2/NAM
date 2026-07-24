#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "app_state.h"
#include "coordinates.h"
#include "direction.h"
#include "game_event.h"
#include "journal.h"
#include "map.h"
#include "move_outcome.h"
#include "objective.h"
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

// Build a journal of `count` distinct eastward single-step travel entries, each
// on its own terrain change so no merging occurs and numbering is 1..count.
Journal make_travel_journal(std::uint32_t count) {
    Journal journal;
    const Terrain terrains[] = {Terrain::open, Terrain::fields, Terrain::hill};
    for (std::uint32_t i = 0; i < count; ++i) {
        MoveAttemptedEvent move;
        move.direction = Direction::right;
        move.outcome.result = MoveResult::moved;
        // Alternate terrain and direction so consecutive entries never merge.
        move.direction = (i % 2 == 0) ? Direction::right : Direction::left;
        move.outcome.terrain = terrains[i % 3];
        move.outcome.stamina_cost = 1;
        journal.record_event(GameEvent{i, move}, "Beacon");
    }
    return journal;
}

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

// A fog scene on an 11x1 open map: after two reveals x0..3 are remembered,
// x4..8 are visible, and x9..10 are unexplored. The beacon overlay can be probed
// against every visibility state without any distinctive terrain interfering.
struct BeaconScene {
    Map map;
    VisibilityMap visibility;
};

BeaconScene make_beacon_scene() {
    Map map(11, 1, std::vector<Terrain>(11, Terrain::open), Coordinates{4, 0});
    VisibilityMap visibility(11, 1);
    visibility.reveal_square(Coordinates{2, 0}, 2);  // x0..4 visible.
    visibility.reveal_square(Coordinates{6, 0}, 2);  // demote x0..3, x4..8 visible.
    return {std::move(map), std::move(visibility)};
}

RenderInput beacon_input(const BeaconScene& scene, Coordinates actor,
                         const BeaconObjective& objective) {
    RenderInput input;
    input.map = &scene.map;
    input.visibility = &scene.visibility;
    input.actor = actor;
    input.terrain = scene.map.terrain_at(actor);
    input.message = "x";
    input.objective = &objective;
    return input;
}

BeaconObjective beacon_at(Coordinates cell, ObjectiveStatus status) {
    BeaconObjective objective;
    objective.beacon = cell;
    objective.name = "Glass River Beacon";
    objective.status = status;
    return objective;
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

TEST_CASE("an unexplored beacon leaks no glyph, colour, or objective coordinate") {
    // TASK-015 / TEST-013 / SEC-002: a beacon on an unexplored cell renders as a
    // blank exactly like any other unexplored cell. The map row (not the HUD
    // objective line, which legitimately names the '*' glyph) must carry no '*'.
    const BeaconScene scene = make_beacon_scene();
    const BeaconObjective objective = beacon_at(Coordinates{10, 0},  // unexplored.
                                                ObjectiveStatus::seeking_beacon);
    const Renderer plain(plain_config());
    const std::string text = plain.render_plain(beacon_input(scene, Coordinates{4, 0}, objective));
    CHECK(first_line(text).find(beacon_glyph) == std::string::npos);  // no '*' in the map row.

    const Renderer colored(color_config());
    const Frame frame = colored.render(beacon_input(scene, Coordinates{4, 0}, objective),
                                       TerminalSize{40, 24});
    CHECK(join_raw(frame).find("\033[96m") == std::string::npos);  // no beacon colour.
}

TEST_CASE("a visible beacon renders the glyph in bright cyan under colour") {
    // TASK-015 / TEST-013.
    const BeaconScene scene = make_beacon_scene();
    const BeaconObjective objective = beacon_at(Coordinates{6, 0},  // visible.
                                                ObjectiveStatus::seeking_beacon);
    const Renderer renderer(color_config());
    const Frame frame = renderer.render(beacon_input(scene, Coordinates{4, 0}, objective),
                                        TerminalSize{40, 24});

    CHECK(contains_glyph(frame, beacon_glyph));               // '*' drawn,
    CHECK(join_raw(frame).find("\033[96m") != std::string::npos);  // in bright cyan,
    CHECK(join_raw(frame).find("\033[0m") != std::string::npos);   // and reset afterwards.
}

TEST_CASE("a remembered beacon uses the dim-memory style, never bright cyan") {
    // TASK-015 / TEST-013: a remembered beacon reuses ESC[2m and emits no new
    // colour, even under colour.
    const BeaconScene scene = make_beacon_scene();
    const BeaconObjective objective = beacon_at(Coordinates{1, 0},  // remembered.
                                                ObjectiveStatus::returning_to_spawn);
    const Renderer renderer(color_config());
    const Frame frame = renderer.render(beacon_input(scene, Coordinates{4, 0}, objective),
                                        TerminalSize{40, 24});

    CHECK(contains_glyph(frame, beacon_glyph));                     // '*' drawn,
    CHECK(join_raw(frame).find("\033[2m") != std::string::npos);    // dimmed,
    CHECK(join_raw(frame).find("\033[96m") == std::string::npos);   // never bright cyan.
}

TEST_CASE("the actor glyph wins on the beacon and the glyph returns after leaving") {
    // TASK-015 / TEST-014.
    const BeaconScene scene = make_beacon_scene();
    const BeaconObjective objective = beacon_at(Coordinates{6, 0},  // visible cell.
                                                ObjectiveStatus::seeking_beacon);
    const Renderer renderer(plain_config());

    // Actor standing on the beacon: the actor glyph is shown and no '*' appears.
    const std::string on_beacon =
        renderer.render_plain(beacon_input(scene, Coordinates{6, 0}, objective));
    CHECK(first_line(on_beacon).find(actor_glyph) != std::string::npos);
    CHECK(first_line(on_beacon).find(beacon_glyph) == std::string::npos);

    // Actor elsewhere: the beacon glyph reappears on its visible cell.
    const std::string off_beacon =
        renderer.render_plain(beacon_input(scene, Coordinates{4, 0}, objective));
    CHECK(first_line(off_beacon).find(beacon_glyph) != std::string::npos);
}

TEST_CASE("plain and no-colour beacon rendering emit the glyph with no new escape") {
    // TASK-015 / REQ-028: plain mode shows only '*'; no-colour ANSI shows '*'
    // without the bright-cyan escape (remembered dim still applies elsewhere).
    const BeaconScene scene = make_beacon_scene();
    const BeaconObjective objective = beacon_at(Coordinates{6, 0},  // visible.
                                                ObjectiveStatus::seeking_beacon);

    const Renderer plain(plain_config());
    const std::string text = plain.render_plain(beacon_input(scene, Coordinates{4, 0}, objective));
    CHECK(first_line(text).find(beacon_glyph) != std::string::npos);
    CHECK(text.find('\x1b') == std::string::npos);

    const Renderer no_color(RenderConfig{false, true, false, false});
    const Frame frame = no_color.render(beacon_input(scene, Coordinates{4, 0}, objective),
                                        TerminalSize{40, 24});
    CHECK(contains_glyph(frame, beacon_glyph));
    CHECK(join_raw(frame).find("\033[96m") == std::string::npos);  // no bright cyan.
}

TEST_CASE("the standard layout shows the objective line after status and before the message") {
    // TASK-015 / TEST-015 / REQ-030.
    const Map map = open_map(8, 4);
    const BeaconObjective objective = beacon_at(Coordinates{7, 3}, ObjectiveStatus::seeking_beacon);
    RenderInput input = make_input(map);
    input.objective = &objective;
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(input, TerminalSize{80, 24});
    const std::string visible = join_visible(frame);

    const std::size_t status = visible.find("Stamina: 7/12");
    const std::size_t objline = visible.find(
        "Objective: Reach Glass River Beacon (*), then return to spawn.");
    const std::size_t message = visible.find("> Moved onto open ground");
    REQUIRE(status != std::string::npos);
    REQUIRE(objline != std::string::npos);
    REQUIRE(message != std::string::npos);
    CHECK(status < objline);   // objective follows the status line,
    CHECK(objline < message);  // and precedes the latest-event message.
    CHECK(frame.size() == 24);
    for (const std::string& row : frame) {
        CHECK(strip_ansi(row).size() <= 80);
    }
}

TEST_CASE("the compact layout adds a bounded Goal line") {
    // TASK-015 / TEST-015 / REQ-030.
    const Map map = open_map(8, 4);
    const BeaconObjective objective = beacon_at(Coordinates{7, 3}, ObjectiveStatus::seeking_beacon);
    RenderInput input = make_input(map);
    input.objective = &objective;
    const Renderer renderer(plain_config());
    const Frame frame = renderer.render(input, TerminalSize{30, 10});
    const std::string visible = join_visible(frame);

    CHECK(visible.find("Goal: reach Glass River Beacon") != std::string::npos);
    CHECK(frame.size() == 10);
    for (const std::string& row : frame) {
        CHECK(strip_ansi(row).size() <= 30);
    }
}

TEST_CASE("plain rendering places the objective line after status and before the message") {
    // TASK-015 / TEST-015 / REQ-030.
    const Map map = open_map(8, 4);
    const BeaconObjective objective =
        beacon_at(Coordinates{7, 3}, ObjectiveStatus::returning_to_spawn);
    RenderInput input = make_input(map);
    input.objective = &objective;
    const Renderer renderer(plain_config());
    const std::string text = renderer.render_plain(input);

    const std::size_t status = text.find("Stamina: 7/12");
    const std::size_t objline = text.find("Objective: Return to spawn.");
    const std::size_t message = text.find("Moved onto open ground");
    REQUIRE(status != std::string::npos);
    REQUIRE(objline != std::string::npos);
    REQUIRE(message != std::string::npos);
    CHECK(status < objline);
    CHECK(objline < message);
    CHECK(text.find('\x1b') == std::string::npos);
}

TEST_CASE("the objective line keeps the frame bounded across a range of sizes") {
    // TASK-015 / TEST-015 / REQ-031: adding the objective row never overflows or
    // shortens the frame.
    const Map map = open_map(30, 12);
    const BeaconObjective objective = beacon_at(Coordinates{29, 11}, ObjectiveStatus::seeking_beacon);
    const Renderer renderer(color_config());
    for (int rows = 6; rows <= 40; rows += 7) {
        for (int cols = 16; cols <= 100; cols += 21) {
            RenderInput input = make_input(map);
            input.objective = &objective;
            const Frame frame = renderer.render(input, TerminalSize{cols, rows});
            CHECK(frame.size() == static_cast<std::size_t>(rows));
            for (const std::string& row : frame) {
                CHECK(strip_ansi(row).size() <= static_cast<std::size_t>(cols));
            }
        }
    }
}

// --- Objective screens ------------------------------------------------------

// The visible, whitespace-trimmed content of each frame row, dropping the blank
// centring rows, so a panel's logical lines can be checked directly.
std::vector<std::string> panel_content_lines(const Frame& frame) {
    std::vector<std::string> lines;
    for (const std::string& row : frame) {
        const std::string visible = strip_ansi(row);
        const std::size_t begin = visible.find_first_not_of(' ');
        if (begin == std::string::npos) {
            continue;  // a blank centring row.
        }
        const std::size_t end = visible.find_last_not_of(' ');
        lines.push_back(visible.substr(begin, end - begin + 1));
    }
    return lines;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

TEST_CASE("the interactive discovery screen contains the exact ordered lines and stays bounded") {
    // REQ-017 / REQ-031 / TEST-009: exactly the four discovery lines in order,
    // exactly `rows` rows, each within `columns`, and no ANSI escape byte.
    const Renderer renderer(color_config());
    const Frame frame = renderer.render_discovery("Glass River Beacon", TerminalSize{60, 16});
    CHECK(frame.size() == 16);
    CHECK_FALSE(any_esc(frame));
    for (const std::string& row : frame) {
        CHECK(strip_ansi(row).size() <= 60);
    }
    const std::vector<std::string> lines = panel_content_lines(frame);
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "BEACON DISCOVERED");
    CHECK(lines[1] == "Glass River Beacon");
    CHECK(lines[2] == "Return to spawn to complete the expedition.");
    CHECK(lines[3] == "Press Enter to continue, or use a movement key.");
}

TEST_CASE("the plain discovery block is the exact lines with one trailing newline and no ANSI") {
    // REQ-033 / TEST-009: the plain discovery block is the four logical lines, one
    // per line, with a single trailing newline and no escape byte.
    const Renderer renderer(plain_config());
    const std::string block = renderer.render_discovery_plain("Glass River Beacon");
    CHECK(block ==
          "BEACON DISCOVERED\n"
          "Glass River Beacon\n"
          "Return to spawn to complete the expedition.\n"
          "Press Enter to continue, or use a movement key.\n");
    CHECK(block.find('\x1b') == std::string::npos);
    // Exactly one trailing newline: the last character is a newline and the one
    // before it is not.
    REQUIRE(block.size() >= 2);
    CHECK(block.back() == '\n');
    CHECK(block[block.size() - 2] != '\n');
}

TEST_CASE("the interactive completion screen contains the exact ordered summary lines") {
    // REQ-023 / REQ-024 / TEST-013: exactly the six completion lines in order,
    // with the counters and final stamina rendered in decimal.
    const Renderer renderer(color_config());
    const CompletionSummary summary{"Glass River Beacon", 8, 11, 3, 12};
    const Frame frame = renderer.render_completion(summary, TerminalSize{40, 16});
    CHECK(frame.size() == 16);
    CHECK_FALSE(any_esc(frame));
    for (const std::string& row : frame) {
        CHECK(strip_ansi(row).size() <= 40);
    }
    const std::vector<std::string> lines = panel_content_lines(frame);
    REQUIRE(lines.size() == 6);
    CHECK(lines[0] == "EXPEDITION COMPLETE");
    CHECK(lines[1] == "Beacon: Glass River Beacon");
    CHECK(lines[2] == "Moves: 8");
    CHECK(lines[3] == "Attempts: 11");
    CHECK(lines[4] == "Final stamina: 3/12");
    CHECK(lines[5] == "Press Enter or q to exit.");
}

TEST_CASE("the plain completion block is the exact lines with one trailing newline and no ANSI") {
    // REQ-033 / TEST-013.
    const Renderer renderer(plain_config());
    const CompletionSummary summary{"Glass River Beacon", 8, 11, 3, 12};
    const std::string block = renderer.render_completion_plain(summary);
    CHECK(block ==
          "EXPEDITION COMPLETE\n"
          "Beacon: Glass River Beacon\n"
          "Moves: 8\n"
          "Attempts: 11\n"
          "Final stamina: 3/12\n"
          "Press Enter or q to exit.\n");
    CHECK(block.find('\x1b') == std::string::npos);
    const std::vector<std::string> lines = split_lines(block);
    REQUIRE(lines.size() == 6);
    CHECK(lines.front() == "EXPEDITION COMPLETE");
}

TEST_CASE("objective screens use the 80x24 fallback for an unknown size") {
    // REQ-031: an invalid size falls back to 80x24, so the frame has 24 rows.
    const Renderer renderer(color_config());
    const Frame discovery = renderer.render_discovery("Glass River Beacon", TerminalSize{0, 0});
    CHECK(discovery.size() == 24);
    const CompletionSummary summary{"Glass River Beacon", 8, 11, 3, 12};
    const Frame completion = renderer.render_completion(summary, TerminalSize{0, 0});
    CHECK(completion.size() == 24);
    for (const std::string& row : discovery) {
        CHECK(strip_ansi(row).size() <= 80);
    }
}

TEST_CASE("objective screens below the minimum reuse the bounded too-small panel") {
    // REQ-032: a size below the absolute minimum shows the shared window-too-small
    // panel, bounded to the requested size.
    const Renderer renderer(color_config());
    const Frame discovery = renderer.render_discovery("Glass River Beacon", TerminalSize{11, 5});
    CHECK(discovery.size() == 5);
    bool mentions_small = false;
    for (const std::string& row : discovery) {
        if (row.find("Window") != std::string::npos) {
            mentions_small = true;
        }
        CHECK(row.size() <= 11);
    }
    CHECK(mentions_small);
}

TEST_CASE("objective screens are exactly rows tall and bounded across a range of sizes") {
    // REQ-031 / TEST-009: every above-minimum size yields exactly `rows` rows, each
    // within `columns`, for both the discovery and completion screens.
    const Renderer renderer(color_config());
    const CompletionSummary summary{"Glass River Beacon", 8, 11, 3, 12};
    for (int rows = 6; rows <= 40; rows += 7) {
        for (int cols = 16; cols <= 100; cols += 21) {
            const Frame discovery =
                renderer.render_discovery("Glass River Beacon", TerminalSize{cols, rows});
            const Frame completion = renderer.render_completion(summary, TerminalSize{cols, rows});
            CHECK(discovery.size() == static_cast<std::size_t>(rows));
            CHECK(completion.size() == static_cast<std::size_t>(rows));
            for (const std::string& row : discovery) {
                CHECK(strip_ansi(row).size() <= static_cast<std::size_t>(cols));
            }
            for (const std::string& row : completion) {
                CHECK(strip_ansi(row).size() <= static_cast<std::size_t>(cols));
            }
        }
    }
}

TEST_CASE("the plain journal block is ANSI-free with a header and one trailing newline") {
    const Renderer renderer(RenderConfig{false, false, false, false});
    const Journal journal = make_travel_journal(3);
    const std::string block = renderer.render_journal_plain(journal);

    CHECK(block.find('\x1b') == std::string::npos);
    CHECK(block.rfind("EXPEDITION JOURNAL", 0) == 0);  // Header is the first line.
    CHECK(block.find("1. ") != std::string::npos);
    CHECK(block.find("2. ") != std::string::npos);
    CHECK(block.find("3. ") != std::string::npos);
    REQUIRE_FALSE(block.empty());
    CHECK(block.back() == '\n');
    CHECK(block[block.size() - 2] != '\n');  // Exactly one trailing newline.
}

TEST_CASE("the empty plain journal block shows the placeholder") {
    const Renderer renderer(RenderConfig{false, false, false, false});
    const Journal empty;
    const std::string block = renderer.render_journal_plain(empty);
    CHECK(block == std::string("EXPEDITION JOURNAL\n(No journal entries yet.)\n"));
}

TEST_CASE("the interactive journal frame is exactly row and column bounded across sizes") {
    const Renderer renderer(color_config());
    const Journal journal = make_travel_journal(40);
    for (int rows = 6; rows <= 40; rows += 7) {
        for (int cols = 16; cols <= 100; cols += 21) {
            const TerminalSize size{cols, rows};
            const Frame frame = renderer.render_journal(journal, 0, size);
            CHECK(frame.size() == static_cast<std::size_t>(rows));
            for (const std::string& row : frame) {
                CHECK(row.find('\x1b') == std::string::npos);
                CHECK(row.size() <= static_cast<std::size_t>(cols));
            }
        }
    }
}

TEST_CASE("the interactive journal frame shows header and controls") {
    const Renderer renderer(color_config());
    const Journal journal = make_travel_journal(5);
    const Frame frame = renderer.render_journal(journal, 0, TerminalSize{60, 12});
    const std::string visible = join_visible(frame);
    CHECK(visible.find("EXPEDITION JOURNAL") != std::string::npos);
    CHECK(visible.find("return") != std::string::npos);
    CHECK(visible.find("quit") != std::string::npos);
    CHECK(visible.find("scroll") != std::string::npos);
}

TEST_CASE("an empty interactive journal frame shows the placeholder") {
    const Renderer renderer(color_config());
    const Journal empty;
    const Frame frame = renderer.render_journal(empty, 0, TerminalSize{60, 10});
    const std::string visible = join_visible(frame);
    CHECK(visible.find("(No journal entries yet.)") != std::string::npos);
    CHECK(frame.size() == 10);
}

TEST_CASE("journal page capacity matches the header and controls reservation") {
    const Renderer renderer(color_config());
    // Header + controls reserve two rows, leaving rows-2 entry rows.
    CHECK(renderer.journal_page_capacity(TerminalSize{60, 12}) == 10);
    CHECK(renderer.journal_page_capacity(TerminalSize{60, 24}) == 22);
    // An unknown size resolves to the 80x24 fallback.
    CHECK(renderer.journal_page_capacity(TerminalSize{0, 0}) == 22);
}

TEST_CASE("the interactive journal scrolls chronologically and clamps at both ends") {
    const Renderer renderer(color_config());
    const Journal journal = make_travel_journal(20);
    const TerminalSize size{60, 7};  // capacity 5 entry rows.
    const int capacity = renderer.journal_page_capacity(size);
    REQUIRE(capacity == 5);

    // scroll_top 0 shows entries 1..5 chronologically, top to bottom.
    const std::string top = join_visible(renderer.render_journal(journal, 0, size));
    CHECK(top.find("1. ") != std::string::npos);
    CHECK(top.find("5. ") != std::string::npos);
    CHECK(top.find("6. ") == std::string::npos);

    // The newest page (scroll_top 15) shows entries 16..20.
    const std::string bottom = join_visible(renderer.render_journal(journal, 15, size));
    CHECK(bottom.find("16. ") != std::string::npos);
    CHECK(bottom.find("20. ") != std::string::npos);
    CHECK(bottom.find("15. ") == std::string::npos);

    // Over-scroll clamps to the newest page rather than running past the end.
    const std::string clamped = join_visible(renderer.render_journal(journal, 999, size));
    CHECK(clamped == bottom);
    // Negative scroll clamps to the oldest page.
    const std::string neg = join_visible(renderer.render_journal(journal, -5, size));
    CHECK(neg == top);
}

TEST_CASE("the interactive journal reuses the too-small panel below the minimum") {
    const Renderer renderer(color_config());
    const Journal journal = make_travel_journal(5);
    const Frame frame = renderer.render_journal(journal, 0, TerminalSize{11, 5});
    const std::string visible = join_visible(frame);
    CHECK(visible.find("Window") != std::string::npos);
}

TEST_CASE("journal rendering is deterministic for identical inputs") {
    const Renderer renderer(color_config());
    const Journal journal = make_travel_journal(12);
    const TerminalSize size{72, 15};
    CHECK(join_raw(renderer.render_journal(journal, 3, size)) ==
          join_raw(renderer.render_journal(journal, 3, size)));
    CHECK(renderer.render_journal_plain(journal) == renderer.render_journal_plain(journal));
}

}  // TEST_SUITE("console")