#include <doctest/doctest.h>

#include <string>
#include <vector>

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

    const std::string version = version_text();
    CHECK(version.find("nam_console") != std::string::npos);
}

}  // TEST_SUITE("console")
