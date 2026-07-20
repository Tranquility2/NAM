#include "pcg32.h"

#include <cstddef>
#include <stdexcept>

namespace {

// The reference PCG multiplier (a fixed constant of the algorithm; changing it
// would produce a different, incompatible generator).
constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;

// The fixed width, in hex digits, of each serialized 64-bit state word.
constexpr std::size_t kHexWidth = 16;

// Append `value` as exactly 16 uppercase hexadecimal digits, most significant
// nibble first. Manual formatting keeps this independent of any stream, locale,
// or fill/width state.
void append_hex16(std::string& out, std::uint64_t value) {
    constexpr char digits[] = "0123456789ABCDEF";
    for (int shift = 60; shift >= 0; shift -= 4) {
        const std::uint64_t nibble = (value >> static_cast<unsigned>(shift)) & 0xFu;
        out.push_back(digits[static_cast<std::size_t>(nibble)]);
    }
}

// Parse exactly 16 uppercase hexadecimal digits into a 64-bit value. Returns
// false on any non-hex byte (including non-ASCII), so malformed fields are
// reported rather than silently coerced. The caller guarantees the width.
[[nodiscard]] bool parse_hex16(std::string_view field, std::uint64_t& out) noexcept {
    std::uint64_t value = 0;
    for (const char character : field) {
        std::uint64_t digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint64_t>(character - '0');
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint64_t>(character - 'A' + 10);
        } else {
            return false;
        }
        value = (value << 4u) | digit;
    }
    out = value;
    return true;
}

}  // namespace

Pcg32::Pcg32(std::uint64_t seed, std::uint64_t stream) noexcept {
    // Reference PCG seeding: start from zero state, derive an odd increment from
    // the stream selector, step once, fold in the seed (mod 2^64 by unsigned
    // overflow), and step once more.
    state_ = 0u;
    increment_ = (stream << 1u) | 1u;
    (void)next_u32();
    state_ += seed;
    (void)next_u32();
}

Pcg32::Pcg32(const Pcg32State& state) : state_(state.state), increment_(state.increment) {
    // Every increment produced by seeding is odd (its low bit selects the
    // stream). An even increment cannot occur naturally and would break the
    // generator, so a restore from such a state is rejected outright.
    if ((increment_ & 1u) == 0u) {
        throw std::invalid_argument("Pcg32 increment must be odd");
    }
}

std::uint32_t Pcg32::next_u32() noexcept {
    const std::uint64_t old_state = state_;
    state_ = old_state * kMultiplier + increment_;

    // XSH-RR output transform on the pre-advance state.
    const std::uint32_t xorshifted =
        static_cast<std::uint32_t>(((old_state >> 18u) ^ old_state) >> 27u);
    const std::uint32_t rot = static_cast<std::uint32_t>(old_state >> 59u);

    // Rotate right by `rot`. Computing the left shift as ((32 - rot) & 31)
    // avoids both a shift-by-32 (undefined) when rot is 0 and any unary minus on
    // an unsigned value (which some compilers reject under warnings-as-errors).
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

std::uint32_t Pcg32::next_bounded(std::uint32_t exclusive_upper_bound) {
    if (exclusive_upper_bound == 0u) {
        throw std::invalid_argument("Pcg32::next_bounded requires a positive bound");
    }

    // Reject the lowest (2^32 mod bound) outputs so the accepted range is an
    // exact multiple of the bound, eliminating modulo bias. `0 - bound` is the
    // defined unsigned wraparound value 2^32 - bound; mod bound yields the
    // threshold with no signed arithmetic.
    const std::uint32_t threshold =
        (std::uint32_t{0} - exclusive_upper_bound) % exclusive_upper_bound;

    for (;;) {
        const std::uint32_t value = next_u32();
        if (value >= threshold) {
            return value % exclusive_upper_bound;
        }
    }
}

std::string serialize_pcg32_state(const Pcg32State& state) {
    std::string text = "PCG32-V1:";
    append_hex16(text, state.state);
    text.push_back(':');
    append_hex16(text, state.increment);
    return text;
}

Pcg32StateParseResult parse_pcg32_state(std::string_view text) {
    constexpr std::string_view family = "PCG32-V";

    // Must begin with the family marker before anything else is inspected.
    if (text.size() < family.size() || text.substr(0, family.size()) != family) {
        return Pcg32StateError{Pcg32StateErrorCode::invalid_format};
    }

    // The version token runs from after the marker to the first ':'. Hex fields
    // never contain ':', so this separator is unambiguous.
    const std::size_t version_end = text.find(':', family.size());
    if (version_end == std::string_view::npos) {
        return Pcg32StateError{Pcg32StateErrorCode::invalid_format};
    }
    const std::string_view version = text.substr(family.size(), version_end - family.size());
    if (version != "1") {
        return Pcg32StateError{Pcg32StateErrorCode::unsupported_version};
    }

    // Canonical v1 body: <16 hex> ':' <16 hex> immediately after "PCG32-V1:".
    const std::size_t state_begin = version_end + 1;
    const std::size_t separator = state_begin + kHexWidth;
    const std::size_t increment_begin = separator + 1;
    const std::size_t expected_size = increment_begin + kHexWidth;
    if (text.size() != expected_size || text[separator] != ':') {
        return Pcg32StateError{Pcg32StateErrorCode::invalid_format};
    }

    Pcg32State parsed;
    if (!parse_hex16(text.substr(state_begin, kHexWidth), parsed.state) ||
        !parse_hex16(text.substr(increment_begin, kHexWidth), parsed.increment)) {
        return Pcg32StateError{Pcg32StateErrorCode::invalid_hex};
    }

    if ((parsed.increment & 1u) == 0u) {
        return Pcg32StateError{Pcg32StateErrorCode::invalid_increment};
    }

    return parsed;
}

std::string_view to_string(Pcg32StateErrorCode code) noexcept {
    switch (code) {
        case Pcg32StateErrorCode::invalid_format:      return "invalid_format";
        case Pcg32StateErrorCode::unsupported_version: return "unsupported_version";
        case Pcg32StateErrorCode::invalid_hex:         return "invalid_hex";
        case Pcg32StateErrorCode::invalid_increment:   return "invalid_increment";
    }
    return "unknown";
}
