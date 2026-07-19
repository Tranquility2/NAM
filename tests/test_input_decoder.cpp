#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <string_view>

#include "input.h"
#include "input_decoder.h"

using namespace nam::console;

namespace {

// Decode a single event from a fixed byte string with no injected timeouts.
KeyEvent decode(std::string_view bytes) {
    InputDecoder decoder;
    ScriptedByteReader reader = ScriptedByteReader::from_bytes(bytes);
    return decoder.decode(reader);
}

constexpr char ESC = '\x1b';

}  // namespace

TEST_SUITE("input") {

TEST_CASE("printable bytes decode to character events") {
    CHECK(decode("a") == KeyEvent::of_character('a'));
    CHECK(decode("Q") == KeyEvent::of_character('Q'));
    CHECK(decode(" ") == KeyEvent::of_character(' '));
    CHECK(decode("~") == KeyEvent::of_character('~'));
}

TEST_CASE("transport letters remain ordinary characters") {
    // A, B, C, D are used inside arrow sequences on the wire, but as bare bytes
    // they are plain characters and must never be mistaken for movement.
    CHECK(decode("A") == KeyEvent::of_character('A'));
    CHECK(decode("B") == KeyEvent::of_character('B'));
    CHECK(decode("C") == KeyEvent::of_character('C'));
    CHECK(decode("D") == KeyEvent::of_character('D'));
}

TEST_CASE("carriage return and newline both decode to Enter") {
    CHECK(decode("\r").key == Key::enter);
    CHECK(decode("\n").key == Key::enter);
}

TEST_CASE("CSI arrow sequences map to the four directions") {
    CHECK(decode(std::string{ESC} + "[A").key == Key::up);
    CHECK(decode(std::string{ESC} + "[B").key == Key::down);
    CHECK(decode(std::string{ESC} + "[C").key == Key::right);
    CHECK(decode(std::string{ESC} + "[D").key == Key::left);
}

TEST_CASE("CSI Home/End arrive as both letter and numeric forms") {
    CHECK(decode(std::string{ESC} + "[H").key == Key::home);
    CHECK(decode(std::string{ESC} + "[F").key == Key::end);
    CHECK(decode(std::string{ESC} + "[1~").key == Key::home);
    CHECK(decode(std::string{ESC} + "[7~").key == Key::home);
    CHECK(decode(std::string{ESC} + "[4~").key == Key::end);
    CHECK(decode(std::string{ESC} + "[8~").key == Key::end);
}

TEST_CASE("SS3 sequences map arrows and Home/End") {
    CHECK(decode(std::string{ESC} + "OA").key == Key::up);
    CHECK(decode(std::string{ESC} + "OB").key == Key::down);
    CHECK(decode(std::string{ESC} + "OC").key == Key::right);
    CHECK(decode(std::string{ESC} + "OD").key == Key::left);
    CHECK(decode(std::string{ESC} + "OH").key == Key::home);
    CHECK(decode(std::string{ESC} + "OF").key == Key::end);
}

TEST_CASE("a lone Escape is reported when no continuation arrives") {
    // Exhausted script: the continuation read times out -> bare Escape.
    CHECK(decode(std::string{ESC}).key == Key::escape);

    // Explicit timeout after ESC -> bare Escape.
    InputDecoder decoder;
    ScriptedByteReader reader;
    reader.push_byte(0x1b).push_timeout();
    CHECK(decoder.decode(reader).key == Key::escape);
}

TEST_CASE("an escape sequence delivered within the window still decodes") {
    // ESC, then the continuation bytes arrive as real (ok) reads.
    InputDecoder decoder;
    ScriptedByteReader reader;
    reader.push_byte(0x1b).push_byte('[').push_byte('C');
    CHECK(decoder.decode(reader).key == Key::right);
}

TEST_CASE("a continuation that times out mid-sequence never moves the actor") {
    // ESC '[' then a gap: the CSI is abandoned as unknown, not an arrow.
    InputDecoder decoder;
    ScriptedByteReader reader;
    reader.push_byte(0x1b).push_byte('[').push_timeout().push_byte('A');
    CHECK(decoder.decode(reader).key == Key::unknown);
}

TEST_CASE("unknown CSI sequences are consumed and reported as unknown") {
    CHECK(decode(std::string{ESC} + "[Z").key == Key::unknown);   // shift-tab.
    CHECK(decode(std::string{ESC} + "[5~").key == Key::unknown);  // page up.
    CHECK(decode(std::string{ESC} + "[3~").key == Key::unknown);  // delete.
    CHECK(decode(std::string{ESC} + "[1;5C").key == Key::unknown);  // ctrl-arrow.
}

TEST_CASE("ESC followed by an unrelated byte is unknown rather than movement") {
    InputDecoder decoder;
    ScriptedByteReader reader;
    reader.push_byte(0x1b).push_byte('x');
    CHECK(decoder.decode(reader).key == Key::unknown);
}

TEST_CASE("unknown SS3 finals are unknown") {
    CHECK(decode(std::string{ESC} + "OZ").key == Key::unknown);
}

TEST_CASE("end of input is distinct from a bare Escape") {
    // Empty script: the very first (blocking) read is EOF.
    CHECK(decode("").key == Key::end_of_input);

    InputDecoder decoder;
    ScriptedByteReader reader;
    reader.push_eof();
    CHECK(decoder.decode(reader).key == Key::end_of_input);
}

TEST_CASE("stray control bytes carry no game meaning") {
    CHECK(decode(std::string(1, '\x01')).key == Key::unknown);  // Ctrl-A.
    CHECK(decode(std::string(1, '\x7f')).key == Key::unknown);  // DEL.
}

TEST_CASE("an over-long CSI parameter run terminates as unknown") {
    // Far more parameter bytes than max_csi_bytes, with no final byte, must not
    // hang and must resolve to unknown once the reader is exhausted.
    std::string bytes = std::string{ESC} + "[";
    bytes += std::string(200, '1');
    CHECK(decode(bytes).key == Key::unknown);
}

TEST_CASE("decoding is deterministic for a fixed script") {
    const std::string script = std::string{ESC} + "[A";
    CHECK(decode(script) == decode(script));
}

}  // TEST_SUITE("input")
