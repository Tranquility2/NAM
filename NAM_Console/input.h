#pragma once

#include <chrono>
#include <cstdint>

// Semantic input model for the console frontend.
//
// This layer is deliberately independent of the raw transport. A key event
// describes *what the user meant* (an arrow, a character, end of input), never
// how it arrived on the wire. In particular none of the Key values are equal to
// the raw bytes 'A'/'B'/'C'/'D' that terminals use inside arrow-key escape
// sequences, so those letters stay free to be used as ordinary commands.
namespace nam::console {

// A single decoded input event.
enum class Key : std::uint8_t {
    character,     // A printable character; the value is in KeyEvent::character.
    up,            // Cursor up (arrow, or a movement command).
    down,          // Cursor down.
    left,          // Cursor left.
    right,         // Cursor right.
    home,          // Home key.
    end,           // End key.
    enter,         // Return/Enter.
    escape,        // A bare Escape press (not the start of a sequence).
    resize,        // The terminal was resized; the frame must be rebuilt.
    interrupt,     // A handled termination signal asked us to quit cleanly.
    unknown,       // A recognized-but-unmapped sequence; never causes movement.
    end_of_input,  // Real end of input (stdin closed / EOF).
};

// A decoded event plus its payload. `character` is only meaningful when
// `key == Key::character`.
struct KeyEvent {
    Key key = Key::unknown;
    char character = '\0';

    [[nodiscard]] static constexpr KeyEvent of(Key value) noexcept { return KeyEvent{value, '\0'}; }
    [[nodiscard]] static constexpr KeyEvent of_character(char value) noexcept {
        return KeyEvent{Key::character, value};
    }
};

[[nodiscard]] constexpr bool operator==(const KeyEvent& left, const KeyEvent& right) noexcept {
    return left.key == right.key && left.character == right.character;
}
[[nodiscard]] constexpr bool operator!=(const KeyEvent& left, const KeyEvent& right) noexcept {
    return !(left == right);
}

// The outcome of pulling a single byte from a transport.
enum class ByteStatus : std::uint8_t {
    ok,            // `byte` holds a valid transport byte.
    timeout,       // No byte arrived within the requested deadline.
    end_of_input,  // The transport reached a real end of input.
};

struct ByteResult {
    ByteStatus status = ByteStatus::end_of_input;
    unsigned char byte = 0;

    [[nodiscard]] static constexpr ByteResult ok(unsigned char value) noexcept {
        return ByteResult{ByteStatus::ok, value};
    }
    [[nodiscard]] static constexpr ByteResult timed_out() noexcept {
        return ByteResult{ByteStatus::timeout, 0};
    }
    [[nodiscard]] static constexpr ByteResult eof() noexcept {
        return ByteResult{ByteStatus::end_of_input, 0};
    }
};

// The transport seam the decoder pulls from. Keeping this abstract is what makes
// the decoder testable without a real terminal: a test supplies a scripted
// reader, while the POSIX backend supplies an fd-backed reader.
class ByteReader {
public:
    virtual ~ByteReader() = default;

    // Fetch the first byte of an event. This is a blocking read from the caller's
    // point of view, so it never reports a timeout; it returns a byte or EOF.
    [[nodiscard]] virtual ByteResult read_blocking() = 0;

    // Fetch a continuation byte, waiting at most `timeout`. Returns a byte, a
    // timeout (no continuation arrived), or EOF.
    [[nodiscard]] virtual ByteResult read_within(std::chrono::milliseconds timeout) = 0;
};

}  // namespace nam::console
