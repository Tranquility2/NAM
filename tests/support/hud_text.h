#pragma once

#include <string>

#include "renderer.h"

// Shared console-test helper. The HUD glyph legend is on screen during play, so
// it names every terrain symbol and every overlay glyph in ordinary output. A
// test that asserts a glyph is *absent* is almost always asking about the map,
// not about the reference strip that explains the map, so it must drop the legend
// row first.
//
// This lives with the tests rather than in the console library because it exists
// only to make such assertions precise; production code never removes the legend.
namespace nam::test {

// True when a rendered line is the HUD legend row. An interactive frame clips
// every row to the terminal width, so a narrow frame carries only a prefix of the
// legend; the minimum length keeps a short map row from matching by accident.
[[nodiscard]] inline bool is_legend_line(const std::string& line) {
    const std::string legend = nam::console::hud_legend_text();
    return line.size() >= 8 && legend.compare(0, line.size(), line) == 0;
}

// The text with every HUD legend row removed.
[[nodiscard]] inline std::string without_legend(const std::string& text) {
    std::string result;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = text.find('\n', begin);
        const bool last = end == std::string::npos;
        const std::string line = last ? text.substr(begin) : text.substr(begin, end - begin);
        if (!is_legend_line(line)) {
            result += line;
            if (!last) result.push_back('\n');
        }
        if (last) break;
        begin = end + 1;
    }
    return result;
}

}  // namespace nam::test
