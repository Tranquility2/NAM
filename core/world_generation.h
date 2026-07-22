#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

#include "coordinates.h"
#include "map.h"

// Deterministic procedural world generation. This is the first NAM world recipe,
// "Tiny World": a compact, fixed-size map that converts a stable 64-bit seed into
// a fully connected terrain grid whose water, mountain, hill, and field regions
// form coherent spatial clusters, plus short interior barrier ridges and a
// protected central spawn.
//
// Terrain is not sampled independently per cell. Instead each feature is grown as
// a connected blob: a seeded start cell is chosen, then neighbours are repeatedly
// annexed until the feature reaches an exact target size. Water bodies, mountain
// cores, field regions, and barrier ridges are grown in a fixed order, and a
// deterministic one-cell hill halo is stamped around the mountains. This mirrors
// the clustered look of the hand-authored default map instead of white noise.
//
// Like pcg32.*, this module is frontend-neutral: it performs only fixed-width
// unsigned arithmetic, owns no global state, and touches no <iostream>, terminal,
// localization, CLI, environment, clock, or <random> facility. The same seed
// therefore produces a byte-identical map on every supported compiler and
// operating system, which is what lets the generated output act as a stable
// compatibility contract for future share/replay features.
//
// Every generation pass (boundary, connected feature growth, hill halo, spawn
// protection, and acceptance) is explicit and documented so later recipes can be
// added without silently changing Tiny World's output.

// The exact Tiny World dimensions. These are fixed for this recipe: golden maps,
// the exact feature totals, and renderer expectations all depend on them, so they
// are not configurable in this step.
inline constexpr std::size_t tiny_world_width = 29;
inline constexpr std::size_t tiny_world_height = 15;

// The fixed spawn point and the centre of the protected 3x3 open region.
inline constexpr Coordinates tiny_world_spawn{14, 7};

// The maximum number of candidate maps grown for one seed before generation gives
// up. Bounded feature growth, sparse barrier ridges, and the protected spawn make
// valid candidates common, so this limit is a safety bound rather than an expected
// code path.
inline constexpr std::uint32_t tiny_world_candidate_limit = 64;

// A successfully generated world: the map plus the deterministic metadata a
// frontend needs to identify and reproduce it. `generation_attempt` is the
// zero-based index of the candidate that was accepted (0 means the first
// candidate for the seed was already valid).
struct GeneratedWorld {
    Map map;
    std::uint64_t numeric_seed = 0;
    std::uint32_t generation_attempt = 0;
};

// Why a seed failed to produce a world. Stable, non-localized values mirroring
// the core's other typed results (see MapLoadErrorCode): callers switch on the
// code and choose their own wording.
enum class WorldGenerationErrorCode {
    candidate_limit_exhausted,  // No valid candidate within tiny_world_candidate_limit.
};

// A generation diagnostic. It carries the stable code and the numeric seed that
// produced it; presentation is a frontend concern.
struct WorldGenerationError {
    WorldGenerationErrorCode code{};
    std::uint64_t numeric_seed = 0;
};

// Either a fully validated world or the first error encountered. Generation
// never returns an invalid map: on exhaustion it returns the typed error.
using WorldGenerationResult = std::variant<GeneratedWorld, WorldGenerationError>;

// Hash arbitrary seed text into a stable 64-bit value with 64-bit FNV-1a over the
// exact input bytes. Hashing performs no case folding, whitespace trimming,
// Unicode normalization, locale conversion, or numeric parsing, so equal byte
// strings hash equally on every supported platform and embedded zero bytes are
// hashed like any other byte.
[[nodiscard]] std::uint64_t hash_seed_text(std::string_view text) noexcept;

// Generate the Tiny World for a numeric seed. Candidates are grown from a single
// Pcg32 seeded from `numeric_seed`; retries continue from the current RNG state
// rather than reseeding, so the returned attempt number is meaningful and
// reproducible. Returns the first valid candidate and its zero-based attempt, or
// a typed error if the candidate limit is exhausted. The function never prints,
// reads environment state, or uses global mutable state.
[[nodiscard]] WorldGenerationResult generate_tiny_world(std::uint64_t numeric_seed);

// A stable identifier string for an error code (for example
// "candidate_limit_exhausted"). Useful for logging and tests; not a localized
// message.
[[nodiscard]] std::string_view to_string(WorldGenerationErrorCode code) noexcept;
