// Windows console backend for TerminalSession.
//
// Uses the Win32 console API directly: raw input via SetConsoleMode +
// ReadConsoleInput, sizing via GetConsoleScreenBufferInfo, and output either
// through virtual-terminal (ANSI) sequences when the console supports them or
// through native cursor/fill calls when it does not. Original modes and cursor
// visibility are restored by the RAII destructor on every exit path.

#include <memory>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "input.h"
#include "input_decoder.h"
#include "terminal.h"

// Older SDKs may not define the virtual-terminal flags; provide them so the
// target still compiles and negotiates VT at runtime.
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif

namespace nam::console {

namespace {

// Set by the console control handler (a separate OS thread) to request a clean
// shutdown; the event wakes a blocked ReadConsoleInput wait.
volatile LONG g_quit_requested = 0;
HANDLE g_quit_event = nullptr;

BOOL WINAPI console_control_handler(DWORD control_type) {
    switch (control_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            InterlockedExchange(&g_quit_requested, 1);
            if (g_quit_event != nullptr) {
                SetEvent(g_quit_event);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

void write_console(HANDLE handle, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(bytes.size() - offset);
        if (WriteConsoleA(handle, bytes.data() + offset, chunk, &written, nullptr) == 0 ||
            written == 0) {
            return;  // Teardown must never fail loudly.
        }
        offset += written;
    }
}

// Map a Win32 key-down record to a semantic event. A record with no recognized
// virtual key and no printable character is reported as Key::unknown; the caller
// then ignores pure modifier presses.
[[nodiscard]] KeyEvent translate_key(const KEY_EVENT_RECORD& key) {
    switch (key.wVirtualKeyCode) {
        case VK_UP:     return KeyEvent::of(Key::up);
        case VK_DOWN:   return KeyEvent::of(Key::down);
        case VK_LEFT:   return KeyEvent::of(Key::left);
        case VK_RIGHT:  return KeyEvent::of(Key::right);
        case VK_HOME:   return KeyEvent::of(Key::home);
        case VK_END:    return KeyEvent::of(Key::end);
        case VK_PRIOR:  return KeyEvent::of(Key::page_up);
        case VK_NEXT:   return KeyEvent::of(Key::page_down);
        case VK_RETURN: return KeyEvent::of(Key::enter);
        case VK_ESCAPE: return KeyEvent::of(Key::escape);
        default:
            break;
    }
    const wchar_t character = key.uChar.UnicodeChar;
    if (character >= 0x20 && character < 0x7F) {
        return KeyEvent::of_character(static_cast<char>(character));
    }
    return KeyEvent::of(Key::unknown);
}

}  // namespace

class TerminalImpl {
public:
    [[nodiscard]] static std::unique_ptr<TerminalImpl> create(TerminalError& error) {
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE) {
            error = TerminalError::not_a_terminal;
            return nullptr;
        }

        DWORD input_mode = 0;
        DWORD output_mode = 0;
        if (GetConsoleMode(input, &input_mode) == 0 || GetConsoleMode(output, &output_mode) == 0) {
            error = TerminalError::not_a_terminal;  // A redirected handle is not a console.
            return nullptr;
        }

        auto impl = std::unique_ptr<TerminalImpl>(new TerminalImpl());
        impl->input_ = input;
        impl->output_ = output;
        impl->original_input_mode_ = input_mode;
        impl->original_output_mode_ = output_mode;

        DWORD raw_input = input_mode;
        raw_input &= ~static_cast<DWORD>(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                                         ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT);
        raw_input |= static_cast<DWORD>(ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);
        if (SetConsoleMode(input, raw_input) == 0) {
            error = TerminalError::mode_set_failed;
            return nullptr;  // Destructor restores nothing that was applied.
        }
        impl->input_mode_changed_ = true;

        const DWORD vt_output = output_mode | static_cast<DWORD>(ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        if (SetConsoleMode(output, vt_output) != 0) {
            impl->ansi_ = true;
            impl->output_mode_changed_ = true;
        }

        if (GetConsoleCursorInfo(output, &impl->original_cursor_) != 0) {
            impl->have_cursor_ = true;
            CONSOLE_CURSOR_INFO hidden = impl->original_cursor_;
            hidden.bVisible = FALSE;
            SetConsoleCursorInfo(output, &hidden);
        }

        g_quit_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        SetConsoleCtrlHandler(&console_control_handler, TRUE);
        impl->ctrl_handler_installed_ = true;

        if (impl->ansi_) {
            write_console(output, "\033[2J\033[H");
        } else {
            impl->clear_native();
        }

        error = TerminalError::none;
        return impl;
    }

    TerminalImpl(const TerminalImpl&) = delete;
    TerminalImpl& operator=(const TerminalImpl&) = delete;

    ~TerminalImpl() { restore(); }

    [[nodiscard]] KeyEvent read_event() {
        for (;;) {
            if (InterlockedCompareExchange(&g_quit_requested, 0, 0) != 0) {
                return KeyEvent::of(Key::interrupt);
            }

            HANDLE handles[2] = {g_quit_event, input_};
            const DWORD count = (g_quit_event != nullptr) ? 2 : 1;
            HANDLE* wait_handles = (g_quit_event != nullptr) ? handles : &handles[1];
            const DWORD waited = WaitForMultipleObjects(count, wait_handles, FALSE, INFINITE);
            if (g_quit_event != nullptr && waited == WAIT_OBJECT_0) {
                return KeyEvent::of(Key::interrupt);
            }

            INPUT_RECORD record;
            DWORD read = 0;
            if (ReadConsoleInputW(input_, &record, 1, &read) == 0 || read == 0) {
                return KeyEvent::of(Key::end_of_input);
            }

            if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
                return KeyEvent::of(Key::resize);
            }
            if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown != 0) {
                const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
                const KeyEvent event = translate_key(key);
                if (event.key == Key::unknown && key.uChar.UnicodeChar == 0) {
                    continue;  // Pure modifier press: ignore, keep blocking.
                }
                return event;
            }
            // Mouse/focus/menu records and key-up events carry no game meaning.
        }
    }

