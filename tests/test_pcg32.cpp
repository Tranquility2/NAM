#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "pcg32.h"

namespace {

// Drain `count` raw outputs into a vector so sequences can be compared directly.
std::vector<std::uint32_t> draw(Pcg32& engine, int count) {
    std::vector<std::uint32_t> values;
    values.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        values.push_back(engine.next_u32());
    }
    return values;
}

}  // namespace

TEST_SUITE("game") {

TEST_CASE("Pcg32 reproduces the reference PCG-XSH-RR 64/32 vector") {
    Pcg32 engine(42u, 54u);
    CHECK(engine.next_u32() == 0xA15C02B7u);
    CHECK(engine.next_u32() == 0x7B47F409u);
    CHECK(engine.next_u32() == 0xBA1D3330u);
    CHECK(engine.next_u32() == 0x83D2F293u);
    CHECK(engine.next_u32() == 0xBFA4784Bu);
    CHECK(engine.next_u32() == 0xCBED606Eu);
}

TEST_CASE("identical seed and stream produce identical sequences") {
    Pcg32 a(1234u, 7u);
    Pcg32 b(1234u, 7u);
    CHECK(draw(a, 32) == draw(b, 32));
}

TEST_CASE("changing the seed or the stream diverges the sequence") {
    Pcg32 base(42u, 54u);
    Pcg32 other_seed(43u, 54u);
    Pcg32 other_stream(42u, 55u);

    const std::uint32_t base_first = base.next_u32();
    CHECK(base_first == 0xA15C02B7u);
    CHECK(other_seed.next_u32() != base_first);
    CHECK(other_stream.next_u32() != base_first);
}

TEST_CASE("next_bounded rejects a zero bound") {
    Pcg32 engine(42u, 54u);
    CHECK_THROWS_AS(static_cast<void>(engine.next_bounded(0u)), std::invalid_argument);
}

TEST_CASE("next_bounded always returns a value below the bound") {
    Pcg32 engine(2024u, 3u);
    for (const std::uint32_t bound : {1u, 2u, 3u, 7u, 10u, 1000u, 65535u}) {
        for (int i = 0; i < 500; ++i) {
            CHECK(engine.next_bounded(bound) < bound);
        }
    }
}

TEST_CASE("a bound of one always yields zero") {
    Pcg32 engine(7u, 54u);
    for (int i = 0; i < 8; ++i) {
        CHECK(engine.next_bounded(1u) == 0u);
    }
}

TEST_CASE("next_bounded has a locked vector for a non-power-of-two bound") {
    // Generated from Pcg32(42, 54) with bound 10; a one-bit change to seeding,
    // output, or rejection would move these values.
    Pcg32 engine(42u, 54u);
    const std::vector<std::uint32_t> expected{3u, 7u, 4u, 5u, 5u, 6u, 5u, 5u, 4u, 4u};
    std::vector<std::uint32_t> actual;
    actual.reserve(expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        actual.push_back(engine.next_bounded(10u));
    }
    CHECK(actual == expected);
}

TEST_CASE("a snapshot restores the exact continuation of the sequence") {
    Pcg32 engine(123u, 9u);
    (void)engine.next_u32();
    (void)engine.next_u32();

    const Pcg32State state = engine.snapshot();
    const std::uint32_t expected_next = engine.next_u32();

    Pcg32 restored(state);
    CHECK(restored.next_u32() == expected_next);

    // And it keeps tracking the original from there on.
    CHECK(draw(restored, 16) == draw(engine, 16));
}

TEST_CASE("restoring a state with an even increment is rejected") {
    const Pcg32State bad{0x1234u, 0x10u};  // increment is even.
    CHECK_THROWS_AS(Pcg32{bad}, std::invalid_argument);
}

TEST_CASE("state serialization is canonical, uppercase, and fixed-width") {
    const Pcg32State state{0x0123456789ABCDEFull, 0x6Dull};
    const std::string text = serialize_pcg32_state(state);
    CHECK(text == "PCG32-V1:0123456789ABCDEF:000000000000006D");
    CHECK(text.size() == 42);
    for (const char character : text) {
        // The output is uppercase-only: no lowercase hex digit ever appears.
        CHECK((character < 'a' || character > 'f'));
    }
}

TEST_CASE("serialize/parse round-trips byte-identically") {
    Pcg32 engine(0xDEADBEEFull, 0xC0FFEEull);
    (void)engine.next_u32();
    (void)engine.next_u32();
    (void)engine.next_u32();
    const Pcg32State state = engine.snapshot();

    const std::string text = serialize_pcg32_state(state);
    const Pcg32StateParseResult result = parse_pcg32_state(text);
    REQUIRE(std::holds_alternative<Pcg32State>(result));

    const Pcg32State parsed = std::get<Pcg32State>(result);
    CHECK(parsed.state == state.state);
    CHECK(parsed.increment == state.increment);
    CHECK(serialize_pcg32_state(parsed) == text);
}

TEST_CASE("parsing the canonical example yields the exact words") {
    const Pcg32StateParseResult result =
        parse_pcg32_state("PCG32-V1:0123456789ABCDEF:000000000000006D");
    REQUIRE(std::holds_alternative<Pcg32State>(result));
    const Pcg32State parsed = std::get<Pcg32State>(result);
    CHECK(parsed.state == 0x0123456789ABCDEFull);
    CHECK(parsed.increment == 0x6Dull);
}

TEST_CASE("state parsing reports each typed error distinctly") {
    auto error_of = [](std::string_view text) {
        const Pcg32StateParseResult result = parse_pcg32_state(text);
        REQUIRE(std::holds_alternative<Pcg32StateError>(result));
        return std::get<Pcg32StateError>(result).code;
    };

    SUBCASE("a non-PCG string is an invalid format") {
        CHECK(error_of("not-a-state") == Pcg32StateErrorCode::invalid_format);
    }
    SUBCASE("a missing separator is an invalid format") {
        CHECK(error_of("PCG32-V1") == Pcg32StateErrorCode::invalid_format);
    }
    SUBCASE("a wrong field width is an invalid format") {
        CHECK(error_of("PCG32-V1:0123456789ABCDEF:00000000000000001") ==
              Pcg32StateErrorCode::invalid_format);
    }
    SUBCASE("an unknown version is unsupported") {
        CHECK(error_of("PCG32-V2:0000000000000000:0000000000000001") ==
              Pcg32StateErrorCode::unsupported_version);
    }
    SUBCASE("a non-hex digit is invalid hex") {
        CHECK(error_of("PCG32-V1:000000000000000G:0000000000000001") ==
              Pcg32StateErrorCode::invalid_hex);
    }
    SUBCASE("lowercase hex is rejected as invalid hex") {
        CHECK(error_of("PCG32-V1:00000000000000ab:0000000000000001") ==
              Pcg32StateErrorCode::invalid_hex);
    }
    SUBCASE("an even increment is invalid") {
        CHECK(error_of("PCG32-V1:0000000000000000:0000000000000000") ==
              Pcg32StateErrorCode::invalid_increment);
    }
}

TEST_CASE("error codes map to stable identifier strings") {
    // to_string returns a std::string_view; wrapping it in std::string makes
    // doctest decompose and stringify std::string instead. MSVC would otherwise
    // instantiate its string_view ostream inserter against an incomplete
    // std::basic_ostream (C2027); the comparisons themselves are unchanged.
    CHECK(std::string(to_string(Pcg32StateErrorCode::invalid_format)) == "invalid_format");
    CHECK(std::string(to_string(Pcg32StateErrorCode::unsupported_version)) == "unsupported_version");
    CHECK(std::string(to_string(Pcg32StateErrorCode::invalid_hex)) == "invalid_hex");
    CHECK(std::string(to_string(Pcg32StateErrorCode::invalid_increment)) == "invalid_increment");
}

}  // TEST_SUITE("game")
