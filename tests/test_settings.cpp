#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "nam/version.h"
#include "settings.h"

using namespace nam::console;

namespace {

Environment good_env() {
    Environment env;
    env.no_color = false;
    env.term = "xterm-256color";
    return env;
}

CliResult parse(const std::vector<std::string>& args, const Environment& env) {
    return parse_cli(args, env);
}

}  // namespace

TEST_SUITE("console") {

TEST_CASE("no arguments selects the run action with the built-in map") {
    const CliResult result = parse({}, good_env());
    CHECK(result.action == CliAction::run);
    CHECK(result.exit_code == 0);
    CHECK_FALSE(result.settings.map_path.has_value());
    CHECK_FALSE(result.settings.seed_text.has_value());
    CHECK(result.settings.use_color);
    CHECK_FALSE(result.settings.debug);
    CHECK_FALSE(result.settings.plain);
    CHECK(result.settings.animation);
}

TEST_CASE("--help and -h request help with exit code 0") {
    CHECK(parse({"--help"}, good_env()).action == CliAction::show_help);
    CHECK(parse({"--help"}, good_env()).exit_code == 0);
    CHECK(parse({"-h"}, good_env()).action == CliAction::show_help);
}

TEST_CASE("--version and -V request the version with exit code 0") {
    CHECK(parse({"--version"}, good_env()).action == CliAction::show_version);
    CHECK(parse({"--version"}, good_env()).exit_code == 0);
    CHECK(parse({"-V"}, good_env()).action == CliAction::show_version);
}

TEST_CASE("boolean flags set the matching settings") {
    const CliResult result = parse({"--debug", "--plain", "--no-animation"}, good_env());
    REQUIRE(result.action == CliAction::run);
    CHECK(result.settings.debug);
    CHECK(result.settings.plain);
    CHECK_FALSE(result.settings.animation);
}

TEST_CASE("a map path is accepted positionally and via --map") {
    CHECK(parse({"level.map"}, good_env()).settings.map_path == "level.map");
    CHECK(parse({"--map", "level.map"}, good_env()).settings.map_path == "level.map");
    CHECK(parse({"--map=level.map"}, good_env()).settings.map_path == "level.map");
}

TEST_CASE("conflicting and malformed options are usage errors (exit 2)") {
    SUBCASE("positional and --map together") {
        const CliResult result = parse({"a.map", "--map", "b.map"}, good_env());
        CHECK(result.action == CliAction::error);
        CHECK(result.exit_code == 2);
    }
    SUBCASE("--map given twice") {
        CHECK(parse({"--map", "a.map", "--map", "b.map"}, good_env()).exit_code == 2);
    }
    SUBCASE("--map without a value") {
        CHECK(parse({"--map"}, good_env()).exit_code == 2);
    }
    SUBCASE("an unknown option") {
        const CliResult result = parse({"--frobnicate"}, good_env());
        CHECK(result.action == CliAction::error);
        CHECK(result.exit_code == 2);
    }
    SUBCASE("a second positional argument") {
        CHECK(parse({"a.map", "b.map"}, good_env()).exit_code == 2);
    }
}

TEST_CASE("colour follows the environment unless overridden") {
    SUBCASE("a capable terminal enables colour") {
        CHECK(parse({}, good_env()).settings.use_color);
    }
    SUBCASE("NO_COLOR disables colour") {
        Environment env = good_env();
        env.no_color = true;
        CHECK_FALSE(parse({}, env).settings.use_color);
    }
    SUBCASE("TERM=dumb disables colour") {
        Environment env = good_env();
        env.term = "dumb";
        CHECK_FALSE(parse({}, env).settings.use_color);
    }
    SUBCASE("an empty TERM disables colour") {
        Environment env = good_env();
        env.term.clear();
        CHECK_FALSE(parse({}, env).settings.use_color);
    }
    SUBCASE("--no-color wins even on a capable terminal") {
        CHECK_FALSE(parse({"--no-color"}, good_env()).settings.use_color);
    }
}

TEST_CASE("Environment::term_supports_ansi reflects TERM capability") {
    Environment env;
    env.term = "xterm";
    CHECK(env.term_supports_ansi());
    env.term = "dumb";
    CHECK_FALSE(env.term_supports_ansi());
    env.term.clear();
    CHECK_FALSE(env.term_supports_ansi());
}

TEST_CASE("the raw-argv overload skips argv[0]") {
    const char* argv[] = {"nam_console", "--debug", "level.map"};
    const CliResult result = parse_cli(3, argv, good_env());
    CHECK(result.action == CliAction::run);
    CHECK(result.settings.debug);
    CHECK(result.settings.map_path == "level.map");
}

TEST_CASE("usage and version text are self-describing") {
    const std::string usage = usage_text();
    CHECK(usage.find("Usage:") != std::string::npos);
    CHECK(usage.find("--map") != std::string::npos);
    CHECK(usage.find("--plain") != std::string::npos);
    CHECK(usage.find("--help") != std::string::npos);
    CHECK(usage.find("--seed") != std::string::npos);
    CHECK(usage.find("Tiny World") != std::string::npos);
    CHECK(usage.find("128") != std::string::npos);

    const std::string version = version_text();
    CHECK(version.find("nam_console") != std::string::npos);
}

TEST_CASE("the generated version constants describe 0.2.0") {
    // The extra parentheses stop doctest from decomposing the operands, so it
    // never stringifies nam::version (a std::string_view) through std::ostream.
    // MSVC would otherwise instantiate its string_view inserter against an
    // incomplete std::basic_ostream (C2027); the comparison itself is unchanged.
    CHECK((nam::version == "0.2.0"));
    CHECK(nam::version_major == 0);
    CHECK(nam::version_minor == 2);
    CHECK(nam::version_patch == 0);
}

TEST_CASE("version_text is exactly the program banner for the generated version") {
    CHECK(version_text() == "nam_console 0.2.0\n");
}

TEST_CASE("both --seed syntaxes preserve the exact text bytes") {
    // TEST-011.
    CHECK(parse({"--seed", "glass-river"}, good_env()).settings.seed_text == "glass-river");
    CHECK(parse({"--seed=glass-river"}, good_env()).settings.seed_text == "glass-river");

    const CliResult spaced = parse({"--seed", "glass river"}, good_env());
    CHECK(spaced.action == CliAction::run);
    CHECK(spaced.settings.seed_text == "glass river");
    CHECK_FALSE(spaced.settings.map_path.has_value());

    // A value that itself begins with a dash is taken verbatim in the = form.
    CHECK(parse({"--seed=-x"}, good_env()).settings.seed_text == "-x");
}

TEST_CASE("an empty seed is accepted and selects a generated world") {
    // TEST-012 (parsing half): the seed is present but empty (not std::nullopt).
    const CliResult result = parse({"--seed="}, good_env());
    CHECK(result.action == CliAction::run);
    REQUIRE(result.settings.seed_text.has_value());
    CHECK(result.settings.seed_text->empty());
    CHECK_FALSE(result.settings.map_path.has_value());
}

TEST_CASE("seed usage errors all exit with code 2") {
    // TEST-013.
    SUBCASE("a separate --seed with no following value") {
        const CliResult result = parse({"--seed"}, good_env());
        CHECK(result.action == CliAction::error);
        CHECK(result.exit_code == 2);
    }
    SUBCASE("--seed given twice") {
        CHECK(parse({"--seed", "a", "--seed", "b"}, good_env()).exit_code == 2);
        CHECK(parse({"--seed=a", "--seed=b"}, good_env()).exit_code == 2);
    }
    SUBCASE("a seed combined with a positional map") {
        CHECK(parse({"level.map", "--seed=a"}, good_env()).exit_code == 2);
    }
    SUBCASE("a seed combined with --map") {
        CHECK(parse({"--seed=a", "--map", "level.map"}, good_env()).exit_code == 2);
        CHECK(parse({"--map=level.map", "--seed", "a"}, good_env()).exit_code == 2);
    }
}

TEST_CASE("a seed of exactly 128 bytes is accepted and 129 is rejected") {
    // TEST-014.
    const std::string max_len(max_seed_text_bytes, 'a');
    const std::string over_len(max_seed_text_bytes + 1, 'a');

    SUBCASE("128 bytes via the separate form") {
        const CliResult result = parse({"--seed", max_len}, good_env());
        CHECK(result.action == CliAction::run);
        CHECK(result.settings.seed_text == max_len);
    }
    SUBCASE("128 bytes via the = form") {
        CHECK(parse({"--seed=" + max_len}, good_env()).action == CliAction::run);
    }
    SUBCASE("129 bytes via the separate form") {
        const CliResult result = parse({"--seed", over_len}, good_env());
        CHECK(result.action == CliAction::error);
        CHECK(result.exit_code == 2);
    }
    SUBCASE("129 bytes via the = form") {
        CHECK(parse({"--seed=" + over_len}, good_env()).exit_code == 2);
    }
}

TEST_CASE("format_seed_for_display escapes every required class of byte") {
    // TEST-015. Printable text is quoted and otherwise untouched.
    CHECK(format_seed_for_display("glass-river") == "\"glass-river\"");
    CHECK(format_seed_for_display("") == "\"\"");

    // Backslash and double quote get C-style escapes.
    CHECK(format_seed_for_display("a\\b") == "\"a\\\\b\"");
    CHECK(format_seed_for_display("a\"b") == "\"a\\\"b\"");

    // Whitespace controls use their named escapes.
    CHECK(format_seed_for_display("a\nb") == "\"a\\nb\"");
    CHECK(format_seed_for_display("a\rb") == "\"a\\rb\"");
    CHECK(format_seed_for_display("a\tb") == "\"a\\tb\"");

    // ESC, DEL, and a high-bit byte become uppercase \xHH escapes.
    CHECK(format_seed_for_display(std::string_view("\x1b", 1)) == "\"\\x1B\"");
    CHECK(format_seed_for_display(std::string_view("\x7f", 1)) == "\"\\x7F\"");
    CHECK(format_seed_for_display(std::string_view("\xff", 1)) == "\"\\xFF\"");

    // A NUL byte is escaped, not truncated.
    CHECK(format_seed_for_display(std::string_view("\0", 1)) == "\"\\x00\"");
}

TEST_CASE("format_seed_for_display never emits a raw control or high byte") {
    // Feed every byte value 0..255 and assert the escaped output stays inside the
    // printable ASCII range, so a seed can never inject a terminal control byte.
    std::string all_bytes;
    all_bytes.reserve(256);
    for (int value = 0; value < 256; ++value) {
        all_bytes.push_back(static_cast<char>(value));
    }
    const std::string escaped = format_seed_for_display(all_bytes);
    for (const char character : escaped) {
        const unsigned char byte = static_cast<unsigned char>(character);
        CHECK(byte >= 0x20u);
        CHECK(byte <= 0x7Eu);
    }
    // In particular there is no raw ESC byte anywhere in the output.
    CHECK(escaped.find('\x1b') == std::string::npos);
}

}  // TEST_SUITE("console")
