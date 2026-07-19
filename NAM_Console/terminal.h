#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "frame.h"
#include "input.h"

namespace nam::console {

// The usable character grid of the terminal. `valid()` is false when the size
// could not be determined; callers then fall back to a conservative default.
struct TerminalSize {
    int columns = 0;
    int rows = 0;

    [[nodiscard]] bool valid() const noexcept { return columns > 0 && rows > 0; }
};

// Why entering interactive terminal mode failed. Chooses the exit code and the
// message shown to the user when a session cannot be created.
enum class TerminalError {
    none,
    not_a_terminal,     // stdin or stdout is not an interactive terminal.
    mode_query_failed,  // The current console mode could not be read.
    mode_set_failed,    // Raw/interactive mode could not be applied.
};

// A short human-readable explanation for a TerminalError.
[[nodiscard]] std::string_view describe(TerminalError error) noexcept;

// True when the corresponding standard stream is connected to an interactive
// terminal. Implemented per platform; safe to call before creating a session.
[[nodiscard]] bool stdin_is_interactive();
[[nodiscard]] bool stdout_is_interactive();

// Whether a cursor-addressable interactive UI is worth attempting on this
// platform. Both standard streams must be terminals. POSIX additionally needs a
// non-dumb TERM, supplied via `env_ansi`; Windows negotiates VT/native output in
// its backend and ignores `env_ansi`. Keeping this policy in the platform layer
// avoids spreading `#ifdef`s through the application.
[[nodiscard]] bool interactive_display_supported(bool env_ansi);

// Platform-private state; each backend defines its own version and only one
// backend is compiled, so there is no conflicting definition.
class TerminalImpl;

// The result of attempting to enter interactive mode (declared before the
// class, defined after it, because std::optional needs the complete type).
struct TerminalStartup;

// A movable, non-copyable RAII guard over the interactive terminal. Construction
// (via create) puts the terminal into raw/event-driven mode; destruction always
// restores the original modes, resets colour, and makes the cursor visible again
// — on normal exit, early return, thrown exception, EOF, or a handled signal.
class TerminalSession {
public:
    // Enter interactive mode. Never throws; failures are reported in the result
    // so the caller can pick a plain fallback or a clean exit code.
    [[nodiscard]] static TerminalStartup create();

    ~TerminalSession();

    TerminalSession(TerminalSession&&) noexcept;
    TerminalSession& operator=(TerminalSession&&) noexcept;
    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;

    // Block until the next semantic input event. Idle cost is effectively zero:
    // the call sleeps in the OS until a key, a resize, a termination signal, or
    // end of input wakes it. A handled termination signal yields Key::interrupt;
    // a resize yields Key::resize.
    [[nodiscard]] KeyEvent read_event();

    // Present a composed frame. This is the single output channel for interactive
    // mode: cursor homing and stale-line erasure are applied here and the frame
    // is written in one operation.
    void draw(const Frame& frame);

    // The current terminal grid size, re-queried live so resizes are respected.
    [[nodiscard]] TerminalSize size() const;

    // Whether ANSI/VT sequences may be emitted. Always true on POSIX; on Windows
    // it reflects whether virtual-terminal processing was enabled.
    [[nodiscard]] bool supports_ansi() const noexcept;

private:
    explicit TerminalSession(std::unique_ptr<TerminalImpl> impl) noexcept;

    std::unique_ptr<TerminalImpl> impl_;
};

// On success `session` holds an engaged value; on failure `error` explains why.
struct TerminalStartup {
    std::optional<TerminalSession> session;
    TerminalError error = TerminalError::none;
};

}  // namespace nam::console
