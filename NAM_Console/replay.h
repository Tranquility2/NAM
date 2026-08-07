#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "direction.h"

namespace nam::console {

// A run's movement commands as a compact, replayable string.
//
// Plain mode is already a replay engine: it reads commands from an input stream
// and, given the same world, produces byte-identical output. What was missing was
// a record of the commands a run made. This is that record, written into the
// report so a finished run can be reproduced without a side file.
//
// The encoding is run-length: a direction letter, optionally preceded by the
// number of times it repeats. `4d3sw` is four rights, three downs, one up. Only
// `w`, `a`, `s`, `d` appear, which is what lets a decoded string be handed
// straight back to plain mode - `nam_console --seed-number N` reading the string
// replays the run.
//
// Every attempted move is recorded, including one blocked by terrain: a blocked
// attempt changes no world state but does change the attempt count the report
// shows, so a replay that dropped it would not reproduce the run it describes.

// The largest repeat count a single token may carry. A run cannot legitimately
// repeat one direction more than this on any tier, so a larger count is a
// malformed or hostile string rather than a record of play.
inline constexpr std::size_t replay_max_repeat = 9999;

// The largest number of moves one encoded string may expand to. Bounds the work a
// single decoded line can ask for, so a short hostile string cannot demand an
// enormous expansion.
inline constexpr std::size_t replay_max_moves = 65536;

// Encode a run's directions. Deterministic, ASCII-only, and locale-independent.
// An empty run encodes to an empty string.
[[nodiscard]] std::string encode_replay(const std::vector<Direction>& moves);

// Decode a replay string back to the directions it records.
//
// Returns std::nullopt when the string is not a replay: any character other than
// a decimal digit or a direction letter, a digit run not followed by a letter, a
// repeat count of zero or above `replay_max_repeat`, or a total expansion above
// `replay_max_moves`. Callers use the empty result to fall back to their ordinary
// unknown-command handling, so a typo is never silently played as moves.
//
// A single direction letter decodes to a single move, which makes this a superset
// of the one-command-per-line form rather than a competing grammar.
[[nodiscard]] std::optional<std::vector<Direction>> decode_replay(const std::string& text);

}  // namespace nam::console
