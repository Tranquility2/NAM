#include "settings.h"

#include <cstdlib>

namespace nam::console {

namespace {

constexpr const char* kProgramName = "nam_console";
constexpr const char* kVersion = "1.0.0";

[[nodiscard]] CliResult usage_error(std::string detail) {
    CliResult result;
    result.action = CliAction::error;
    result.exit_code = 2;
    result.message = std::move(detail);
    return result;
}

}  // namespace

Environment Environment::from_process() {
    Environment environment;
    // NO_COLOR is defined by its mere presence, regardless of value.
    environment.no_color = std::getenv("NO_COLOR") != nullptr;
    if (const char* term = std::getenv("TERM")) {
        environment.term = term;
    }
    return environment;
}

bool Environment::term_supports_ansi() const noexcept {
    return !term.empty() && term != "dumb";
}

CliResult parse_cli(const std::vector<std::string>& args, const Environment& environment) {
    CliResult result;
    Settings& settings = result.settings;

    bool no_color_flag = false;
    std::optional<std::string> positional_map;
    std::optional<std::string> option_map;

    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& arg = args[index];

        if (arg == "--help" || arg == "-h") {
            result.action = CliAction::show_help;
            result.exit_code = 0;
            return result;
        }
        if (arg == "--version" || arg == "-V") {
            result.action = CliAction::show_version;
            result.exit_code = 0;
            return result;
        }
        if (arg == "--debug") {
            settings.debug = true;
        } else if (arg == "--plain") {
            settings.plain = true;
        } else if (arg == "--no-color" || arg == "--no-colour") {
            no_color_flag = true;
        } else if (arg == "--no-animation") {
            settings.animation = false;
        } else if (arg == "--map") {
            if (index + 1 >= args.size()) {
                return usage_error("--map requires a path argument");
            }
            if (option_map) {
                return usage_error("--map was given more than once");
            }
            option_map = args[++index];
        } else if (arg.rfind("--map=", 0) == 0) {
            if (option_map) {
                return usage_error("--map was given more than once");
            }
            option_map = arg.substr(std::string("--map=").size());
        } else if (!arg.empty() && arg.front() == '-' && arg != "-") {
            return usage_error("unknown option '" + arg + "'");
        } else {
            if (positional_map) {
                return usage_error("unexpected extra argument '" + arg + "'");
            }
            positional_map = arg;
        }
    }

    if (positional_map && option_map) {
        return usage_error("provide a map path either positionally or with --map, not both");
    }
    if (option_map) {
        settings.map_path = std::move(option_map);
    } else if (positional_map) {
        settings.map_path = std::move(positional_map);
    }

    // Colour is on unless the CLI or environment turns it off. An explicit
    // --no-color always wins; otherwise NO_COLOR or a dumb terminal disables it.
    const bool environment_disables_color = environment.no_color || !environment.term_supports_ansi();
    settings.use_color = !(no_color_flag || environment_disables_color);

    result.action = CliAction::run;
    result.exit_code = 0;
    return result;
}

CliResult parse_cli(int argc, const char* const argv[], const Environment& environment) {
    std::vector<std::string> args;
    if (argc > 1) {
        args.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index) {
            args.emplace_back(argv[index]);
        }
    }
    return parse_cli(args, environment);
}

std::string usage_text() {
    std::string text;
    text += "Usage: ";
    text += kProgramName;
    text += " [map] [options]\n\n";
    text += "Navigate the actor around a terrain map in your terminal.\n\n";
    text += "Positional:\n";
    text += "  map               Path to a map file (defaults to the built-in map).\n\n";
    text += "Options:\n";
    text += "  --map <path>      Load the map at <path> (alternative to the positional form).\n";
    text += "  --debug           Show internal diagnostics in the HUD.\n";
    text += "  --plain           Force line-oriented mode (no raw terminal or ANSI).\n";
    text += "  --no-color        Disable colour output.\n";
    text += "  --no-animation    Disable transient move emphasis.\n";
    text += "  -h, --help        Show this help and exit.\n";
    text += "  -V, --version     Show version information and exit.\n\n";
    text += "Controls (interactive): arrow keys or W/A/S/D or H/J/K/L to move, q or Esc to quit.\n";
    text += "Environment: NO_COLOR and TERM=dumb disable colour; a non-terminal stdin/stdout\n";
    text += "uses plain mode automatically.\n";
    return text;
}

std::string version_text() {
    std::string text = kProgramName;
    text += ' ';
    text += kVersion;
    text += '\n';
    return text;
}

}  // namespace nam::console