    void draw(const Frame& frame) {
        if (ansi_) {
            draw_ansi(frame);
        } else {
            draw_native(frame);
        }
    }

    [[nodiscard]] TerminalSize size() const {
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(output_, &info) != 0) {
            const int columns = info.srWindow.Right - info.srWindow.Left + 1;
            const int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
            if (columns > 0 && rows > 0) {
                return TerminalSize{columns, rows};
            }
        }
        return TerminalSize{};
    }

    [[nodiscard]] bool supports_ansi() const noexcept { return ansi_; }

private:
    TerminalImpl() = default;

    void draw_ansi(const Frame& frame) {
        std::string out = "\033[H";
        for (std::size_t i = 0; i < frame.size(); ++i) {
            out += frame[i];
            out += "\033[K";
            if (i + 1 < frame.size()) {
                out += "\r\n";
            }
        }
        out += "\033[J";
        write_console(output_, out);
    }

    void draw_native(const Frame& frame) {
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(output_, &info) == 0) {
            return;
        }
        const SHORT origin_x = info.srWindow.Left;
        const SHORT origin_y = info.srWindow.Top;
        const SHORT columns = static_cast<SHORT>(info.srWindow.Right - info.srWindow.Left + 1);
        const SHORT rows = static_cast<SHORT>(info.srWindow.Bottom - info.srWindow.Top + 1);

        for (SHORT row = 0; row < rows; ++row) {
            std::string line =
                (static_cast<std::size_t>(row) < frame.size()) ? frame[row] : std::string();
            if (line.size() > static_cast<std::size_t>(columns)) {
                line.resize(static_cast<std::size_t>(columns));
            }

            COORD at{origin_x, static_cast<SHORT>(origin_y + row)};
            SetConsoleCursorPosition(output_, at);
            DWORD written = 0;
            WriteConsoleA(output_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);

            const DWORD remaining = static_cast<DWORD>(columns) - static_cast<DWORD>(line.size());
            if (remaining > 0) {
                COORD tail{static_cast<SHORT>(origin_x + static_cast<SHORT>(line.size())),
                           static_cast<SHORT>(origin_y + row)};
                DWORD filled = 0;
                FillConsoleOutputCharacterA(output_, ' ', remaining, tail, &filled);
            }
        }
    }

    void clear_native() {
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(output_, &info) == 0) {
            return;
        }
        const DWORD cells = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
        const COORD home{0, 0};
        DWORD filled = 0;
        FillConsoleOutputCharacterA(output_, ' ', cells, home, &filled);
        SetConsoleCursorPosition(output_, home);
    }

    void restore() {
        if (ansi_) {
            write_console(output_, "\033[0m");
        }
        if (have_cursor_) {
            SetConsoleCursorInfo(output_, &original_cursor_);
            have_cursor_ = false;
        }
        if (output_mode_changed_) {
            SetConsoleMode(output_, original_output_mode_);
            output_mode_changed_ = false;
        }
        if (input_mode_changed_) {
            SetConsoleMode(input_, original_input_mode_);
            input_mode_changed_ = false;
        }
        if (ctrl_handler_installed_) {
            SetConsoleCtrlHandler(&console_control_handler, FALSE);
            ctrl_handler_installed_ = false;
        }
        if (g_quit_event != nullptr) {
            CloseHandle(g_quit_event);
            g_quit_event = nullptr;
        }
    }

    HANDLE input_ = nullptr;
    HANDLE output_ = nullptr;
    DWORD original_input_mode_ = 0;
    DWORD original_output_mode_ = 0;
    CONSOLE_CURSOR_INFO original_cursor_{};
    bool have_cursor_ = false;
    bool input_mode_changed_ = false;
    bool output_mode_changed_ = false;
    bool ctrl_handler_installed_ = false;
    bool ansi_ = false;
};

// --- TerminalSession forwarding (PIMPL) -------------------------------------

TerminalSession::TerminalSession(std::unique_ptr<TerminalImpl> impl) noexcept
    : impl_(std::move(impl)) {}

TerminalSession::TerminalSession(TerminalSession&&) noexcept = default;
TerminalSession& TerminalSession::operator=(TerminalSession&&) noexcept = default;
TerminalSession::~TerminalSession() = default;

TerminalStartup TerminalSession::create() {
    TerminalError error = TerminalError::none;
    std::unique_ptr<TerminalImpl> impl = TerminalImpl::create(error);

    TerminalStartup startup;
    startup.error = error;
    if (impl) {
        startup.session = TerminalSession(std::move(impl));
    }
    return startup;
}

KeyEvent TerminalSession::read_event() { return impl_->read_event(); }
void TerminalSession::draw(const Frame& frame) { impl_->draw(frame); }
TerminalSize TerminalSession::size() const { return impl_->size(); }
bool TerminalSession::supports_ansi() const noexcept { return impl_->supports_ansi(); }

// --- Capability queries ------------------------------------------------------

bool stdin_is_interactive() {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

bool stdout_is_interactive() {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) != 0;
}

bool interactive_display_supported(bool /*env_ansi*/) {
    // TERM is a POSIX concept; on Windows the backend negotiates VT vs native
    // output itself, so a console on both standard streams is sufficient.
    return stdin_is_interactive() && stdout_is_interactive();
}

}  // namespace nam::console
