#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nam::console {

// The maximum number of bytes a `--seed` value may contain. The limit is
// validated before generation and before any display string is constructed, so
// a hostile or accidental oversized argument cannot force large allocations.
inline constexpr std::size_t max_seed_text_bytes = 128;

// A minimal, testable view of the process environment that influences terminal
// behaviour. Captured once so parsing and capability decisions are pure with
// respect to their inputs (no scattered getenv calls).
struct Environment {
    bool no_color = false;       // NO_COLOR present (any value) disables colour.
    std::string term;            // Value of TERM; "dumb"/empty means no ANSI.

    // Read the relevant variables from the real process environment.
    [[nodiscard]] static Environment from_process();

    // A "dumb" or absent TERM cannot interpret cursor/colour control sequences.
    [[nodiscard]] bool term_supports_ansi() const noexcept;
};

// Fully resolved runtime settings. There is exactly one of these, owned by main
// and passed by const reference to the app and renderer; nothing reads globals.
struct Settings {
    std::optional<std::string> map_path;   // std::nullopt selects the built-in map.
    std::optional<std::string> seed_text;  // std::nullopt means no procedural world.
    bool debug = false;                    // --debug: show internal diagnostics.
    bool plain = false;                    // --plain: force line-oriented mode.
    bool use_color = true;                 // Colour permitted (CLI + environment).
    bool animation = true;                 // --no-animation disables move emphasis.
};

// What the CLI asked the program to do.
enum class CliAction {
    run,           // Proceed to load the map and play.
    show_help,     // Print usage and exit 0.
    show_version,  // Print version and exit 0.
    error,         // Usage error; exit 2.
};

// The result of parsing argv. Exactly one action, plus the data that action
// needs. `exit_code` is the process code to use for the non-run actions.
struct CliResult {
    CliAction action = CliAction::run;
    Settings settings;
    std::string message;  // Error text (for error) — developer/user facing.
    int exit_code = 0;
};

// Parse command-line arguments against a captured environment. Pure and free of
// I/O so it can be unit-tested directly.
//
// Grammar:
//   nam_console [map]
//     --map <path>   --seed <text>   --debug   --plain
//     --no-color     --no-animation
//     --help         --version
//
// Unknown options, a missing --map value, or a positional map combined with
// --map are rejected as usage errors (exit code 2). A --seed value selects the
// generated Tiny World (its text is hashed deterministically); it may be empty
// but must be at most 128 bytes, may not be repeated, and may not be combined
// with any map input. NO_COLOR and TERM=dumb turn colour off; an explicit CLI
// flag always wins over the environment.
[[nodiscard]] CliResult parse_cli(const std::vector<std::string>& args, const Environment& environment);

// Convenience overload for a raw argv (argv[0] is the program name and skipped).
[[nodiscard]] CliResult parse_cli(int argc, const char* const argv[], const Environment& environment);

// Escape arbitrary seed bytes for safe terminal display. The result is wrapped in
// double quotes; a backslash, double quote, newline, carriage return, and tab
// become `\\`, `\"`, `\n`, `\r`, and `\t`; other printable ASCII bytes
// (0x20..0x7E) are preserved verbatim; every remaining byte is rendered as an
// uppercase `\xHH` escape. No raw control byte (including ESC) ever appears in
// the output, so a seed can never inject terminal control sequences.
[[nodiscard]] std::string format_seed_for_display(std::string_view seed);

// The usage text shown by --help and referenced by error messages.
[[nodiscard]] std::string usage_text();

// The program version banner shown by --version.
[[nodiscard]] std::string version_text();

}  // namespace nam::console
