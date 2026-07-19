#pragma once

#include <string>
#include <vector>

namespace nam::console {

// A fully composed frame: the exact visible rows to present, top to bottom.
// Each entry is one screen row of already-laid-out content and may contain SGR
// colour escapes when colour is enabled. Cursor positioning and stale-line
// erasure are added by the terminal backend at draw time, so the renderer that
// produces a Frame stays free of terminal-control details and is easy to test.
using Frame = std::vector<std::string>;

}  // namespace nam::console
