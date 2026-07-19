#include "map_parser.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] constexpr bool is_space(char character) noexcept {
    return character == ' ' || character == '\t';
}

// Split a line into whitespace-delimited tokens. The returned views alias the
// input, which must outlive them (the parser keeps every line in a vector).
[[nodiscard]] std::vector<std::string_view> split_whitespace(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t position = 0;
    while (position < line.size()) {
        while (position < line.size() && is_space(line[position])) {
            ++position;
        }
        const std::size_t start = position;
        while (position < line.size() && !is_space(line[position])) {
            ++position;
        }
        if (position > start) {
            tokens.push_back(line.substr(start, position - start));
        }
    }
    return tokens;
}

// Parse a whole token as a non-negative integer. Rejects signs, overflow, and
// trailing characters so malformed dimensions are diagnosed rather than
// silently truncated.
[[nodiscard]] std::optional<std::size_t> parse_size(std::string_view text) {
    std::size_t value = 0;
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] MapLoadError make_error(MapLoadErrorCode code, std::string_view source,
                                      std::size_t line, std::size_t column, std::string message) {
    return MapLoadError{code, std::string(source), line, column, std::move(message)};
}

// A spawn requested explicitly in a canonical header, kept as the raw
// non-negative values the source declared (before any narrowing to int) so that
// bounds validation cannot be defeated by a value that wraps when truncated.
// `line` is the 1-based source line for diagnostics.
struct SpawnRequest {
    std::size_t x = 0;
    std::size_t y = 0;
    std::size_t line = 0;
};

// Deterministic spawn policy: the walkable cell nearest the map centre, with
// ties broken by scanning rows before columns. Used when no explicit spawn is
// given (all legacy maps, and canonical maps that omit the spawn line).
//
// A single O(width*height) pass selects the walkable cell with the minimum key
// (Chebyshev distance to centre, y, x). Visiting cells in ascending (y, x)
// order means an earlier candidate already wins any (y, x) tie, so we replace
// the incumbent only on a strictly smaller Chebyshev distance. This rejects a
// fully-walled map in linear time instead of rescanning growing rings.
[[nodiscard]] std::optional<Coordinates> find_default_spawn(const std::vector<Terrain>& cells,
                                                            std::size_t width, std::size_t height) {
    const int centre_x = static_cast<int>(width / 2);
    const int centre_y = static_cast<int>(height / 2);
    std::optional<Coordinates> best;
    int best_distance = 0;
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            if (!is_walkable(cells[y * width + x])) {
                continue;
            }
            const int dx = static_cast<int>(x) - centre_x;
            const int dy = static_cast<int>(y) - centre_y;
            const int distance = std::max(std::abs(dx), std::abs(dy));
            if (!best || distance < best_distance) {
                best = Coordinates{static_cast<int>(x), static_cast<int>(y)};
                best_distance = distance;
            }
        }
    }
    return best;
}

