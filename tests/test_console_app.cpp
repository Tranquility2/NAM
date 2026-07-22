#include <doctest/doctest.h>

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "console_app.h"
#include "game_state.h"
#include "input.h"
#include "map.h"
#include "map_parser.h"
#include "settings.h"

using namespace nam::console;

namespace {

Map make_map(std::string_view text) {
    MapLoadResult result = load_map(text);
    REQUIRE(std::holds_alternative<Map>(result));
    return std::get<Map>(std::move(result));
}

// A small room the actor can move around in horizontally.
GameState make_state() {
    return GameState(make_map("NAM-MAP 1\nwidth 5\nheight 3\nspawn 2 1\n---\n=====\n|...|\n=====\n"));
}

// A room wider than the initial 5x5 reveal, with a distinctive mountain '@' far
// to the right (walkable, so the actor can approach it) that starts hidden.
GameState make_big_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 9\nheight 3\nspawn 1 1\n---\n=========\n......@..\n=========\n"));
}

// A one-lane mountain corridor for stamina integration. From the left spawn the
// four-cost mountains drain 12 -> 8 -> 4 -> 0 over three steps; a fourth step is
// unaffordable. A distinctive water cell sits at x=8, outside radius 4 from the
// last successfully occupied mountain at x=3, so it must never appear unless fog
// wrongly refreshes on the block.
GameState make_cost_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 9\nheight 3\nspawn 0 1\n---\n=========\n.@@@@...~\n=========\n"));
}

// A corridor with an open spawn at x=1, a hill at x=2, and a distinctive water
// cell at x=5. The water sits at radius 3 from the hill but beyond radius 2 from
// either the open spawn or the hill cell if it were flat, so only standing on
// the hill reveals it. Stepping back off the hill keeps it as memory.
GameState make_hill_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 9\nheight 3\nspawn 1 1\n---\n=========\n..^..~...\n=========\n"));
}

// A mountain corridor whose distinctive water at x=8 is reachable only from the
// fourth mountain at x=4 (radius 4 -> x=8). Three steps drain 12 -> 8 -> 4 -> 0,
// so the fourth mountain is unaffordable until a rest restores exactly enough
// stamina to enter it and finally reveal the water.
GameState make_mountain_reach_state() {
    return GameState(make_map(
        "NAM-MAP 1\nwidth 11\nheight 3\nspawn 0 1\n---\n===========\n.@@@@...~..\n===========\n"));
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
    CHECK(direction_for(KeyEvent::of_character('j')) == Direction::down);
    CHECK(direction_for(KeyEvent::of_character('a')) == Direction::left);
    CHECK(direction_for(KeyEvent::of_character('h')) == Direction::left);
    CHECK(direction_for(KeyEvent::of_character('d')) == Direction::right);
    CHECK(direction_for(KeyEvent::of_character('l')) == Direction::right);
    CHECK(direction_for(KeyEvent::of_character('W')) == Direction::up);  // case-insensitive.
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
    // TASK-016 / TEST-017: the water at x=8 lies at radius 4 from the fourth
    // mountain. Draining to zero blocks that mountain, so the water stays hidden;
    // only resting enough to afford and enter the mountain finally reveals it.
    std::string blocked;
    const int code_blocked = run_plain_state(make_mountain_reach_state(), "d\nd\nd\nd\nq\n", blocked);
    CHECK(code_blocked == 0);
    CHECK(blocked.find("Not enough stamina for mountain: need 4, have 0.") != std::string::npos);
    CHECK(blocked.find('~') == std::string::npos);  // the blocked mountain reveals nothing.

    std::string reached;
    const int code_reached =
        run_plain_state(make_mountain_reach_state(), "d\nd\nd\nr\nd\nq\n", reached);
    CHECK(code_reached == 0);
    CHECK(reached.find("Rested and recovered 4 stamina.") != std::string::npos);
    CHECK(reached.find("Moved onto mountain for 4 stamina.") != std::string::npos);
    CHECK(reached.find('~') != std::string::npos);  // entering the mountain reveals it.
    CHECK(reached.find('\x1b') == std::string::npos);  // plain stays ANSI-free.
}

TEST_CASE("a scripted high-cost route shows costs, drains stamina, and blocks deterministically") {
    // Three affordable mountain steps drain stamina to zero; the fourth is an
    // unaffordable, typed block that does not move the actor.
    std::string output;
    const int code = run_plain_state(make_cost_state(), "d\nd\nd\nd\nq\n", output);
    CHECK(code == 0);

    // Exact successful-cost wording and the deterministic HUD stamina countdown.
    CHECK(output.find("Moved onto mountain for 4 stamina.") != std::string::npos);
    CHECK(output.find("Stamina: 8/12") != std::string::npos);
    CHECK(output.find("Stamina: 4/12") != std::string::npos);
    CHECK(output.find("Stamina: 0/12") != std::string::npos);

    // The unaffordable fourth step is blocked with the exact insufficient wording.
    CHECK(output.find("Not enough stamina for mountain: need 4, have 0.") != std::string::npos);

    // Fog stays tied to the actor's real position: the block never advances it,
    // so the distant water cell is never revealed and no '~' leaks into output.
    CHECK(output.find('~') == std::string::npos);
    CHECK(output.find('\x1b') == std::string::npos);  // plain stays ANSI-free.

    // The whole scripted session is byte-identical across repeated runs.
    std::string second;
    const int code_again = run_plain_state(make_cost_state(), "d\nd\nd\nd\nq\n", second);
    CHECK(code_again == code);
    CHECK(second == output);
}

TEST_CASE("plain mode recognises both r and rest as the rest command") {
    std::string spelled;
    const int a = run_plain_state(make_rest_state(), "rest\nq\n", spelled);
    CHECK(a == 0);
    CHECK(spelled.find("Stamina is already full.") != std::string::npos);
    CHECK(spelled.find("Unknown command") == std::string::npos);

    std::string letter;
    const int b = run_plain_state(make_rest_state(), "r\nq\n", letter);
    CHECK(b == 0);
    CHECK(letter.find("Stamina is already full.") != std::string::npos);
    CHECK(letter.find("Unknown command") == std::string::npos);
}

TEST_CASE("resting from zero lets the actor complete the next water move") {
    // Drain to zero on the mountains, then a water step is blocked; rest recovers
    // four, which is exactly enough to enter the water and leave one stamina.
    std::string blocked_first;
    const int code_blocked =
        run_plain_state(make_rest_state(), "d\nd\nd\nd\nq\n", blocked_first);
    CHECK(code_blocked == 0);
    CHECK(blocked_first.find("Stamina: 0/12") != std::string::npos);
    CHECK(blocked_first.find("Not enough stamina for water: need 3, have 0.") !=
          std::string::npos);

    std::string recovered;
    const int code = run_plain_state(make_rest_state(), "d\nd\nd\nr\nd\nq\n", recovered);
    CHECK(code == 0);
    CHECK(recovered.find("Rested and recovered 4 stamina.") != std::string::npos);
    CHECK(recovered.find("Moved onto water for 3 stamina.") != std::string::npos);
    CHECK(recovered.find("Stamina: 1/12") != std::string::npos);
    CHECK(recovered.find('\x1b') == std::string::npos);  // plain stays ANSI-free.
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

}  // TEST_SUITE("console")
