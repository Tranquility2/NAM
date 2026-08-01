#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "console_app.h"
#include "direction.h"
#include "expedition.h"
#include "level_feature.h"
#include "frame.h"
#include "game_state.h"
#include "input.h"
#include "map.h"
#include "map_parser.h"
#include "objective.h"
#include "settings.h"
#include "terminal.h"

using namespace nam::console;

namespace {

Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

// A room the actor can move around in horizontally. Its deterministic exit_cell sits
// at the far right (7,1), well beyond the reach of the short movement scripts
// below, so ordinary movement and quit tests never enter the discovery flow.
GameState make_state() {
    return GameState(
        make_map("NAM-MAP 1\nwidth 9\nheight 3\nspawn 2 1\n---\n#########\n#.......#\n#########\n"));
}

// A room wider than the initial 5x5 reveal, with a distinctive mountain '@' far
// to the right (walkable, so the actor can approach it) that starts hidden.
GameState make_big_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 9\nheight 3\nspawn 1 1\n---\n#########\n......@..\n#########\n"));
}

// A corridor with an open spawn at x=1, a hill at x=2, and a distinctive water
// cell at x=5. The water sits at radius 3 from the hill but beyond radius 2 from
// either the open spawn or the hill cell if it were flat, so only standing on
// the hill reveals it. Stepping back off the hill keeps it as memory.
GameState make_hill_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 9\nheight 3\nspawn 1 1\n---\n#########\n..^..~...\n#########\n"));
}

// A long corridor of fields leading to a distant water glyph. Nothing accumulates
// over the march, so it never stalls and walking far enough finally reveals the
// water.
GameState make_mountain_reach_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 14\nheight 4\nspawn 0 1\n---\n##############\n.xxxxxxxxxxx.~\n.#############\n..............\n"));
}

std::size_t count_char(const std::string& text, char needle) {
    std::size_t total = 0;
    for (const char c : text) {
        if (c == needle) {
            ++total;
        }
    }
    return total;
}

std::size_t count_substr(const std::string& text, const std::string& needle) {
    if (needle.empty()) {
        return 0;
    }
    std::size_t total = 0;
    std::size_t pos = text.find(needle);
    while (pos != std::string::npos) {
        ++total;
        pos = text.find(needle, pos + needle.size());
    }
    return total;
}

int run_plain_state(GameState state, const std::string& commands, std::string& output,
                    Settings settings = {}) {
    ConsoleApp app(std::move(state), settings);
    std::istringstream input(commands);
    std::ostringstream out;
    const int code = app.run_plain(input, out);
    output = out.str();
    return code;
}

int run_plain_with(const std::string& commands, std::string& output, Settings settings = {}) {
    ConsoleApp app(make_state(), settings);
    std::istringstream input(commands);
    std::ostringstream out;
    const int code = app.run_plain(input, out);
    output = out.str();
    return code;
}