// Validate dimensions, read exactly `height` terrain rows starting at
// `data_start`, resolve the spawn, and construct the Map. Returns the first
// error encountered.
[[nodiscard]] MapLoadResult build_map(const std::vector<std::string>& lines, std::size_t data_start,
                                      std::size_t width, std::size_t height,
                                      std::optional<SpawnRequest> spawn, std::string_view source) {
    if (width == 0 || height == 0 || width > max_map_dimension || height > max_map_dimension) {
        return make_error(MapLoadErrorCode::dimensions_out_of_range, source, 0, 0,
                          "dimensions " + std::to_string(width) + "x" + std::to_string(height) +
                              " are outside 1.." + std::to_string(max_map_dimension));
    }

    if (lines.size() < data_start + height) {
        const std::size_t available = lines.size() > data_start ? lines.size() - data_start : 0;
        return make_error(MapLoadErrorCode::missing_rows, source, lines.size() + 1, 0,
                          "expected " + std::to_string(height) + " terrain rows, found " +
                              std::to_string(available));
    }

    std::vector<Terrain> cells;
    cells.reserve(width * height);
    for (std::size_t row = 0; row < height; ++row) {
        const std::string& text = lines[data_start + row];
        const std::size_t line_no = data_start + row + 1;
        if (text.size() != width) {
            const std::size_t column = (text.size() < width ? text.size() : width) + 1;
            return make_error(MapLoadErrorCode::row_width_mismatch, source, line_no, column,
                              "row width " + std::to_string(text.size()) +
                                  " does not match declared width " + std::to_string(width));
        }
        for (std::size_t column = 0; column < width; ++column) {
            const std::optional<Terrain> terrain = terrain_from_symbol(text[column]);
            if (!terrain) {
                return make_error(MapLoadErrorCode::unknown_symbol, source, line_no, column + 1,
                                  std::string("unknown terrain symbol '") + text[column] + "'");
            }
            cells.push_back(*terrain);
        }
    }

    Coordinates chosen_spawn{};
    if (spawn) {
        const SpawnRequest requested = *spawn;
        // Validate the raw (non-negative) values against the map before any
        // narrowing to int, so a value that would wrap on truncation (for
        // example 2^32 + 2 collapsing to 2) is still rejected. width/height are
        // already bounded by max_map_dimension, so a value inside them is also
        // representable as int; guard the int range explicitly for clarity.
        constexpr auto int_max = static_cast<std::size_t>(std::numeric_limits<int>::max());
        if (requested.x >= width || requested.y >= height ||
            requested.x > int_max || requested.y > int_max) {
            return make_error(MapLoadErrorCode::spawn_out_of_bounds, source, requested.line, 1,
                              "spawn (" + std::to_string(requested.x) + "," +
                                  std::to_string(requested.y) + ") is outside the map " +
                                  std::to_string(width) + "x" + std::to_string(height));
        }
        const int spawn_x = static_cast<int>(requested.x);
        const int spawn_y = static_cast<int>(requested.y);
        const std::size_t flat = requested.y * width + requested.x;
        if (!is_walkable(cells[flat])) {
            return make_error(MapLoadErrorCode::spawn_not_walkable, source, requested.line, 1,
                              "spawn (" + std::to_string(spawn_x) + "," +
                                  std::to_string(spawn_y) + ") is on impassable terrain");
        }
        chosen_spawn = Coordinates{spawn_x, spawn_y};
    } else {
        const std::optional<Coordinates> policy_spawn = find_default_spawn(cells, width, height);
        if (!policy_spawn) {
            return make_error(MapLoadErrorCode::no_walkable_spawn, source, 0, 0,
                              "no walkable cell is available for a spawn");
        }
        chosen_spawn = *policy_spawn;
    }

    return Map(width, height, std::move(cells), chosen_spawn);
}

// Parse the canonical "NAM-MAP 1" header (already validated on line 1) starting
// from line index 1, then delegate to build_map.
[[nodiscard]] MapLoadResult load_canonical(const std::vector<std::string>& lines,
                                           std::string_view source) {
    std::size_t width = 0;
    std::size_t height = 0;
    bool have_width = false;
    bool have_height = false;
    std::optional<SpawnRequest> spawn;

    std::size_t index = 1;
    bool saw_separator = false;
    for (; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        const std::size_t line_no = index + 1;
        if (line == "---") {
            saw_separator = true;
            ++index;
            break;
        }

        const std::vector<std::string_view> tokens = split_whitespace(line);
        if (tokens.empty()) {
            return make_error(MapLoadErrorCode::malformed_header, source, line_no, 1,
                              "expected a header key or '---' separator");
        }

        if (tokens[0] == "width") {
            std::optional<std::size_t> value;
            if (tokens.size() == 2) {
                value = parse_size(tokens[1]);
            }
            if (!value) {
                return make_error(MapLoadErrorCode::invalid_dimensions, source, line_no, 1,
                                  "width must be written as 'width <n>'");
            }
            width = *value;
            have_width = true;
        } else if (tokens[0] == "height") {
            std::optional<std::size_t> value;
            if (tokens.size() == 2) {
                value = parse_size(tokens[1]);
            }
            if (!value) {
                return make_error(MapLoadErrorCode::invalid_dimensions, source, line_no, 1,
                                  "height must be written as 'height <n>'");
            }
            height = *value;
            have_height = true;
        } else if (tokens[0] == "spawn") {
            if (spawn) {
                return make_error(MapLoadErrorCode::duplicate_spawn, source, line_no, 1,
                                  "spawn specified more than once");
            }
            std::optional<std::size_t> spawn_x;
            std::optional<std::size_t> spawn_y;
            if (tokens.size() == 3) {
                spawn_x = parse_size(tokens[1]);
                spawn_y = parse_size(tokens[2]);
            }
            if (!spawn_x || !spawn_y) {
                return make_error(MapLoadErrorCode::malformed_header, source, line_no, 1,
                                  "spawn must be written as 'spawn <x> <y>'");
            }
            spawn = SpawnRequest{*spawn_x, *spawn_y, line_no};
        } else {
            return make_error(MapLoadErrorCode::malformed_header, source, line_no, 1,
                              "unknown header key '" + std::string(tokens[0]) + "'");
        }
    }

    if (!saw_separator) {
        return make_error(MapLoadErrorCode::malformed_header, source, lines.size() + 1, 0,
                          "missing '---' separator before terrain rows");
    }
    if (!have_width || !have_height) {
        return make_error(MapLoadErrorCode::missing_dimensions, source, 0, 0,
                          "canonical header must set both width and height");
    }

    return build_map(lines, index, width, height, spawn, source);
}

}  // namespace

