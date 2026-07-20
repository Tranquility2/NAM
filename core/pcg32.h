#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

// A deterministic, frontend-neutral pseudo-random engine: the reference
// PCG-XSH-RR 64/32 generator (https://www.pcg-random.org). It exists so future
// procedural generation and replay features can produce byte-identical worlds on
// every supported compiler and operating system. The engine performs only
// fixed-width unsigned arithmetic, owns no global state, and touches no
// <random> engine or distribution — callers construct and pass instances
// explicitly so ordering dependencies stay visible and testable.

// The two-word internal state of a Pcg32 instance. `increment` is always odd;
// its full value selects the stream. A snapshot of a live engine can be
// serialized, stored, and later restored to resume the exact same sequence.
struct Pcg32State {
    std::uint64_t state = 0;
    std::uint64_t increment = 0;
};

// Why a serialized Pcg32State string could not be parsed. Stable, non-localized
// values mirroring the core's other typed parse results (see MapLoadErrorCode):
// callers switch on the code and choose their own wording.
enum class Pcg32StateErrorCode {
    invalid_format,       // Prefix, separators, or field widths were wrong.
    unsupported_version,  // A well-formed marker named an unknown state version.
    invalid_hex,          // A state/increment field held a non-hex character.
    invalid_increment,    // The parsed increment was even (an invalid stream).
};

// A parse diagnostic. It carries only the stable code; presentation is a
// frontend concern.
struct Pcg32StateError {
    Pcg32StateErrorCode code{};
};

// Either a fully validated state or the first error encountered. Parsing never
// silently substitutes a seed for malformed input.
using Pcg32StateParseResult = std::variant<Pcg32State, Pcg32StateError>;

// The reference PCG-XSH-RR 64/32 generator.
class Pcg32 {
public:
    // Seed the engine. The lower 63 bits of `stream` select one of 2^63 odd LCG
    // increments. The default stream selector is a released compatibility
    // constant and must not be changed silently.
    explicit Pcg32(std::uint64_t seed, std::uint64_t stream = 54u) noexcept;

    // Restore an engine from a previously captured state. Throws
    // std::invalid_argument when `state.increment` is even, because an even
    // increment cannot be produced by seeding and would corrupt the stream.
    explicit Pcg32(const Pcg32State& state);

    // The next 32-bit output. Advances the internal state by one step.
    [[nodiscard]] std::uint32_t next_u32() noexcept;

    // A uniformly distributed value in [0, exclusive_upper_bound) using unbiased
    // rejection sampling. Throws std::invalid_argument when the bound is 0.
    [[nodiscard]] std::uint32_t next_bounded(std::uint32_t exclusive_upper_bound);

    // The exact current internal state, suitable for serialization or restore.
    [[nodiscard]] Pcg32State snapshot() const noexcept { return {state_, increment_}; }

private:
    std::uint64_t state_ = 0;
    std::uint64_t increment_ = 0;
};

// Serialize a state to its canonical text form:
//     "PCG32-V1:" <16 uppercase hex state> ":" <16 uppercase hex increment>
// for example "PCG32-V1:0123456789ABCDEF:000000000000006D". The representation
// is always uppercase and fixed-width so it round-trips byte-identically.
[[nodiscard]] std::string serialize_pcg32_state(const Pcg32State& state);

// Parse the canonical text form. Only the exact prefix, separator positions,
// field widths, and uppercase hexadecimal digits are accepted, and an even
// increment is rejected. The parser never reads outside `text`; malformed,
// truncated, oversized, or non-ASCII input yields a typed error.
[[nodiscard]] Pcg32StateParseResult parse_pcg32_state(std::string_view text);

// A stable identifier string for an error code (for example "invalid_hex").
[[nodiscard]] std::string_view to_string(Pcg32StateErrorCode code) noexcept;
