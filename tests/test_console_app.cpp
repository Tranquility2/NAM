#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "console_app.h"
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

// A room the actor can move around in horizontally. Its deterministic beacon sits
// at the far right (7,1), well beyond the reach of the short movement scripts
// below, so ordinary movement and quit tests never enter the discovery flow.
GameState make_state() {
    return GameState(
        make_map("NAM-MAP 1\nwidth 9\nheight 3\nspawn 2 1\n---\n=========\n|.......|\n=========\n"));
}

// A room wider than the initial 5x5 reveal, with a distinctive mountain '@' far
// to the right (walkable, so the actor can approach it) that starts hidden.
GameState make_big_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 9\nheight 3\nspawn 1 1\n---\n=========\n......@..\n=========\n"));
}

// A one-lane mountain corridor for stamina integration. With the 20-stamina cap,
// five mountain steps drain to zero and the sixth mountain is unaffordable. A
// distant water glyph at x=10 stays hidden at that blocking point.
GameState make_cost_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 11\nheight 3\nspawn 0 1\n---\n===========\n.@@@@@@...~\n===========\n"));
}

// A corridor with an open spawn at x=1, a hill at x=2, and a distinctive water
// cell at x=5. The water sits at radius 3 from the hill but beyond radius 2 from
// either the open spawn or the hill cell if it were flat, so only standing on
// the hill reveals it. Stepping back off the hill keeps it as memory.
GameState make_hill_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 9\nheight 3\nspawn 1 1\n---\n=========\n..^..~...\n=========\n"));
}

// A long corridor where four mountains then four open tiles drain stamina to zero
// on open ground; the next mountain is blocked until one open-ground rest recovers
// enough stamina. A distant water glyph at x=13 becomes visible only after that
// mountain is entered.
GameState make_mountain_reach_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 14\nheight 3\nspawn 0 1\n---\n==============\n.@@@@....@...~\n==============\n"));
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

// A stranded-recovery map: three four-cost mountains drain 12 -> 8 -> 4 -> 0 from
// the left spawn, then a water cell (cost 3) sits immediately after. Resting from
// zero recovers 4, which is exactly enough to enter the water and leave 1.
GameState make_rest_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 6\nheight 3\nspawn 0 1\n---\n======\n.@@@~.\n======\n"));
}

