#include <doctest/doctest.h>

#include <sstream>
#include <string>
#include <string_view>
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

int run_plain_with(const std::string& commands, std::string& output, Settings settings = {}) {
    ConsoleApp app(make_state(), settings);
    std::istringstream input(commands);
    std::ostringstream out;
    const int code = app.run_plain(input, out);
    output = out.str();
    return code;
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

}  // TEST_SUITE("console")
