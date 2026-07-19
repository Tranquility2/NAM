#pragma once

#include <cstddef>
#include <istream>
#include <string>
#include <string_view>
#include <variant>

#include "map.h"

// A stable, non-localized classification of why a map failed to load. Frontends
// switch on this code to produce their own (optionally localized) wording; the
// code values themselves are part of the core's contract.
enum class MapLoadErrorCode {
    empty_input,             // No content, or only blank lines.
    unsupported_version,     // A "NAM-MAP <n>" header with an unknown version.
    malformed_header,        // A header line could not be understood.
    missing_dimensions,      // Canonical header omitted width or height.
    invalid_dimensions,      // Width/height were not non-negative integers.
    dimensions_out_of_range, // Width/height were zero or above the limit.
    missing_rows,            // Fewer terrain rows than the declared height.
    row_width_mismatch,      // A terrain row was not exactly `width` cells wide.
    unknown_symbol,          // A cell used a character with no terrain mapping.
    spawn_out_of_bounds,     // An explicit spawn fell outside the map.
    spawn_not_walkable,      // An explicit spawn landed on impassable terrain.
    duplicate_spawn,         // More than one spawn line was given.
    no_walkable_spawn,       // No walkable cell exists for the spawn policy.
    file_open_error,         // load_map_file could not open the path.
};

// A parser diagnostic. `line` and `column` are 1-based positions into the
// source; either is 0 when it does not apply. `message` is a developer-facing
// technical description (not user-facing UI text).
struct MapLoadError {
    MapLoadErrorCode code{};
    std::string source;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string message;
};

// Either a fully validated Map or the first error encountered.
using MapLoadResult = std::variant<Map, MapLoadError>;

// The largest width or height a map may declare.
inline constexpr std::size_t max_map_dimension = 1024;

// Parse a map from a stream or string. Parsing is pure and never prints;
// `source` is echoed back in diagnostics (for example a file path). LF and CRLF
// line endings produce identical results.
//
// Two header formats are accepted:
//   * Canonical "NAM-MAP 1": `width`/`height`/optional `spawn` lines, a `---`
//     separator, then exactly `height` terrain rows.
//   * Legacy "<width> <height>" on the first line, followed by the terrain
//     rows. Legacy input has no spawn line, so the deterministic spawn policy
//     (nearest walkable cell to the map centre) is applied.
[[nodiscard]] MapLoadResult load_map(std::istream& input, std::string_view source = "<stream>");
[[nodiscard]] MapLoadResult load_map(std::string_view text, std::string_view source = "<string>");

// Convenience adapter: open a file and parse it. Returns file_open_error if the
// path cannot be read. The core still performs no I/O beyond this explicit call.
[[nodiscard]] MapLoadResult load_map_file(const std::string& path);

// A stable identifier string for an error code (for example "row_width_mismatch").
// Useful for logging and tests; not a localized message.
[[nodiscard]] std::string_view to_string(MapLoadErrorCode code) noexcept;

// The embedded canonical map source, in NAM-MAP 1 format.
[[nodiscard]] const std::string& builtin_map_source();

// Parse the embedded map. It is always valid, so this returns a Map directly;
// it throws std::logic_error only if the embedded data is ever corrupted.
[[nodiscard]] Map builtin_map();