// A one-row open corridor with a landmark at (2,0) and exit at (3,0).
GameState make_corridor_state() {
    return GameState(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
}

// The deterministic exit_cell name of the corridor map, read from an independent
// GameState so expectations use the exact generated name.
std::string corridor_landmark_name() {
    return make_corridor_state().objective().name;
}

// A single-reachable-cell map: the spawn is sealed by a wall, so the exit_cell is at
// spawn and the objective starts completed.
GameState make_single_cell_state() {
    return GameState(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.#.\n"));
}

std::string single_cell_landmark_name() {
    return make_single_cell_state().objective().name;
}

// A three-cell corridor with an adjacent landmark followed by the exit.
GameState make_adjacent_state() {
    return GameState(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n...\n"));
}

std::string adjacent_landmark_name() {
    return make_adjacent_state().objective().name;
}

// A scripted, TTY-free InteractiveSession: it replays a fixed list of key events,
// counts draws and reads, and yields end_of_input once the script is exhausted so
// the loop can never block. This proves the final-frame draw count and that no
// input is read after the expedition completes.
class FakeSession final : public InteractiveSession {
public:
    explicit FakeSession(std::vector<KeyEvent> events) : events_(std::move(events)) {}

    [[nodiscard]] bool supports_ansi() const override { return false; }
    [[nodiscard]] TerminalSize size() const override { return TerminalSize{80, 24}; }
    [[nodiscard]] KeyEvent read_event() override {
        ++reads;
        if (read_index_ < events_.size()) {
            return events_[read_index_++];
        }
        return KeyEvent::of(Key::end_of_input);
    }
    void draw(const Frame& frame) override { ++draws; frames.push_back(frame); }

    int draws = 0;
    int reads = 0;
    std::vector<Frame> frames;

private:
    std::vector<KeyEvent> events_;
    std::size_t read_index_ = 0;
};

}  // namespace

TEST_SUITE("console") {

TEST_CASE("movement keys and command letters map to directions") {
    CHECK(direction_for(KeyEvent::of(Key::up)) == Direction::up);
    CHECK(direction_for(KeyEvent::of(Key::down)) == Direction::down);
    CHECK(direction_for(KeyEvent::of(Key::left)) == Direction::left);
    CHECK(direction_for(KeyEvent::of(Key::right)) == Direction::right);

    CHECK(direction_for(KeyEvent::of_character('w')) == Direction::up);
    CHECK(direction_for(KeyEvent::of_character('k')) == Direction::up);
    CHECK(direction_for(KeyEvent::of_character('s')) == Direction::down);
    CHECK(direction_for(KeyEvent::of_character('a')) == Direction::left);
    CHECK(direction_for(KeyEvent::of_character('h')) == Direction::left);
    CHECK(direction_for(KeyEvent::of_character('d')) == Direction::right);
    CHECK(direction_for(KeyEvent::of_character('l')) == Direction::right);
    CHECK(direction_for(KeyEvent::of_character('W')) == Direction::up);  // case-insensitive.
}

TEST_CASE("j is reserved for the journal and is no longer a movement alias") {
    // 'j' used to move down; it now opens the journal and yields no direction.
    CHECK_FALSE(direction_for(KeyEvent::of_character('j')).has_value());
    CHECK_FALSE(direction_for(KeyEvent::of_character('J')).has_value());
    CHECK(is_journal_event(KeyEvent::of_character('j')));
    CHECK(is_journal_event(KeyEvent::of_character('J')));
    CHECK_FALSE(is_journal_event(KeyEvent::of_character('s')));
    CHECK_FALSE(is_journal_event(KeyEvent::of(Key::down)));
    // Down movement still works through 's' and the arrow key.
    CHECK(direction_for(KeyEvent::of_character('s')) == Direction::down);
    CHECK(direction_for(KeyEvent::of(Key::down)) == Direction::down);
    // The journal key is never mistaken for quit.
    CHECK_FALSE(is_quit_event(KeyEvent::of_character('j')));
}

TEST_CASE("non-movement events yield no direction") {
    CHECK_FALSE(direction_for(KeyEvent::of_character('q')).has_value());
    CHECK_FALSE(direction_for(KeyEvent::of(Key::home)).has_value());
    CHECK_FALSE(direction_for(KeyEvent::of(Key::unknown)).has_value());
    CHECK_FALSE(direction_for(KeyEvent::of(Key::enter)).has_value());
    CHECK_FALSE(direction_for(KeyEvent::of(Key::escape)).has_value());
}

TEST_CASE("quit is recognised from Escape and q") {
    CHECK(is_quit_event(KeyEvent::of(Key::escape)));
    CHECK(is_quit_event(KeyEvent::of_character('q')));
    CHECK(is_quit_event(KeyEvent::of_character('Q')));
    CHECK_FALSE(is_quit_event(KeyEvent::of_character('w')));
    CHECK_FALSE(is_quit_event(KeyEvent::of(Key::up)));
    CHECK_FALSE(is_quit_event(KeyEvent::of(Key::end_of_input)));
}

TEST_CASE("plain mode plays a scripted session and exits cleanly") {
    std::string output;
    const int code = run_plain_with("d\nd\nq\n", output);
    CHECK(code == 0);
    CHECK_FALSE(output.empty());
    CHECK(output.find('\x1b') == std::string::npos);  // no ANSI when redirected.
    CHECK(output.find("Moved") != std::string::npos);  // a move actually landed.
    CHECK(output.find("Goodbye") != std::string::npos);
}

TEST_CASE("plain mode ends gracefully at end of input") {
    std::string output;
    const int code = run_plain_with("d\n", output);  // no quit command.
    CHECK(code == 0);
    CHECK(output.find("End of input") != std::string::npos);
}

TEST_CASE("unknown plain commands are reported rather than obeyed") {
    std::string output;
    const int code = run_plain_with("floop\nq\n", output);
    CHECK(code == 0);
    CHECK(output.find("Unknown command") != std::string::npos);
}

TEST_CASE("identical input produces byte-identical output streams") {
    std::string first;
    std::string second;
    const int code_a = run_plain_with("d\na\ns\nw\nq\n", first);
    const int code_b = run_plain_with("d\na\ns\nw\nq\n", second);
    CHECK(code_a == code_b);
    CHECK(first == second);
}

TEST_CASE("a long session stays bounded and terminates") {
    std::string commands;
    for (int i = 0; i < 500; ++i) {
        commands += "d\na\n";  // bounce right/left forever.
    }
    commands += "q\n";
    std::string output;
    const int code = run_plain_with(commands, output);
    CHECK(code == 0);
    CHECK(output.find("Goodbye") != std::string::npos);
}

TEST_CASE("a seeded plain session shows the safely escaped seed in its first output") {
    // TEST-016 (plain half): the initial HUD line identifies the expedition and the
    // original seed, displayed through the escaping helper.
    Settings settings;
    settings.seed_text = "glass-river";
    std::string output;
    const int code = run_plain_with("q\n", output, settings);
    CHECK(code == 0);
    CHECK(output.find("Expedition seed: \"glass-river\"") != std::string::npos);
    CHECK(output.find('\x1b') == std::string::npos);  // no ANSI when redirected.
}

TEST_CASE("a seed carrying control bytes cannot inject terminal sequences") {
    // A seed that embeds an ESC-based colour sequence must be neutralised: the raw
    // ESC never appears, only its \xHH escape does.
    Settings settings;
    std::string seed = "a";
    seed.push_back('\x1b');  // ESC
    seed += "[31m";
    settings.seed_text = seed;
    std::string output;
    const int code = run_plain_with("q\n", output, settings);
    CHECK(code == 0);
    CHECK(output.find('\x1b') == std::string::npos);
    CHECK(output.find("Expedition seed: \"a\\x1B[31m\"") != std::string::npos);
}

TEST_CASE("a seeded plain session is byte-identical across repeated runs") {
    Settings settings;
    settings.seed_text = "glass-river";
    std::string first;
    std::string second;
    const int code_a = run_plain_with("d\nd\nq\n", first, settings);
    const int code_b = run_plain_with("d\nd\nq\n", second, settings);
    CHECK(code_a == code_b);
    CHECK(first == second);
}

TEST_CASE("an unseeded plain session keeps its original welcome and no seed notice") {
    std::string output;
    const int code = run_plain_with("q\n", output);
    CHECK(code == 0);
    CHECK(output.find("Plain mode.") != std::string::npos);
    CHECK(output.find("Expedition seed:") == std::string::npos);
}

TEST_CASE("fog hides distant terrain until the actor explores toward it") {
    // TASK-020 / TEST-016: the far mountain '@' starts outside the 5x5 reveal
    // and is hidden in the initial plain frame.
    std::string hidden;
    const int code_hidden = run_plain_state(make_big_state(), "q\n", hidden);
    CHECK(code_hidden == 0);
    CHECK(hidden.find('@') == std::string::npos);

    // TEST-017: walking right three cells brings '@' into sight, so it appears.
    std::string revealed;
    const int code_revealed = run_plain_state(make_big_state(), "d\nd\nd\nq\n", revealed);
    CHECK(code_revealed == 0);
    CHECK(revealed.find('@') != std::string::npos);

    // TEST-018: after stepping back out of range the mountain is remembered, so
    // it remains present in the later plain frames rather than vanishing. The
    // reveal contributes one frame; every extra '@' comes from a remembered
    // frame after the actor left sight.
    std::string remembered;
    const int code_remembered = run_plain_state(make_big_state(), "d\nd\nd\na\na\nq\n", remembered);
    CHECK(code_remembered == 0);
    CHECK(count_char(remembered, '@') >= 2);
    CHECK(remembered.find('\x1b') == std::string::npos);  // plain stays ANSI-free.
}

TEST_CASE("entering a hill reveals a far glyph that persists as memory after leaving") {
    // TASK-015 / TEST-015..016: the distinctive water at x=5 is hidden at the
    // open spawn, appears only once the actor stands on the hill (radius 3), and
    // remains present as remembered terrain after the actor steps back off.
    std::string hidden;
    const int code_hidden = run_plain_state(make_hill_state(), "q\n", hidden);
    CHECK(code_hidden == 0);
    CHECK(hidden.find('~') == std::string::npos);

    std::string on_hill;
    const int code_hill = run_plain_state(make_hill_state(), "d\nq\n", on_hill);
    CHECK(code_hill == 0);
    CHECK(on_hill.find('~') != std::string::npos);

    // Leaving the hill keeps the water as memory, so it stays in later frames:
    // one frame from the reveal plus at least one remembered frame afterwards.
    std::string remembered;
    const int code_remembered = run_plain_state(make_hill_state(), "d\na\nq\n", remembered);
    CHECK(code_remembered == 0);
    CHECK(count_char(remembered, '~') >= 2);
    CHECK(remembered.find('\x1b') == std::string::npos);  // plain stays ANSI-free.
}

TEST_CASE("a long fields march reveals a far glyph without ever stalling") {
    // Nothing accumulates over a route, so the actor simply walks until the
    // distant water is in sight, with no block and no interruption in between.
    const std::string eleven_fields = R"(d
d
d
d
d
d
d
d
d
d
d
)";

    std::string reached;
    const int code = run_plain_state(make_mountain_reach_state(), eleven_fields + "q\n", reached);
    CHECK(code == 0);
    CHECK(reached.find("Moved onto fields. Sight 3.") != std::string::npos);
    CHECK(reached.find("Blocked") == std::string::npos);
    CHECK(reached.find('~') != std::string::npos);
    CHECK(reached.find('\x1b') == std::string::npos);
}

TEST_CASE("stepping off fields into water widens the reported sight") {
    const auto make_zero_water_state = [] {
        return GameState(make_map(R"(NAM-MAP 1
width 14
height 3
spawn 0 1
---
==============
.xxxxxxxxxx~..
==============
)"));
    };
    const std::string ten_fields = R"(d
d
d
d
d
d
d
d
d
d
)";

    std::string travelled;
    const int code = run_plain_state(make_zero_water_state(), ten_fields + "d\nq\n", travelled);
    CHECK(code == 0);
    // Fields see three cells and shallow water sees four, so the last step both
    // succeeds and reports the wider sight it bought.
    CHECK(travelled.find("Moved onto fields. Sight 3.") != std::string::npos);
    CHECK(travelled.find("Moved onto shallow water. Sight 4.") != std::string::npos);
    CHECK(travelled.find("Sight: 4") != std::string::npos);
    CHECK(travelled.find('\x1b') == std::string::npos);
}

TEST_CASE("plain mode shows the deterministic objective line from the first frame") {
    // TASK-020 / REQ-029: the seeking objective line, naming the exit_cell, appears
    // in the very first plain frame before any command.
    const std::string name = corridor_landmark_name();
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "q\n", output);
    CHECK(code == 0);
    const std::string expected = "Objective: Reach " + name + " (*).";
    CHECK(output.find(expected) != std::string::npos);
}

TEST_CASE("plain mode shows the discovery block only when a move enters the exit_cell") {
    // TASK-020 / REQ-017 / REQ-033: the exact discovery block appears on the move
    // that enters the exit_cell (3,0), not on the earlier approach moves.
    const std::string name = corridor_landmark_name();

    // One approach move never opens the discovery screen.
    std::string approach;
    run_plain_state(make_corridor_state(), "d\nq\n", approach);
    CHECK(approach.find("LANDMARK DISCOVERED") == std::string::npos);
    CHECK(approach.find("Moved onto open ground. Sight 3.") != std::string::npos);

    // The second move enters the landmark and prints the exact discovery block.
    std::string entered;
    run_plain_state(make_corridor_state(), "d\nd\nq\n", entered);
    CHECK(entered.find("LANDMARK DISCOVERED\n" + name +
                       "\nThe exit direction is now revealed."
                       "\nPress Enter to continue, or use a movement key.\n") !=
          std::string::npos);
    CHECK(entered.find('\x1b') == std::string::npos);  // plain stays ANSI-free.
}

TEST_CASE("plain discovery executes a movement command exactly once and dismisses") {
    // TASK-020 / REQ-020 / RISK-003: a movement command on the discovery screen
    // dismisses it and executes that one move, returning to gameplay.
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\na\nq\n", output);
    CHECK(code == 0);
    CHECK(count_substr(output, "Pos (1,0)") >= 2);
    CHECK(count_substr(output, "Pos (0,0)") == 1);
}

TEST_CASE("plain discovery dismisses to gameplay on an empty line without moving") {
    // TASK-020 / REQ-020: an empty line dismisses the discovery screen to the intact
    // gameplay frame without emitting an event, so the actor stays on the exit_cell.
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\n\nq\n", output);
    CHECK(code == 0);
    const std::size_t discovery = output.find("LANDMARK DISCOVERED");
    REQUIRE(discovery != std::string::npos);
    // After the discovery block a gameplay frame reappears with the unchanged
    // position on the landmark and the exit objective line.
    const std::string tail = output.substr(discovery);
    CHECK(tail.find("Pos (2,0)") != std::string::npos);
    CHECK(tail.find("Objective: Reach the exit to the east (*).") != std::string::npos);
}

TEST_CASE("plain discovery prints the reminder for unknown commands") {
    // TASK-020 / REQ-020: rest and unknown commands keep the discovery screen active
    // and print the exact reminder, emitting no event.
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\nr\nfloop\nq\n", output);
    CHECK(code == 0);
    CHECK(count_substr(output,
                       "Landmark discovered. Press Enter or use a movement command to continue.") ==
          2);
    // The reminders never advance the actor: only the final quit draws a gameplay
    // frame, so exactly one "Pos " frame follows the discovery block.
    const std::size_t discovery = output.find("LANDMARK DISCOVERED");
    REQUIRE(discovery != std::string::npos);
    const std::string tail = output.substr(discovery);
    CHECK(count_substr(tail, "Pos ") == 1);  // only the quit frame, none for reminders.
}

TEST_CASE("plain completion returns immediately and leaves trailing commands unread") {
    // REQ-009 / RISK-003: the completing move writes the report exactly once and
    // returns 0 without reading or processing any later command, so no post-
    // completion move ever runs.
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\nd\nd\nr\nq\n", output);
    CHECK(code == 0);
    CHECK(count_substr(output, "EXPEDITION REPORT") == 1);  // exactly one report.
    CHECK(count_substr(output, "Score:") == 1);
    CHECK(output.find("Moves: 7") == std::string::npos);   // no post-completion move ran.
    CHECK(output.find("Goodbye") == std::string::npos);    // no goodbye after completion.
}

TEST_CASE("plain completion exits without a goodbye on end of input") {
    // REQ-009: the completing move exits 0 and never prints a goodbye block, whether
    // or not trailing commands follow (they are unread).
    const std::string route = "d\nd\nd\n";

    std::string eof_output;
    CHECK(run_plain_state(make_corridor_state(), route, eof_output) == 0);
    CHECK(eof_output.find("Goodbye") == std::string::npos);
    CHECK(eof_output.find("End of input") == std::string::npos);

    std::string quit_output;
    CHECK(run_plain_state(make_corridor_state(), route + "q\n", quit_output) == 0);
    CHECK(quit_output.find("Goodbye") == std::string::npos);
    CHECK(eof_output == quit_output);  // the trailing q is unread, so output is identical.
}

TEST_CASE("plain single-cell map prints the report once and exits immediately") {
    // REQ-002 / REQ-009 / TEST-006: a single reachable cell starts completed, so
    // plain mode writes the full report once and returns 0 immediately without
    // reading stdin. The report scores the maximum with zero moves.
    const std::string name = single_cell_landmark_name();
    std::string output;
    const int code = run_plain_state(make_single_cell_state(), "d\nq\n", output);
    CHECK(code == 0);
    CHECK(count_substr(output, "EXPEDITION REPORT") == 1);
    CHECK(output.find("The party found " + name + " and reached the exit in 0 moves.") !=
          std::string::npos);
    CHECK(output.find("Score: 1000 (route 1000 / 1000, discoveries 0)") != std::string::npos);
    CHECK(output.find("Route: 0 moves (shortest 0)") != std::string::npos);
    CHECK(output.find("Goodbye") == std::string::npos);  // immediate quiet exit.
}

TEST_CASE("a fake interactive session pauses on discovery then completes on acknowledgement") {
    // TASK-019 / TEST-011 / REQ-016 / REQ-021: reaching the exit_cell opens discovery;
    // dismissing it with movement keys walks back to spawn and opens completion,
    // which waits for the end-of-input acknowledgement. One draw per processed input.
    const std::string name = corridor_landmark_name();
    std::vector<KeyEvent> script{
        KeyEvent::of_character('d'), KeyEvent::of_character('d'), KeyEvent::of_character('d')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_corridor_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 4);
    CHECK(session.draws == 4);
    CHECK(app.final_message() == "Level complete beyond " + name + ".");
}

TEST_CASE("a fake interactive session dismisses discovery with Enter and ignores other keys") {
    // TASK-019 / REQ-018 / TEST-010 / TEST-012: on the discovery screen, rest and
    // unknown keys are ignored (no draw), Enter dismisses to the gameplay frame, and
    // a later quit ends with the ordinary goodbye.
    std::vector<KeyEvent> script{
        KeyEvent::of_character('d'), KeyEvent::of_character('d'),
        KeyEvent::of_character('r'), KeyEvent::of_character('z'), KeyEvent::of(Key::enter),
        KeyEvent::of_character('q')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_corridor_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 6);
    // initial + two approach/discovery draws + one dismissal draw; the ignored
    // rest and unknown keys draw nothing and the quit draws nothing.
    CHECK(session.draws == 4);
    CHECK(app.final_message() == "Goodbye.");  // a pre-completion quit keeps its goodbye.
}

TEST_CASE("a fake interactive session goes directly from discovery to completion") {
    // TASK-019 / REQ-019: when the discovery-dismissing movement key completes the
    // objective, the completion screen is shown directly with no intermediate
    // gameplay frame (exactly one draw for that move).
    const std::string name = adjacent_landmark_name();
    std::vector<KeyEvent> script{KeyEvent::of_character('d'), KeyEvent::of_character('d')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_adjacent_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 3);  // discover, complete, then end-of-input acknowledgement.
    CHECK(session.draws == 3);  // initial + discovery + completion; no gameplay frame between.
    CHECK(app.final_message() == "Level complete beyond " + name + ".");
}

TEST_CASE("a fake interactive session ignores movement on the completion screen") {
    // TASK-019 / REQ-025 / RISK-004: completion ignores movement and rest keys
    // (no draw, no state change) and only an acknowledgement exits.
    const std::string name = adjacent_landmark_name();
    std::vector<KeyEvent> script{KeyEvent::of_character('d'), KeyEvent::of_character('d'),
                                 KeyEvent::of_character('d'), KeyEvent::of_character('r'),
                                 KeyEvent::of(Key::enter)};
    FakeSession session(std::move(script));

    ConsoleApp app(make_adjacent_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 5);
    CHECK(session.draws == 3);  // ignored completion movement/rest add no draw.
    CHECK(app.final_message() == "Level complete beyond " + name + ".");
}

TEST_CASE("a fake interactive session redraws the active panel on resize") {
    // TASK-019 / TEST-018 / REQ-032: a resize redraws whichever dedicated panel is
    // currently active — the discovery screen here, then the completion screen.
    std::vector<KeyEvent> discovery_script{
        KeyEvent::of_character('d'), KeyEvent::of(Key::resize), KeyEvent::of_character('q')};
    FakeSession discovery_session(std::move(discovery_script));
    ConsoleApp discovery_app(make_adjacent_state(), Settings{});
    CHECK(discovery_app.run_interactive(discovery_session) == 0);
    CHECK(discovery_session.reads == 3);
    CHECK(discovery_session.draws == 3);  // initial + discovery + resize redraw.

    std::vector<KeyEvent> completion_script{KeyEvent::of_character('d'), KeyEvent::of_character('d'),
                                            KeyEvent::of(Key::resize), KeyEvent::of(Key::enter)};
    FakeSession completion_session(std::move(completion_script));
    ConsoleApp completion_app(make_adjacent_state(), Settings{});
    CHECK(completion_app.run_interactive(completion_session) == 0);
    CHECK(completion_session.reads == 4);
    CHECK(completion_session.draws == 4);  // initial + discovery + completion + resize redraw.
}

TEST_CASE("a fake interactive session waits on the completion screen for a single-cell map") {
    // TASK-019 / TEST-017 / REQ-027: an already-completed objective starts on the
    // completion screen, reads input, ignores non-acknowledgement keys, and exits 0
    // on acknowledgement with the preserved completion message.
    const std::string name = single_cell_landmark_name();
    std::vector<KeyEvent> script{KeyEvent::of_character('d'), KeyEvent::of(Key::enter)};
    FakeSession session(std::move(script));

    ConsoleApp app(make_single_cell_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 2);  // the ignored movement key and the acknowledgement.
    CHECK(session.draws == 1);  // exactly one completion frame, redrawn for nothing else.
    CHECK(app.final_message() == "Level complete beyond " + name + ".");
}

TEST_CASE("the interactive report scrolls on arrow and page keys and redraws each time") {
    // REQ-004 / REQ-005 / REQ-006 / REQ-033: on the final report every scroll key
    // redraws exactly one frame and emits no core event; only an acknowledgement
    // exits. The completing move draws the report at (0, 0).
    const std::string name = adjacent_landmark_name();
    std::vector<KeyEvent> script{
        KeyEvent::of_character('d'), KeyEvent::of_character('d'), KeyEvent::of(Key::up),
        KeyEvent::of(Key::down),     KeyEvent::of(Key::page_up),  KeyEvent::of(Key::page_down),
        KeyEvent::of(Key::left),     KeyEvent::of(Key::right),    KeyEvent::of(Key::enter)};
    FakeSession session(std::move(script));

    ConsoleApp app(make_adjacent_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 9);
    // initial + discovery + completion report + six scroll redraws.
    CHECK(session.draws == 9);
    CHECK(app.final_message() == "Level complete beyond " + name + ".");
}

TEST_CASE("the interactive report acknowledges with q and redraws on resize") {
    // REQ-007 / REQ-008: a resize on the report reclamps and redraws it; q
    // acknowledges and exits 0 with the preserved final message.
    const std::string name = adjacent_landmark_name();
    std::vector<KeyEvent> script{KeyEvent::of_character('d'), KeyEvent::of_character('d'),
                                 KeyEvent::of(Key::resize), KeyEvent::of_character('q')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_adjacent_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 4);
    CHECK(session.draws == 4);  // initial + discovery + completion + resize redraw.
    CHECK(app.final_message() == "Level complete beyond " + name + ".");
}

TEST_CASE("the plain final report is deterministic across identical runs") {
    // REQ-024 / TEST-008: the same completed expedition produces byte-identical
    // report output on repeated runs.
    std::string first;
    std::string second;
    CHECK(run_plain_state(make_corridor_state(), "d\nd\nd\n", first) == 0);
    CHECK(run_plain_state(make_corridor_state(), "d\nd\nd\n", second) == 0);
    CHECK(first == second);
}

TEST_CASE("the plain final report escapes a hostile seed identity safely") {
    // SEC-001 / SEC-002 / REQ-025: a control-byte seed reaches the report only
    // through byte escaping, so no ESC byte ever appears in the output.
    Settings settings;
    settings.seed_text = std::string("evil\x1b[31m");
    settings.numeric_seed = 12345;
    std::string output;
    const int code = run_plain_state(make_single_cell_state(), "", output, settings);
    CHECK(code == 0);
    CHECK(output.find("EXPEDITION REPORT") != std::string::npos);
    CHECK(output.find('\x1b') == std::string::npos);      // no raw ESC leaks.
    CHECK(output.find("\\x1B") != std::string::npos);     // the ESC is shown escaped.
    CHECK(output.find("Replay: nam_console --seed-number 12345") != std::string::npos);
}

TEST_CASE("the journal opens over gameplay and dismisses back with j") {
    // TASK-016 / REQ-021 / REQ-022: j opens the journal over gameplay, j dismisses it
    // back to the gameplay frame, and a later quit keeps the ordinary goodbye.
    std::vector<KeyEvent> script{KeyEvent::of_character('j'), KeyEvent::of_character('j'),
                                 KeyEvent::of_character('q')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 3);
    CHECK(session.draws == 3);  // initial gameplay + journal + dismissal gameplay frame.
    CHECK(app.final_message() == "Goodbye.");
}

TEST_CASE("Escape dismisses the journal instead of quitting the game") {
    // TASK-016 / REQ-023: on the journal, Escape returns to the previous state; it is
    // handled before any general quit predicate. A later q then quits normally.
    std::vector<KeyEvent> script{KeyEvent::of_character('j'), KeyEvent::of(Key::escape),
                                 KeyEvent::of_character('q')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 3);
    CHECK(session.draws == 3);  // initial + journal + dismissal; the quit draws nothing.
    CHECK(app.final_message() == "Goodbye.");
}

TEST_CASE("journal scrolling keys each redraw and emit no core event") {
    // TASK-016 / REQ-024 / REQ-033: Up/Down and Page Up/Page Down each redraw the
    // journal without moving, completing, or otherwise mutating state; the run ends
    // with the ordinary goodbye and never reaches completion.
    std::vector<KeyEvent> script{
        KeyEvent::of_character('d'),  KeyEvent::of_character('j'), KeyEvent::of(Key::up),
        KeyEvent::of(Key::down),      KeyEvent::of(Key::page_up),  KeyEvent::of(Key::page_down),
        KeyEvent::of_character('j'),  KeyEvent::of_character('q')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 8);
    // initial + one move + open journal + four scroll redraws + dismissal frame.
    CHECK(session.draws == 8);
    CHECK(app.final_message() == "Goodbye.");
}

TEST_CASE("the journal opens over discovery and returns to the discovery screen") {
    // TASK-016 / REQ-022: opening the journal over discovery never dismisses it; the
    // discovery screen is restored intact and a later quit keeps its goodbye.
    std::vector<KeyEvent> script{KeyEvent::of_character('d'), KeyEvent::of_character('j'),
                                 KeyEvent::of(Key::escape), KeyEvent::of_character('q')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_adjacent_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 4);
    // initial + discovery + journal + discovery restored; the quit draws nothing.
    CHECK(session.draws == 4);
    CHECK(app.final_message() == "Goodbye.");
}

TEST_CASE("end of input on a journal over gameplay uses the goodbye wording") {
    // TASK-016 / REQ-026: EOF while the journal is open over gameplay restores the
    // previous state and keeps the ordinary end-of-input goodbye.
    std::vector<KeyEvent> script{KeyEvent::of_character('j')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 2);  // the journal open plus the end-of-input read.
    CHECK(session.draws == 2);  // initial gameplay + journal.
    CHECK(app.final_message() == "End of input. Goodbye.");
}

TEST_CASE("the journal redraws on resize while open") {
    // TASK-016 / REQ-024: a resize while the journal is open reclamps and redraws the
    // journal frame rather than the underlying screen.
    std::vector<KeyEvent> script{KeyEvent::of_character('j'), KeyEvent::of(Key::resize),
                                 KeyEvent::of_character('j'), KeyEvent::of_character('q')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 4);
    CHECK(session.draws == 4);  // initial + journal + resize redraw + dismissal frame.
    CHECK(app.final_message() == "Goodbye.");
}

TEST_CASE("plain j prints the journal once and immediately resumes gameplay") {
    // TASK-016 / REQ-027: a plain journal command prints one complete journal block
    // and continues without printing an extra gameplay frame.
    std::string output;
    const int code = run_plain_with("j\nq\n", output);
    CHECK(code == 0);
    CHECK(output.find("EXPEDITION JOURNAL") != std::string::npos);
    CHECK(output.find("(No journal entries yet.)") != std::string::npos);
    // Only the initial frame and the quit frame carry a status line; j adds none.
    CHECK(count_substr(output, "Pos ") == 2);
}

TEST_CASE("plain j records prior moves and does not dismiss discovery") {
    // TASK-016 / REQ-028: on the discovery screen a plain journal command prints the
    // block (including the recorded discovery milestone) without dismissing
    // discovery, acknowledging, or emitting an event.
    const std::string name = corridor_landmark_name();
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\nj\nq\n", output);
    CHECK(code == 0);
    CHECK(output.find("EXPEDITION JOURNAL") != std::string::npos);
    CHECK(output.find("Sighted " + name +
                      "; the land opened up and the exit direction was revealed.") !=
          std::string::npos);
}

TEST_CASE("plain j on a completed single-cell map prints the initial completion entry") {
    // TASK-016 / REQ-028 / REQ-012: the journal on an already-completed map shows the
    // explicit initial-completion entry and printing it does not re-acknowledge.
    const std::string name = single_cell_landmark_name();
    std::string output;
    const int code = run_plain_state(make_single_cell_state(), "j\n", output);
    CHECK(code == 0);
    CHECK(output.find("EXPEDITION JOURNAL") != std::string::npos);
    CHECK(output.find("Found " + name + " and the exit at spawn; the level was already complete.") !=
          std::string::npos);
    CHECK(output.find("Goodbye") == std::string::npos);  // EOF acknowledgement stays quiet.
}

}  // TEST_SUITE("console")

