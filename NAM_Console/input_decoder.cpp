#include "input_decoder.h"

#include <cctype>

namespace nam::console {

namespace {

[[nodiscard]] bool is_csi_final(unsigned char byte) noexcept {
    return byte >= 0x40 && byte <= 0x7E;  // ECMA-48 final byte range.
}

[[nodiscard]] bool is_printable(unsigned char byte) noexcept {
    return byte >= 0x20 && byte < 0x7F;
}

// Interpret an assembled CSI sequence (the bytes between "ESC [" and the final
// byte, plus the final byte itself). Only movement-relevant sequences are
// mapped; everything else is deliberately Key::unknown.
[[nodiscard]] KeyEvent interpret_csi(const std::string& params, char final_byte) {
    if (params.empty()) {
        switch (final_byte) {
            case 'A': return KeyEvent::of(Key::up);
            case 'B': return KeyEvent::of(Key::down);
            case 'C': return KeyEvent::of(Key::right);
            case 'D': return KeyEvent::of(Key::left);
            case 'H': return KeyEvent::of(Key::home);
            case 'F': return KeyEvent::of(Key::end);
            default:  return KeyEvent::of(Key::unknown);
        }
    }

    if (final_byte == '~') {
        // Numeric keypad / editing keys: "1~"/"7~" are Home, "4~"/"8~" are End.
        // A leading numeric run with no modifier (';') is all we accept.
        std::size_t digits = 0;
        while (digits < params.size() &&
               std::isdigit(static_cast<unsigned char>(params[digits])) != 0) {
            ++digits;
        }
        if (digits == params.size()) {
            if (params == "1" || params == "7") return KeyEvent::of(Key::home);
            if (params == "4" || params == "8") return KeyEvent::of(Key::end);
        }
    }

    return KeyEvent::of(Key::unknown);
}

}  // namespace

KeyEvent InputDecoder::decode(ByteReader& reader) const {
    const ByteResult first = reader.read_blocking();
    if (first.status == ByteStatus::end_of_input) {
        return KeyEvent::of(Key::end_of_input);
    }

    const unsigned char byte = first.byte;
    if (byte == 0x1B) {
        return decode_escape(reader);
    }
    if (byte == '\r' || byte == '\n') {
        return KeyEvent::of(Key::enter);
    }
    if (is_printable(byte)) {
        return KeyEvent::of_character(static_cast<char>(byte));
    }
    // Other control bytes (backspace, tabs, stray C0) carry no game meaning.
    return KeyEvent::of(Key::unknown);
}

KeyEvent InputDecoder::decode_escape(ByteReader& reader) const {
    const ByteResult next = reader.read_within(continuation_timeout_);
    if (next.status != ByteStatus::ok) {
        // No continuation within the window (timeout) or the stream ended right
        // after ESC: the user meant a bare Escape.
        return KeyEvent::of(Key::escape);
    }
    if (next.byte == '[') {
        return decode_csi(reader);
    }
    if (next.byte == 'O') {
        return decode_ss3(reader);
    }
    // ESC followed by an unrelated byte: consume it and report unknown so it can
    // never be mistaken for movement.
    return KeyEvent::of(Key::unknown);
}

KeyEvent InputDecoder::decode_csi(ByteReader& reader) const {
    std::string params;
    params.reserve(max_csi_bytes);

    for (std::size_t read = 0; read < max_csi_bytes; ++read) {
        const ByteResult result = reader.read_within(continuation_timeout_);
        if (result.status != ByteStatus::ok) {
            return KeyEvent::of(Key::unknown);  // Truncated sequence; consumed.
        }
        if (is_csi_final(result.byte)) {
            return interpret_csi(params, static_cast<char>(result.byte));
        }
        params.push_back(static_cast<char>(result.byte));
    }

    // The sequence exceeded the bound: keep draining (still bounded) until a
    // final byte so the transport is left at a clean boundary, then give up.
    for (std::size_t drained = 0; drained < max_csi_bytes; ++drained) {
        const ByteResult result = reader.read_within(continuation_timeout_);
        if (result.status != ByteStatus::ok || is_csi_final(result.byte)) {
            break;
        }
    }
    return KeyEvent::of(Key::unknown);
}

KeyEvent InputDecoder::decode_ss3(ByteReader& reader) const {
    const ByteResult result = reader.read_within(continuation_timeout_);
    if (result.status != ByteStatus::ok) {
        return KeyEvent::of(Key::unknown);
    }
    switch (result.byte) {
        case 'A': return KeyEvent::of(Key::up);
        case 'B': return KeyEvent::of(Key::down);
        case 'C': return KeyEvent::of(Key::right);
        case 'D': return KeyEvent::of(Key::left);
        case 'H': return KeyEvent::of(Key::home);
        case 'F': return KeyEvent::of(Key::end);
        default:  return KeyEvent::of(Key::unknown);
    }
}

ScriptedByteReader ScriptedByteReader::from_bytes(std::string_view bytes) {
    ScriptedByteReader reader;
    reader.script_.reserve(bytes.size());
    for (const char value : bytes) {
        reader.script_.push_back(ByteResult::ok(static_cast<unsigned char>(value)));
    }
    return reader;
}

ScriptedByteReader& ScriptedByteReader::push_byte(unsigned char value) {
    script_.push_back(ByteResult::ok(value));
    return *this;
}

ScriptedByteReader& ScriptedByteReader::push_timeout() {
    script_.push_back(ByteResult::timed_out());
    return *this;
}

ScriptedByteReader& ScriptedByteReader::push_eof() {
    script_.push_back(ByteResult::eof());
    return *this;
}

ByteResult ScriptedByteReader::read_blocking() {
    if (cursor_ >= script_.size()) {
        return ByteResult::eof();
    }
    const ByteResult result = script_[cursor_++];
    // A blocking read never observes a timeout; treat a scripted gap as EOF.
    if (result.status == ByteStatus::timeout) {
        return ByteResult::eof();
    }
    return result;
}

ByteResult ScriptedByteReader::read_within(std::chrono::milliseconds /*timeout*/) {
    if (cursor_ >= script_.size()) {
        return ByteResult::timed_out();
    }
    return script_[cursor_++];
}

}  // namespace nam::console