// A one-row open corridor whose deterministic beacon is the distant cell (3,0),
// so it exercises discovery and completion. Walking right three cells discovers
// it; walking back three cells returns to spawn and completes the expedition.
GameState make_corridor_state() {
    return GameState(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
}

// The deterministic beacon name of the corridor map, read from an independent
// GameState so expectations use the exact generated name.
std::string corridor_beacon_name() {
    return make_corridor_state().objective().name;
}

// A single-reachable-cell map: the spawn is sealed by a wall, so the beacon is at
// spawn and the objective starts completed.
GameState make_single_cell_state() {
    return GameState(make_map("NAM-MAP 1\nwidth 3\nheight 1\nspawn 0 0\n---\n.=.\n"));
}

std::string single_cell_beacon_name() {
    return make_single_cell_state().objective().name;
}

// A two-cell corridor whose beacon is the single distant cell (1,0), immediately
// right of spawn. Reaching it opens discovery; the discovery-dismissing left move
// returns straight to spawn, so it exercises a direct discovery-to-completion.
GameState make_adjacent_state() {
    return GameState(make_map("NAM-MAP 1\nwidth 2\nheight 1\nspawn 0 0\n---\n..\n"));
}

std::string adjacent_beacon_name() {
    return make_adjacent_state().objective().name;
}

GameState make_rescue_state() {
    return GameState(make_map("NAM-MAP 1\nwidth 5\nheight 1\nspawn 0 0\n---\n.....\n"));
}

std::string rescue_beacon_name() {
    return make_rescue_state().objective().name;
}

std::string stranded_plain_commands() {
    GameState probe = make_rescue_state();
    std::string commands;
    for (std::uint32_t i = 0; i < probe.starting_provisions(); ++i) {
        commands += "d\na\nr\n";
    }
    for (std::uint32_t i = 0; i < GameState::maximum_stamina; ++i) {
        commands += (i % 2 == 0) ? "d\n" : "a\n";
    }
    commands += "d\n";  // blocked at zero stamina with no provisions -> stranded.
    return commands;
}

std::vector<KeyEvent> stranded_interactive_events() {
    GameState probe = make_rescue_state();
    std::vector<KeyEvent> events;
    for (std::uint32_t i = 0; i < probe.starting_provisions(); ++i) {
        events.push_back(KeyEvent::of_character('d'));
        events.push_back(KeyEvent::of_character('a'));
        events.push_back(KeyEvent::of_character('r'));
    }
    for (std::uint32_t i = 0; i < GameState::maximum_stamina; ++i) {
        events.push_back(KeyEvent::of_character((i % 2 == 0) ? 'd' : 'a'));
    }
    events.push_back(KeyEvent::of_character('d'));
    return events;
}

bool frame_contains(const Frame& frame, const std::string& needle) {
    for (const std::string& row : frame) {
        if (row.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::size_t frame_count_with(const std::vector<Frame>& frames, const std::string& needle) {
    std::size_t count = 0;
    for (const Frame& frame : frames) {
        if (frame_contains(frame, needle)) {
            ++count;
        }
    }
    return count;
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
    // The journal key is never mistaken for quit or rest.
    CHECK_FALSE(is_quit_event(KeyEvent::of_character('j')));
    CHECK_FALSE(is_rest_event(KeyEvent::of_character('j')));
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

TEST_CASE("rest is recognised from lower- and upper-case r only") {
    CHECK(is_rest_event(KeyEvent::of_character('r')));
    CHECK(is_rest_event(KeyEvent::of_character('R')));
    CHECK_FALSE(is_rest_event(KeyEvent::of_character('q')));
    CHECK_FALSE(is_rest_event(KeyEvent::of_character('d')));  // 'd'/'l' stay movement.
    CHECK_FALSE(is_rest_event(KeyEvent::of_character('l')));
    CHECK_FALSE(is_rest_event(KeyEvent::of(Key::right)));
    CHECK_FALSE(is_rest_event(KeyEvent::of(Key::escape)));
    // A rest key is never mistaken for a movement direction.
    CHECK_FALSE(direction_for(KeyEvent::of_character('r')).has_value());
    CHECK_FALSE(direction_for(KeyEvent::of_character('R')).has_value());
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
    // TEST-016 (plain half): the initial HUD line identifies Tiny World and the
    // original seed, displayed through the escaping helper.
    Settings settings;
    settings.seed_text = "glass-river";
    std::string output;
    const int code = run_plain_with("q\n", output, settings);
    CHECK(code == 0);
    CHECK(output.find("Tiny World seed: \"glass-river\"") != std::string::npos);
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
    CHECK(output.find("Tiny World seed: \"a\\x1B[31m\"") != std::string::npos);
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
    CHECK(output.find("Tiny World seed:") == std::string::npos);
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

TEST_CASE("an unaffordable mountain keeps a far glyph hidden until a rest lets the actor enter") {
    std::string blocked;
    const int code_blocked = run_plain_state(make_mountain_reach_state(),
                                             R"(d
d
d
d
d
d
d
d
d
q
)", blocked);
    CHECK(code_blocked == 0);
    CHECK(blocked.find("Not enough stamina for mountain: need 4, have 0.") != std::string::npos);
    CHECK(blocked.find('~') == std::string::npos);

    std::string reached;
    const int code_reached = run_plain_state(make_mountain_reach_state(),
                                             R"(d
d
d
d
d
d
d
d
r
d
q
)", reached);
    CHECK(code_reached == 0);
    CHECK(reached.find("Made camp on open ground and recovered 4 stamina. Provisions left:") !=
          std::string::npos);
    CHECK(reached.find("Moved onto mountain for 4 stamina.") != std::string::npos);
    CHECK(reached.find('~') != std::string::npos);
    CHECK(reached.find('\x1b') == std::string::npos);
}

TEST_CASE("a scripted high-cost route shows costs drains stamina and blocks deterministically") {
    std::string output;
    const int code = run_plain_state(make_cost_state(),
                                     R"(d
d
d
d
d
d
q
)", output);
    CHECK(code == 0);

    CHECK(output.find("Moved onto mountain for 4 stamina.") != std::string::npos);
    CHECK(output.find("Stamina: 16/20") != std::string::npos);
    CHECK(output.find("Stamina: 12/20") != std::string::npos);
    CHECK(output.find("Stamina: 8/20") != std::string::npos);
    CHECK(output.find("Stamina: 4/20") != std::string::npos);
    CHECK(output.find("Stamina: 0/20") != std::string::npos);
    CHECK(output.find("Not enough stamina for mountain: need 4, have 0.") != std::string::npos);
    CHECK(output.find('~') == std::string::npos);
    CHECK(output.find('\x1b') == std::string::npos);

    std::string second;
    const int code_again = run_plain_state(make_cost_state(),
                                           R"(d
d
d
d
d
d
q
)", second);
    CHECK(code_again == code);
    CHECK(second == output);
}

TEST_CASE("plain mode recognises both r and rest as the rest command") {
    std::string spelled;
    const int a = run_plain_state(make_rest_state(), R"(rest
q
)", spelled);
    CHECK(a == 0);
    CHECK(spelled.find("A heroic rest is attempted. Your stamina remains heroically full.") !=
          std::string::npos);
    CHECK(spelled.find("Unknown command") == std::string::npos);

    std::string letter;
    const int b = run_plain_state(make_rest_state(), R"(r
q
)", letter);
    CHECK(b == 0);
    CHECK(letter.find("A heroic rest is attempted. Your stamina remains heroically full.") !=
          std::string::npos);
    CHECK(letter.find("Unknown command") == std::string::npos);
}

TEST_CASE("resting from zero lets the actor complete the next water move") {
    const auto make_zero_water_state = [] {
        return GameState(make_map(R"(NAM-MAP 1
width 10
height 3
spawn 0 1
---
==========
.@@@@....~
==========
)"));
    };

    std::string blocked_first;
    const int code_blocked = run_plain_state(make_zero_water_state(),
                                             R"(d
d
d
d
d
d
d
d
d
q
)", blocked_first);
    CHECK(code_blocked == 0);
    CHECK(blocked_first.find("Stamina: 0/20") != std::string::npos);
    CHECK(blocked_first.find("Not enough stamina for water: need 3, have 0.") !=
          std::string::npos);

    std::string recovered;
    const int code = run_plain_state(make_zero_water_state(),
                                     R"(d
d
d
d
d
d
d
d
r
d
q
)", recovered);
    CHECK(code == 0);
    CHECK(recovered.find("Stamina: 1/20") != std::string::npos);
    CHECK(recovered.find('\x1b') == std::string::npos);
}

TEST_CASE("one rest command produces exactly one additional plain frame") {

    // Each plain command emits one frame; every frame carries exactly one "Pos "
    // status line, so counting them counts frames. Adding one rest command adds
    // exactly one frame relative to an otherwise identical session.
    std::string one_rest;
    run_plain_state(make_rest_state(), "r\nq\n", one_rest);
    const std::size_t frames_one = count_substr(one_rest, "Pos ");

    std::string two_rest;
    run_plain_state(make_rest_state(), "r\nr\nq\n", two_rest);
    const std::size_t frames_two = count_substr(two_rest, "Pos ");

    // Adding exactly one more rest command adds exactly one more frame.
    CHECK(frames_two == frames_one + 1);
}

TEST_CASE("a rest never leaves a marker in the recent-move history") {
    // Debug plain frames expose the recent line. A blocked wall bump and a rest
    // both leave the route history untouched; only the successful move appears.
    Settings settings;
    settings.debug = true;
    std::string output;
    // From spawn (0,1): 'a' bumps the left wall boundary (blocked), 'd' moves
    // right onto a mountain (successful), 'r' rests. Only the successful right
    // move (R) should appear in the recent line.
    const int code = run_plain_state(make_rest_state(), "a\nd\nr\nq\n", output, settings);
    CHECK(code == 0);
    const std::size_t recent = output.rfind("Recent:");
    REQUIRE(recent != std::string::npos);
    const std::string tail = output.substr(recent);
    const std::string recent_line = tail.substr(0, tail.find('\n'));
    CHECK(recent_line.find('R') != std::string::npos);   // the successful move.
    CHECK(recent_line.find('L') == std::string::npos);   // no blocked-left marker.
    CHECK(recent_line.find('l') == std::string::npos);   // no lower-case blocked marker.
}

TEST_CASE("repeated rest sessions are byte-identical and ANSI-free") {
    std::string first;
    std::string second;
    const int code_a = run_plain_state(make_rest_state(), "d\nd\nd\nr\nd\nq\n", first);
    const int code_b = run_plain_state(make_rest_state(), "d\nd\nd\nr\nd\nq\n", second);
    CHECK(code_a == code_b);
    CHECK(first == second);
    CHECK(first.find('\x1b') == std::string::npos);
}

TEST_CASE("plain mode shows the deterministic objective line from the first frame") {
    // TASK-020 / REQ-029: the seeking objective line, naming the beacon, appears
    // in the very first plain frame before any command.
    const std::string name = corridor_beacon_name();
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "q\n", output);
    CHECK(code == 0);
    const std::string expected = "Objective: Reach " + name + " (*), then return to spawn.";
    CHECK(output.find(expected) != std::string::npos);
}

TEST_CASE("plain mode shows the discovery block only when a move enters the beacon") {
    // TASK-020 / REQ-017 / REQ-033: the exact discovery block appears on the move
    // that enters the beacon (3,0), not on the earlier approach moves.
    const std::string name = corridor_beacon_name();

    // Two approach moves (to x=2) never open the discovery screen.
    std::string approach;
    run_plain_state(make_corridor_state(), "d\nd\nq\n", approach);
    CHECK(approach.find("BEACON DISCOVERED") == std::string::npos);
    CHECK(approach.find("Moved onto open ground for 1 stamina.") != std::string::npos);

    // The third move enters the beacon and prints the exact discovery block.
    std::string entered;
    run_plain_state(make_corridor_state(), "d\nd\nd\nq\n", entered);
    CHECK(entered.find("BEACON DISCOVERED\n" + name +
                       "\nReturn to spawn to complete the expedition."
                       "\nPress Enter to continue, or use a movement key.\n") !=
          std::string::npos);
    CHECK(entered.find('\x1b') == std::string::npos);  // plain stays ANSI-free.
}

TEST_CASE("plain discovery executes a movement command exactly once and dismisses") {
    // TASK-020 / REQ-020 / RISK-003: a movement command on the discovery screen
    // dismisses it and executes that one move, returning to gameplay.
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\nd\na\nq\n", output);
    CHECK(code == 0);
    // The beacon is at (3,0); the single dismissing left move lands on (2,0). If the
    // key had executed twice the actor would reach (1,0) again, so (1,0) — which the
    // outbound first move already visited exactly once — must appear only once.
    CHECK(count_substr(output, "Pos (2,0)") >= 2);      // outbound and the dismiss move.
    CHECK(count_substr(output, "Pos (1,0)") == 1);      // never revisited: one execution.
}

TEST_CASE("plain discovery dismisses to gameplay on an empty line without moving") {
    // TASK-020 / REQ-020: an empty line dismisses the discovery screen to the intact
    // gameplay frame without emitting an event, so the actor stays on the beacon.
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\nd\n\nq\n", output);
    CHECK(code == 0);
    const std::size_t discovery = output.find("BEACON DISCOVERED");
    REQUIRE(discovery != std::string::npos);
    // After the discovery block a gameplay frame reappears with the unchanged
    // position on the beacon and the returning objective line.
    const std::string tail = output.substr(discovery);
    CHECK(tail.find("Pos (3,0)") != std::string::npos);
    CHECK(tail.find("Objective: Return to spawn.") != std::string::npos);
}

TEST_CASE("plain discovery prints the reminder for rest and unknown commands") {
    // TASK-020 / REQ-020: rest and unknown commands keep the discovery screen active
    // and print the exact reminder, emitting no event.
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\nd\nr\nfloop\nq\n", output);
    CHECK(code == 0);
    CHECK(count_substr(output,
                       "Beacon discovered. Press Enter or use a movement command to continue.") ==
          2);
    // The reminders never advance the actor: only the final quit draws a gameplay
    // frame, so exactly one "Pos " frame follows the discovery block.
    const std::size_t discovery = output.find("BEACON DISCOVERED");
    REQUIRE(discovery != std::string::npos);
    const std::string tail = output.substr(discovery);
    CHECK(count_substr(tail, "Pos ") == 1);  // only the quit frame, none for reminders.
}

TEST_CASE("plain completion prints the full report including the completing move") {
    // REQ-009 / REQ-011 / TEST-006: walking out and back completes the expedition
    // and prints the final report whose counts and final stamina include the
    // completing move (six open-ground steps drain 12 -> 6). Optimal round trip is
    // six, so the score is the maximum.
    const std::string name = corridor_beacon_name();
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\nd\na\na\na\n", output);
    CHECK(code == 0);
    CHECK(output.find("EXPEDITION REPORT") != std::string::npos);
    CHECK(output.find("The " + name + " expedition is complete.") != std::string::npos);
    CHECK(output.find("Score: 1000 / 1000") != std::string::npos);
    CHECK(output.find("Moves: 6") != std::string::npos);
    CHECK(output.find("Move attempts: 6") != std::string::npos);
    CHECK(output.find("Blocked moves: 0") != std::string::npos);
    CHECK(output.find("Provisions used: 0") != std::string::npos);
    CHECK(output.find("Final stamina: 14/20") != std::string::npos);
    CHECK(output.find("Optimal round-trip cost: 6") != std::string::npos);
    CHECK(output.find("ROUTE MAP") != std::string::npos);
    CHECK(output.find("EXPEDITION JOURNAL") != std::string::npos);
    CHECK(output.find('\x1b') == std::string::npos);
}

TEST_CASE("plain completion returns immediately and leaves trailing commands unread") {
    // REQ-009 / RISK-003: the completing move writes the report exactly once and
    // returns 0 without reading or processing any later command, so no post-
    // completion move ever runs.
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\nd\na\na\na\nd\nr\nq\n", output);
    CHECK(code == 0);
    CHECK(count_substr(output, "EXPEDITION REPORT") == 1);  // exactly one report.
    CHECK(count_substr(output, "Score:") == 1);
    CHECK(output.find("Moves: 7") == std::string::npos);   // no post-completion move ran.
    CHECK(output.find("Goodbye") == std::string::npos);    // no goodbye after completion.
}

TEST_CASE("plain completion exits without a goodbye on end of input") {
    // REQ-009: the completing move exits 0 and never prints a goodbye block, whether
    // or not trailing commands follow (they are unread).
    const std::string route = "d\nd\nd\na\na\na\n";

    std::string eof_output;
    CHECK(run_plain_state(make_corridor_state(), route, eof_output) == 0);
    CHECK(eof_output.find("Goodbye") == std::string::npos);
    CHECK(eof_output.find("End of input") == std::string::npos);

    std::string quit_output;
    CHECK(run_plain_state(make_corridor_state(), route + "q\n", quit_output) == 0);
    CHECK(quit_output.find("Goodbye") == std::string::npos);
    CHECK(eof_output == quit_output);  // the trailing q is unread, so output is identical.
}

TEST_CASE("plain mode rescue prints rescue block then rescued report and exits zero") {
    std::string output;
    const int code = run_plain_state(make_rescue_state(), stranded_plain_commands(), output);
    CHECK(code == 0);

    const std::size_t rescue = output.find("RESCUE REQUESTED\n");
    const std::size_t report = output.find("EXPEDITION REPORT");
    REQUIRE(rescue != std::string::npos);
    REQUIRE(report != std::string::npos);
    CHECK(rescue < report);
    CHECK(output.find("Result: rescued after running out of provisions.") != std::string::npos);
    CHECK(output.find("/ 750") != std::string::npos);
    CHECK(output.find("Goodbye") == std::string::npos);
}

TEST_CASE("manual quit before any ending prints no report") {
    std::string output;
    CHECK(run_plain_state(make_rescue_state(), "q\n", output) == 0);
    CHECK(output.find("EXPEDITION REPORT") == std::string::npos);
}

TEST_CASE("plain single-cell map prints the report once and exits immediately") {
    // REQ-002 / REQ-009 / TEST-006: a single reachable cell starts completed, so
    // plain mode writes the full report once and returns 0 immediately without
    // reading stdin. The report scores the maximum with zero moves.
    const std::string name = single_cell_beacon_name();
    std::string output;
    const int code = run_plain_state(make_single_cell_state(), "d\nq\n", output);
    CHECK(code == 0);
    CHECK(count_substr(output, "EXPEDITION REPORT") == 1);
    CHECK(output.find("The " + name + " expedition is complete.") != std::string::npos);
    CHECK(output.find("Score: 1000 / 1000") != std::string::npos);
    CHECK(output.find("Moves: 0") != std::string::npos);
    CHECK(output.find("Final stamina: 20/20") != std::string::npos);
    CHECK(output.find("Goodbye") == std::string::npos);  // immediate quiet exit.
}

TEST_CASE("a fake interactive session pauses on discovery then completes on acknowledgement") {
    // TASK-019 / TEST-011 / REQ-016 / REQ-021: reaching the beacon opens discovery;
    // dismissing it with movement keys walks back to spawn and opens completion,
    // which waits for the end-of-input acknowledgement. One draw per processed input.
    const std::string name = corridor_beacon_name();
    std::vector<KeyEvent> script{
        KeyEvent::of_character('d'), KeyEvent::of_character('d'), KeyEvent::of_character('d'),
        KeyEvent::of_character('a'), KeyEvent::of_character('a'), KeyEvent::of_character('a')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_corridor_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 7);  // six commands plus the end-of-input acknowledgement.
    CHECK(session.draws == 7);  // initial frame plus one per command (discovery/completion).
    CHECK(app.final_message() == "Expedition complete: " + name + ".");
}

TEST_CASE("a fake interactive session dismisses discovery with Enter and ignores other keys") {
    // TASK-019 / REQ-018 / TEST-010 / TEST-012: on the discovery screen, rest and
    // unknown keys are ignored (no draw), Enter dismisses to the gameplay frame, and
    // a later quit ends with the ordinary goodbye.
    std::vector<KeyEvent> script{
        KeyEvent::of_character('d'), KeyEvent::of_character('d'), KeyEvent::of_character('d'),
        KeyEvent::of_character('r'), KeyEvent::of_character('z'), KeyEvent::of(Key::enter),
        KeyEvent::of_character('q')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_corridor_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 7);
    // initial + three approach/discovery draws + one dismissal draw; the ignored
    // rest and unknown keys draw nothing and the quit draws nothing.
    CHECK(session.draws == 5);
    CHECK(app.final_message() == "Goodbye.");  // a pre-completion quit keeps its goodbye.
}

TEST_CASE("a fake interactive session goes directly from discovery to completion") {
    // TASK-019 / REQ-019: when the discovery-dismissing movement key completes the
    // objective, the completion screen is shown directly with no intermediate
    // gameplay frame (exactly one draw for that move).
    const std::string name = adjacent_beacon_name();
    std::vector<KeyEvent> script{KeyEvent::of_character('d'), KeyEvent::of_character('a')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_adjacent_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 3);  // discover, complete, then end-of-input acknowledgement.
    CHECK(session.draws == 3);  // initial + discovery + completion; no gameplay frame between.
    CHECK(app.final_message() == "Expedition complete: " + name + ".");
}

TEST_CASE("a fake interactive session ignores movement and rest on the completion screen") {
    // TASK-019 / REQ-025 / RISK-004: completion ignores movement and rest keys
    // (no draw, no state change) and only an acknowledgement exits.
    const std::string name = adjacent_beacon_name();
    std::vector<KeyEvent> script{KeyEvent::of_character('d'), KeyEvent::of_character('a'),
                                 KeyEvent::of_character('d'), KeyEvent::of_character('r'),
                                 KeyEvent::of(Key::enter)};
    FakeSession session(std::move(script));

    ConsoleApp app(make_adjacent_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 5);
    CHECK(session.draws == 3);  // ignored completion movement/rest add no draw.
    CHECK(app.final_message() == "Expedition complete: " + name + ".");
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

    std::vector<KeyEvent> completion_script{KeyEvent::of_character('d'), KeyEvent::of_character('a'),
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
    const std::string name = single_cell_beacon_name();
    std::vector<KeyEvent> script{KeyEvent::of_character('d'), KeyEvent::of(Key::enter)};
    FakeSession session(std::move(script));

    ConsoleApp app(make_single_cell_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 2);  // the ignored movement key and the acknowledgement.
    CHECK(session.draws == 1);  // exactly one completion frame, redrawn for nothing else.
    CHECK(app.final_message() == "Expedition complete: " + name + ".");
}

TEST_CASE("interactive rescue enters report on Enter and preserves rescued final message") {
    const std::string name = rescue_beacon_name();
    std::vector<KeyEvent> script = stranded_interactive_events();
    script.push_back(KeyEvent::of(Key::enter));  // rescue -> report
    script.push_back(KeyEvent::of(Key::enter));  // acknowledge report

    FakeSession session(std::move(script));
    ConsoleApp app(make_rescue_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(app.final_message() ==
          "Rescued: the " + name + " expedition ran out of provisions and ended early.");
    CHECK(frame_count_with(session.frames, "RESCUE REQUESTED") >= 1);
    CHECK(frame_count_with(session.frames, "EXPEDITION REPORT") >= 1);
}

TEST_CASE("interactive journal opened from rescue restores the rescue screen") {
    const std::string name = rescue_beacon_name();
    std::vector<KeyEvent> script = stranded_interactive_events();
    script.push_back(KeyEvent::of_character('j'));
    script.push_back(KeyEvent::of(Key::escape));
    script.push_back(KeyEvent::of_character('q'));

    FakeSession session(std::move(script));
    ConsoleApp app(make_rescue_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(app.final_message() ==
          "Rescued: the " + name + " expedition ran out of provisions and ended early.");
    CHECK(frame_count_with(session.frames, "EXPEDITION JOURNAL") >= 1);
    CHECK(frame_count_with(session.frames, "RESCUE REQUESTED") >= 2);
}

TEST_CASE("the interactive report scrolls on arrow and page keys and redraws each time") {
    // REQ-004 / REQ-005 / REQ-006 / REQ-033: on the final report every scroll key
    // redraws exactly one frame and emits no core event; only an acknowledgement
    // exits. The completing move draws the report at (0, 0).
    const std::string name = adjacent_beacon_name();
    std::vector<KeyEvent> script{
        KeyEvent::of_character('d'), KeyEvent::of_character('a'), KeyEvent::of(Key::up),
        KeyEvent::of(Key::down),     KeyEvent::of(Key::page_up),  KeyEvent::of(Key::page_down),
        KeyEvent::of(Key::left),     KeyEvent::of(Key::right),    KeyEvent::of(Key::enter)};
    FakeSession session(std::move(script));

    ConsoleApp app(make_adjacent_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 9);
    // initial + discovery + completion report + six scroll redraws.
    CHECK(session.draws == 9);
    CHECK(app.final_message() == "Expedition complete: " + name + ".");
}

TEST_CASE("the interactive report acknowledges with q and redraws on resize") {
    // REQ-007 / REQ-008: a resize on the report reclamps and redraws it; q
    // acknowledges and exits 0 with the preserved final message.
    const std::string name = adjacent_beacon_name();
    std::vector<KeyEvent> script{KeyEvent::of_character('d'), KeyEvent::of_character('a'),
                                 KeyEvent::of(Key::resize), KeyEvent::of_character('q')};
    FakeSession session(std::move(script));

    ConsoleApp app(make_adjacent_state(), Settings{});
    const int code = app.run_interactive(session);

    CHECK(code == 0);
    CHECK(session.reads == 4);
    CHECK(session.draws == 4);  // initial + discovery + completion + resize redraw.
    CHECK(app.final_message() == "Expedition complete: " + name + ".");
}

TEST_CASE("the plain final report is deterministic across identical runs") {
    // REQ-024 / TEST-008: the same completed expedition produces byte-identical
    // report output on repeated runs.
    std::string first;
    std::string second;
    CHECK(run_plain_state(make_corridor_state(), "d\nd\nd\na\na\na\n", first) == 0);
    CHECK(run_plain_state(make_corridor_state(), "d\nd\nd\na\na\na\n", second) == 0);
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
    // block (including the recorded travel and discovery entries) without dismissing
    // discovery, acknowledging, or emitting an event.
    const std::string name = corridor_beacon_name();
    std::string output;
    const int code = run_plain_state(make_corridor_state(), "d\nd\nd\nj\nq\n", output);
    CHECK(code == 0);
    CHECK(output.find("EXPEDITION JOURNAL") != std::string::npos);
    CHECK(output.find("Traveled east across open ground for 3 steps.") != std::string::npos);
    CHECK(output.find("Discovered " + name + ".") != std::string::npos);
}

TEST_CASE("plain j on a completed single-cell map prints the initial completion entry") {
    // TASK-016 / REQ-028 / REQ-012: the journal on an already-completed map shows the
    // explicit initial-completion entry and printing it does not re-acknowledge.
    const std::string name = single_cell_beacon_name();
    std::string output;
    const int code = run_plain_state(make_single_cell_state(), "j\n", output);
    CHECK(code == 0);
    CHECK(output.find("EXPEDITION JOURNAL") != std::string::npos);
    CHECK(output.find("Found " + name + " at spawn; the expedition was already complete.") !=
          std::string::npos);
    CHECK(output.find("Goodbye") == std::string::npos);  // EOF acknowledgement stays quiet.
}

}  // TEST_SUITE("console")