namespace {

// The plain-mode command letter for one cardinal step.
[[nodiscard]] std::string plain_command_for(Direction direction) {
    switch (direction) {
        case Direction::up:    return "w\n";
        case Direction::down:  return "s\n";
        case Direction::left:  return "a\n";
        case Direction::right: return "d\n";
    }
    return "w\n";
}

// Plain-mode commands that walk a copy of the expedition from spawn through the
// landmark to the exit of every level, acknowledging each interlude with a blank
// line. Driving a copy keeps the real app's state untouched.
// The route a level asks for: every discovery when `thorough`, then the landmark,
// then the exit. Read fresh for each level because the state is replaced on an
// advance.
[[nodiscard]] std::vector<Coordinates> targets_for_level(const Expedition& scout, bool thorough) {
    std::vector<Coordinates> targets;
    if (thorough) {
        for (const LevelFeature& feature : scout.state().features()) {
            if (feature.kind == LevelFeatureKind::discovery) targets.push_back(feature.position);
        }
    }
    targets.push_back(scout.state().objective().landmark);
    targets.push_back(scout.state().objective().exit_cell);
    return targets;
}

[[nodiscard]] std::string commands_to_finish(std::uint64_t seed, bool thorough = false) {
    Expedition scout(seed);
    std::string commands;
    for (std::uint32_t level = 0; level < scout.total_levels(); ++level) {
        for (const Coordinates target : targets_for_level(scout, thorough)) {
            while (scout.state().actor_position() != target) {
                const std::vector<Coordinates> path =
                    shortest_path(scout.state().map(), scout.state().actor_position(), target);
                REQUIRE(path.size() >= 2u);
                for (std::size_t step = 1; step < path.size(); ++step) {
                    const Coordinates delta{path[step].x - path[step - 1u].x,
                                            path[step].y - path[step - 1u].y};
                    const std::optional<Direction> direction = direction_of(delta);
                    REQUIRE(direction.has_value());
                    commands += plain_command_for(*direction);
                    static_cast<void>(scout.state().move(*direction));
                }
            }
        }
        commands += "\n";  // Acknowledge the interlude or the final report.
        static_cast<void>(scout.complete_level(LevelPerformance{}));
    }
    return commands;
}

constexpr std::uint64_t kExpeditionSeed = 0x0F4289EAF4A1813Cull;

// The same walk as commands_to_finish, as interactive key events. Interludes are
// acknowledged with Enter.
[[nodiscard]] std::vector<KeyEvent> events_to_finish(std::uint64_t seed) {
    std::vector<KeyEvent> events;
    for (const char letter : commands_to_finish(seed)) {
        if (letter == '\n') {
            events.push_back(KeyEvent::of(Key::enter));
        } else {
            events.push_back(KeyEvent::of_character(letter));
        }
    }
    return events;
}

}  // namespace

