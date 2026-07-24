#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "input.h"

namespace nam::console {

// Decodes raw terminal bytes into semantic KeyEvents. The decoder owns no
// transport: it pulls bytes through a ByteReader, so the exact same logic runs
// against a live file descriptor or a scripted byte buffer in a unit test.
//
// It recognizes a bounded grammar and can never read unboundedly or block
// forever waiting for a continuation:
//   * CSI arrows/Home/End:  ESC [ A|B|C|D|H|F  and  ESC [ 1~|4~|7~|8~
//   * CSI Page Up/Down:     ESC [ 5~|6~
//   * SS3 arrows/Home/End:  ESC O A|B|C|D|H|F
//   * a bare Escape (no continuation within the timeout)
//   * any other CSI sequence is consumed through its final byte and reported as
//     Key::unknown, so an unrecognized sequence never moves the actor.
class InputDecoder {
public:
    // The continuation timeout distinguishes a lone Escape from a slowly
    // delivered escape sequence. 25-50 ms is the usual safe window.
    static constexpr std::chrono::milliseconds default_continuation_timeout{40};

    // The largest number of parameter/intermediate bytes tolerated inside a CSI
    // sequence before it is abandoned as unknown. Bounds worst-case reads.
    static constexpr std::size_t max_csi_bytes = 32;

    explicit InputDecoder(
        std::chrono::milliseconds continuation_timeout = default_continuation_timeout) noexcept
        : continuation_timeout_(continuation_timeout) {}

    [[nodiscard]] std::chrono::milliseconds continuation_timeout() const noexcept {
        return continuation_timeout_;
    }

    // Decode exactly one event, pulling as many bytes as the grammar requires.
    [[nodiscard]] KeyEvent decode(ByteReader& reader) const;

private:
    [[nodiscard]] KeyEvent decode_escape(ByteReader& reader) const;
    [[nodiscard]] KeyEvent decode_csi(ByteReader& reader) const;
    [[nodiscard]] KeyEvent decode_ss3(ByteReader& reader) const;

    std::chrono::milliseconds continuation_timeout_;
};

// A deterministic, in-memory ByteReader used to drive the decoder from a fixed
// script. This is the seam that makes decoding testable without a terminal, but
// it is also handy for tools and demos.
//
// Semantics:
//   * read_blocking(): returns the next scripted result; a scripted timeout is
//     reported as EOF because a genuine blocking read never times out.
//   * read_within(): returns the next scripted result; when the script is
//     exhausted it reports a timeout, modelling "no continuation byte arrived".
class ScriptedByteReader final : public ByteReader {
public:
    ScriptedByteReader() = default;

    // Build a reader whose events are exactly the given bytes, with no injected
    // timeouts. Continuation reads past the end report a timeout.
    [[nodiscard]] static ScriptedByteReader from_bytes(std::string_view bytes);

    ScriptedByteReader& push_byte(unsigned char value);
    ScriptedByteReader& push_timeout();
    ScriptedByteReader& push_eof();

    [[nodiscard]] bool exhausted() const noexcept { return cursor_ >= script_.size(); }

    [[nodiscard]] ByteResult read_blocking() override;
    [[nodiscard]] ByteResult read_within(std::chrono::milliseconds timeout) override;

private:
    std::vector<ByteResult> script_;
    std::size_t cursor_ = 0;
};

}  // namespace nam::console
