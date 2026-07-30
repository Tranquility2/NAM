#include "settings.h"

#include <charconv>
#include <cstdlib>

#include "nam/version.h"

namespace nam::console {

namespace {

constexpr const char* kProgramName = "nam_console";

[[nodiscard]] std::optional<std::string> environment_value(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        std::free(value);
        return std::nullopt;
    }

    std::string result(value);
    std::free(value);
    return result;
#else
    if (const char* value = std::getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
#endif
}

[[nodiscard]] CliResult usage_error(std::string detail) {
    CliResult result;
    result.action = CliAction::error;
    result.exit_code = 2;
    result.message = std::move(detail);
    return result;
}

// Parse a --seed-number token strictly as a decimal uint64: digits only, no sign,
// no surrounding whitespace, no `0x`, and no trailing bytes. The entire token must
// be consumed and the value must fit in uint64. Returns std::nullopt on any
// violation so the caller can raise a usage error with a stable message.
[[nodiscard]] std::optional<std::uint64_t> parse_numeric_seed(std::string_view token) {
    if (token.empty()) {
        return std::nullopt;
    }
    // std::from_chars accepts neither a leading sign nor whitespace and never
    // interprets a base prefix, so a stray '+', '-', ' ', or "0x" already fails.
    // Reject any non-digit explicitly first so "12 " or "1_2" cannot slip through
    // a partial parse.
    for (const char byte : token) {
        if (byte < '0' || byte > '9') {
            return std::nullopt;
        }
    }
    std::uint64_t value = 0;
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc() || parsed.ptr != end) {
        return std::nullopt;  // Overflow or an unconsumed suffix.
    }
    return value;
}

}  // namespace

Environment Environment::from_process() {
    Environment environment;
    // NO_COLOR is defined by its mere presence, regardless of value.
    environment.no_color = environment_value("NO_COLOR").has_value();
    if (const std::optional<std::string> term = environment_value("TERM")) {
        environment.term = *term;
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
    std::optional<std::string> seed_value;
    std::optional<std::uint64_t> numeric_seed_value;
    bool numeric_seed_given = false;

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
        } else if (arg == "--seed") {
            if (index + 1 >= args.size()) {
                return usage_error("--seed requires a text argument");
            }
            if (seed_value) {
                return usage_error("--seed was given more than once");
            }
            std::string value = args[++index];
            if (value.size() > max_seed_text_bytes) {
                return usage_error("--seed text must be at most " +
                                   std::to_string(max_seed_text_bytes) + " bytes");
            }
            seed_value = std::move(value);
        } else if (arg.rfind("--seed=", 0) == 0) {
            if (seed_value) {
                return usage_error("--seed was given more than once");
            }
            std::string value = arg.substr(std::string("--seed=").size());
            if (value.size() > max_seed_text_bytes) {
                return usage_error("--seed text must be at most " +
                                   std::to_string(max_seed_text_bytes) + " bytes");
            }
            seed_value = std::move(value);
        } else if (arg == "--seed-number") {
            if (index + 1 >= args.size()) {
                return usage_error("--seed-number requires a numeric argument");
            }
            if (numeric_seed_given) {
                return usage_error("--seed-number was given more than once");
            }
            const std::optional<std::uint64_t> parsed = parse_numeric_seed(args[++index]);
            if (!parsed) {
                return usage_error(
                    "--seed-number must be a decimal integer in [0, 18446744073709551615]");
            }
            numeric_seed_value = parsed;
            numeric_seed_given = true;
        } else if (arg.rfind("--seed-number=", 0) == 0) {
            if (numeric_seed_given) {
                return usage_error("--seed-number was given more than once");
            }
            const std::optional<std::uint64_t> parsed =
                parse_numeric_seed(arg.substr(std::string("--seed-number=").size()));
            if (!parsed) {
                return usage_error(
                    "--seed-number must be a decimal integer in [0, 18446744073709551615]");
            }
            numeric_seed_value = parsed;
            numeric_seed_given = true;
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
    // A seed selects a generated world, so it cannot be combined with any map
    // input: the source of terrain would otherwise be ambiguous.
    if (seed_value && (positional_map || option_map)) {
        return usage_error("provide either a map or --seed, not both");
    }
    // A numeric seed is a second procedural-world source. It is mutually exclusive
    // with a text seed and with any map input so the world identity is unambiguous.
    if (numeric_seed_given && seed_value) {
        return usage_error("provide either --seed or --seed-number, not both");
    }
    if (numeric_seed_given && (positional_map || option_map)) {
        return usage_error("provide either a map or --seed-number, not both");
    }
    if (option_map) {
        settings.map_path = std::move(option_map);
    } else if (positional_map) {
        settings.map_path = std::move(positional_map);
    }
    if (seed_value) {
        settings.seed_text = std::move(seed_value);
    }
    if (numeric_seed_given) {
        settings.numeric_seed = numeric_seed_value;
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

std::string format_seed_for_display(std::string_view seed) {
    // Retain the raw seed bytes only for hashing; every display path routes here
    // so terminal control bytes (including ESC) can never reach the terminal.
    // Formatting is done by hand with unsigned-byte inspection and manual
    // uppercase hex so it is independent of locale and stream state.
    constexpr char hex_digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(seed.size() + 2);
    out.push_back('"');
    for (const char raw : seed) {
        const unsigned char byte = static_cast<unsigned char>(raw);
        switch (byte) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte >= 0x20u && byte <= 0x7Eu) {
                    out.push_back(static_cast<char>(byte));
                } else {
                    out += "\\x";
                    out.push_back(hex_digits[(byte >> 4u) & 0x0Fu]);
                    out.push_back(hex_digits[byte & 0x0Fu]);
                }
                break;
        }
    }
    out.push_back('"');
    return out;
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
    text += "  --seed <text>     Generate the deterministic expedition from <text> (hashed;\n";
    text += "                    max 128 bytes; cannot be combined with a map).\n";
    text += "  --seed-number <n> Generate the deterministic expedition from a decimal uint64\n";
    text += "                    seed (0..18446744073709551615; cannot be combined with a map\n";
    text += "                    or --seed).\n";
    text += "  --debug           Show internal diagnostics in the HUD.\n";
    text += "  --plain           Force line-oriented mode (no raw terminal or ANSI).\n";
    text += "  --no-color        Disable colour output.\n";
    text += "  --no-animation    Disable transient move emphasis.\n";
    text += "  -h, --help        Show this help and exit.\n";
    text += "  -V, --version     Show version information and exit.\n\n";
    text += "Controls (interactive): arrow keys or W/A/S/D (H/K/L also move) to move,\n";
    text += "j for the expedition journal, q or Esc to quit.\n";
    text += "Map overlay: * current objective, ? discovery (optional find), ! hazard\n";
    text += "(costs extra stamina to cross but never blocks), + safe landmark (restores\n";
    text += "stamina on entry).\n";
    text += "Environment: NO_COLOR and TERM=dumb disable colour; a non-terminal stdin/stdout\n";
    text += "uses plain mode automatically.\n";
    return text;
}

std::string version_text() {
    std::string text = kProgramName;
    text += ' ';
    text += nam::version;
    text += '\n';
    return text;
}

}  // namespace nam::console