MapLoadResult load_map(std::istream& input, std::string_view source) {
    std::vector<std::string> lines;
    std::string raw;
    while (std::getline(input, raw)) {
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();  // Treat CRLF and LF identically.
        }
        lines.push_back(std::move(raw));
    }

    const bool any_content = std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
        return std::any_of(line.begin(), line.end(), [](char c) { return !is_space(c); });
    });
    if (!any_content) {
        return make_error(MapLoadErrorCode::empty_input, source, 0, 0, "input contained no map data");
    }

    const std::vector<std::string_view> header = split_whitespace(lines[0]);

    if (!header.empty() && header[0] == "NAM-MAP") {
        if (header.size() != 2 || header[1] != "1") {
            return make_error(MapLoadErrorCode::unsupported_version, source, 1, 1,
                              "unsupported map version header '" + lines[0] + "'");
        }
        return load_canonical(lines, source);
    }

    if (header.size() == 2) {
        const std::optional<std::size_t> width = parse_size(header[0]);
        const std::optional<std::size_t> height = parse_size(header[1]);
        if (width && height) {
            return build_map(lines, 1, *width, *height, std::nullopt, source);
        }
        return make_error(MapLoadErrorCode::invalid_dimensions, source, 1, 1,
                          "legacy header must be '<width> <height>'");
    }

    return make_error(MapLoadErrorCode::malformed_header, source, 1, 1,
                      "first line is neither 'NAM-MAP 1' nor a legacy '<width> <height>' header");
}

MapLoadResult load_map(std::string_view text, std::string_view source) {
    std::istringstream stream{std::string(text)};
    return load_map(stream, source);
}

MapLoadResult load_map_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return make_error(MapLoadErrorCode::file_open_error, path, 0, 0, "could not open file");
    }
    return load_map(file, path);
}

std::string_view to_string(MapLoadErrorCode code) noexcept {
    switch (code) {
        case MapLoadErrorCode::empty_input:             return "empty_input";
        case MapLoadErrorCode::unsupported_version:     return "unsupported_version";
        case MapLoadErrorCode::malformed_header:        return "malformed_header";
        case MapLoadErrorCode::missing_dimensions:      return "missing_dimensions";
        case MapLoadErrorCode::invalid_dimensions:      return "invalid_dimensions";
        case MapLoadErrorCode::dimensions_out_of_range: return "dimensions_out_of_range";
        case MapLoadErrorCode::missing_rows:            return "missing_rows";
        case MapLoadErrorCode::row_width_mismatch:      return "row_width_mismatch";
        case MapLoadErrorCode::unknown_symbol:          return "unknown_symbol";
        case MapLoadErrorCode::spawn_out_of_bounds:     return "spawn_out_of_bounds";
        case MapLoadErrorCode::spawn_not_walkable:      return "spawn_not_walkable";
        case MapLoadErrorCode::duplicate_spawn:         return "duplicate_spawn";
        case MapLoadErrorCode::no_walkable_spawn:       return "no_walkable_spawn";
        case MapLoadErrorCode::file_open_error:         return "file_open_error";
    }
    return "unknown";
}

const std::string& builtin_map_source() {
    static const std::string source = R"(NAM-MAP 1
width 29
height 10
spawn 14 5
---
=============================
|...@...~~~~~......@.....xxx|
|.@@@@..~~~~~......@@....xxx|
|.@@@@..~~~~......@@......xx|
|..@@...~~~.......@@@......x|
|...........................|
|x......x@@x....^^^.......~~|
|xxx....x@@x...^^^.......~~~|
|xxxxx..........^^^......~~~|
=============================
)";
    return source;
}

Map builtin_map() {
    MapLoadResult result = load_map(std::string_view{builtin_map_source()}, "<builtin>");
    if (Map* map = std::get_if<Map>(&result)) {
        return std::move(*map);
    }
    const MapLoadError& error = std::get<MapLoadError>(result);
    throw std::logic_error("built-in map failed to parse: " + error.message);
}