TEST_SUITE("console") {

TEST_CASE("a plain expedition reports each level and continues into the next") {
    ConsoleApp app(Expedition(kExpeditionSeed), Settings{});
    std::istringstream input(commands_to_finish(kExpeditionSeed));
    std::ostringstream out;

    CHECK(app.run_plain(input, out) == 0);
    const std::string output = out.str();

    // The interlude names the tier still to come; the final report closes the run.
    CHECK(output.find("Result: Medium level ahead. 1 of 2 complete.") != std::string::npos);
    CHECK(output.find("Result: expedition complete. All 2 levels finished.") !=
          std::string::npos);
    // The second level is announced with its tier and position in the chain.
    CHECK(output.find("Medium level (2 of 2).") != std::string::npos);
    CHECK(count_substr(output, "EXPEDITION REPORT") == 2u);
}

TEST_CASE("the expedition score accumulates across levels in the final report") {
    ConsoleApp app(Expedition(kExpeditionSeed), Settings{});
    std::istringstream input(commands_to_finish(kExpeditionSeed));
    std::ostringstream out;
    REQUIRE(app.run_plain(input, out) == 0);
    const std::string output = out.str();

    // Every level report closes with the running expedition section.
    CHECK(count_substr(output, "EXPEDITION\n") == 2u);
    CHECK(output.find(" over 1 of 2 levels") != std::string::npos);
    CHECK(output.find(" over 2 of 2 levels") != std::string::npos);
}

TEST_CASE("a thorough run logs its discoveries and keeps the journal within budget") {
    ConsoleApp app(Expedition(kExpeditionSeed), Settings{});
    std::istringstream input(commands_to_finish(kExpeditionSeed, /*thorough=*/true));
    std::ostringstream out;
    REQUIRE(app.run_plain(input, out) == 0);
    const std::string output = out.str();

    CHECK(output.find("Discoveries: 2 / 2") != std::string::npos);
    CHECK(output.find("Bonus earned: keen eye.") != std::string::npos);
    CHECK(output.find("Bonus spent: keen eye doubled this level's discoveries.") !=
          std::string::npos);

    // The journal is the expedition-wide record, so the final report holds every
    // level's entries. Both levels place one discovery and visiting them both is
    // what earns the carried bonus.
    const std::size_t journal = output.rfind("EXPEDITION JOURNAL");
    REQUIRE(journal != std::string::npos);
    const std::string final_journal = output.substr(journal);
    CHECK(count_substr(final_journal, "Found a hidden site off the route (1 of 1).") == 2u);

    // Three entries per level keeps a four-tier run near its twelve-entry budget.
    CHECK(count_substr(final_journal, "\n6. ") == 1u);
    CHECK(count_substr(final_journal, "\n7. ") == 0u);
}

TEST_CASE("quitting at an interlude ends the run without playing the next level") {
    // Walk the first level only, then quit at its report instead of acknowledging.
    std::string commands = commands_to_finish(kExpeditionSeed);
    const std::size_t first_interlude = commands.find("\n\n");
    REQUIRE(first_interlude != std::string::npos);
    commands = commands.substr(0, first_interlude + 1u) + "q\n";

    ConsoleApp app(Expedition(kExpeditionSeed), Settings{});
    std::istringstream input(commands);
    std::ostringstream out;
    REQUIRE(app.run_plain(input, out) == 0);
    const std::string output = out.str();

    CHECK(count_substr(output, "EXPEDITION REPORT") == 1u);
    CHECK(output.find("Result: expedition complete") == std::string::npos);
}

TEST_CASE("an interactive interlude returns to gameplay on the next level") {
    ConsoleApp app(Expedition(kExpeditionSeed), Settings{});
    FakeSession session(events_to_finish(kExpeditionSeed));

    CHECK(app.run_interactive(session) == 0);

    // The run ended on the final report, so the restored line is the completion
    // message rather than a goodbye.
    CHECK(app.final_message().find("Level complete beyond ") == 0u);

    std::string frames;
    for (const Frame& frame : session.frames) {
        for (const std::string& row : frame) {
            frames += row;
            frames += '\n';
        }
    }
    CHECK(count_substr(frames, "EXPEDITION REPORT") >= 2u);
    CHECK(frames.find("Medium level (2 of 2).") != std::string::npos);
}

TEST_CASE("a standalone level is a one-level expedition with no carryover lines") {
    std::string output;
    REQUIRE(run_plain_state(make_corridor_state(), "d\nd\nd\n", output) == 0);

    CHECK(output.find("Result: level complete.") != std::string::npos);
    CHECK(output.find("Expedition score:") == std::string::npos);
    CHECK(output.find("level ahead") == std::string::npos);
}

}  // TEST_SUITE("console")
