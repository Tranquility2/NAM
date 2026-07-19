#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "input.h"
#include "input_decoder.h"
#include "terminal.h"

namespace nam::console {

namespace {

// --- Signal coordination ----------------------------------------------------
//
// Handlers do only async-signal-safe work: set a sig_atomic flag and nudge a
// self-pipe so a blocked poll() wakes up. All real terminal restoration happens
// later on the normal control-flow path (TerminalImpl::restore), never here.
volatile sig_atomic_t g_resize_pending = 0;
volatile sig_atomic_t g_quit_pending = 0;
volatile sig_atomic_t g_wake_write_fd = -1;

extern "C" void handle_signal(int signal_number) {
    if (signal_number == SIGWINCH) {
        g_resize_pending = 1;
    } else {
        g_quit_pending = 1;
    }
    const int fd = g_wake_write_fd;
    if (fd >= 0) {
        const unsigned char byte = 1;
        const ssize_t written = ::write(fd, &byte, 1);  // async-signal-safe
        static_cast<void>(written);
    }
}

void write_all(int fd, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;  // Give up silently; teardown must never throw.
        }
        offset += static_cast<std::size_t>(written);
    }
}

bool make_nonblocking_cloexec(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return false;
    }
    const int fd_flags = ::fcntl(fd, F_GETFD, 0);
    return fd_flags >= 0 && ::fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC) >= 0;
}

// Pulls bytes from stdin for the decoder. The first byte is read directly (the
// caller only invokes the decoder once poll() reports stdin readable); waiting
// for continuation bytes uses a deadline-bounded poll so a lone Escape is
// distinguished from a slowly delivered sequence, and EINTR never aborts it.
class FdByteReader final : public ByteReader {
public:
    explicit FdByteReader(int fd) noexcept : fd_(fd) {}

    ByteResult read_blocking() override { return read_one(); }

    ByteResult read_within(std::chrono::milliseconds timeout) override {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            const auto now = std::chrono::steady_clock::now();
            int wait_ms = 0;
            if (now < deadline) {
                wait_ms = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            }
            struct pollfd descriptor {};
            descriptor.fd = fd_;
            descriptor.events = POLLIN;
            const int ready = ::poll(&descriptor, 1, wait_ms);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;  // Interrupted by a signal; keep the same deadline.
                }
                return ByteResult::eof();
            }
            if (ready == 0) {
                return ByteResult::timed_out();
            }
            if (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) {
                return read_one();
            }
            return ByteResult::timed_out();
        }
    }

private:
    ByteResult read_one() {
        for (;;) {
            unsigned char byte = 0;
            const ssize_t count = ::read(fd_, &byte, 1);
            if (count == 1) {
                return ByteResult::ok(byte);
            }
            if (count == 0) {
                return ByteResult::eof();
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return ByteResult::timed_out();
            }
            return ByteResult::eof();
        }
    }

    int fd_;
};

}  // namespace

// Platform-private implementation behind TerminalSession's PIMPL. Its destructor
// is the single restoration point; TerminalSession simply owns one of these.
class TerminalImpl {
public:
    [[nodiscard]] static std::unique_ptr<TerminalImpl> create(TerminalError& error) {
        if (!stdin_is_interactive() || !stdout_is_interactive()) {
            error = TerminalError::not_a_terminal;
            return nullptr;
        }

        auto impl = std::unique_ptr<TerminalImpl>(new TerminalImpl());

        if (::tcgetattr(STDIN_FILENO, &impl->original_) != 0) {
            error = TerminalError::mode_query_failed;
            return nullptr;  // Never proceed with uninitialized termios.
        }

        struct termios raw = impl->original_;
        raw.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
        raw.c_oflag &= static_cast<tcflag_t>(~OPOST);
        raw.c_cflag |= static_cast<tcflag_t>(CS8);
        // ICANON/ECHO/IEXTEN off for raw keys; ISIG stays on so Ctrl-C, Ctrl-Z,
        // etc. still raise signals that we handle for a clean shutdown.
        raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN));
        raw.c_cc[VMIN] = 1;   // Blocking reads: idle CPU is effectively zero.
        raw.c_cc[VTIME] = 0;

        if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            error = TerminalError::mode_set_failed;
            return nullptr;
        }
        impl->raw_active_ = true;

        impl->install_wake_pipe();
        impl->install_signal_handlers();

        // Alternate screen + hidden cursor for a clean, self-contained surface.
        write_all(STDOUT_FILENO, "\033[?1049h\033[?25l\033[2J\033[H");
        impl->alt_screen_ = true;

        error = TerminalError::none;
        return impl;
    }

    TerminalImpl(const TerminalImpl&) = delete;
    TerminalImpl& operator=(const TerminalImpl&) = delete;

    ~TerminalImpl() { restore(); }

    [[nodiscard]] KeyEvent read_event() {
        for (;;) {
            if (g_resize_pending) {
                g_resize_pending = 0;
                drain_wake();
                return KeyEvent::of(Key::resize);
            }
            if (g_quit_pending) {
                drain_wake();
                return KeyEvent::of(Key::interrupt);
            }

            struct pollfd fds[2] = {};
            fds[0].fd = STDIN_FILENO;
            fds[0].events = POLLIN;
            fds[1].fd = wake_read_fd_;
            fds[1].events = POLLIN;

            const int ready = ::poll(fds, 2, -1);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;  // A signal fired; loop re-checks the flags.
                }
                return KeyEvent::of(Key::end_of_input);
            }
            if (fds[1].revents != 0) {
                drain_wake();
                continue;
            }
            if (fds[0].revents != 0) {
                FdByteReader reader(STDIN_FILENO);
                return decoder_.decode(reader);
            }
        }
    }

    void draw(const Frame& frame) {
        std::size_t total = 16;
        for (const std::string& line : frame) {
            total += line.size() + 8;
        }

        std::string out;
        out.reserve(total);
        out += "\033[H";  // Home; absolute positioning avoids scroll drift.
        for (std::size_t i = 0; i < frame.size(); ++i) {
            out += frame[i];
            out += "\033[K";  // Erase stale tail of this row.
            if (i + 1 < frame.size()) {
                out += "\r\n";
            }
        }
        out += "\033[J";  // Erase any rows left over from a taller prior frame.
        write_all(STDOUT_FILENO, out);
    }

    [[nodiscard]] TerminalSize size() const {
        struct winsize window {};
        if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0 && window.ws_col > 0 &&
            window.ws_row > 0) {
            return TerminalSize{static_cast<int>(window.ws_col), static_cast<int>(window.ws_row)};
        }
        return TerminalSize{};
    }

    [[nodiscard]] bool supports_ansi() const noexcept { return true; }

private:
    TerminalImpl() = default;

    void install_wake_pipe() {
        int fds[2] = {-1, -1};
        if (::pipe(fds) != 0) {
            return;  // Degrade gracefully: signals still set flags, poll uses EINTR.
        }
        if (!make_nonblocking_cloexec(fds[0]) || !make_nonblocking_cloexec(fds[1])) {
            ::close(fds[0]);
            ::close(fds[1]);
            return;
        }
        wake_read_fd_ = fds[0];
        wake_write_fd_ = fds[1];
        g_wake_write_fd = wake_write_fd_;
    }

    void install_one(int signal_number, struct sigaction& saved) {
        struct sigaction action {};
        action.sa_handler = &handle_signal;
        sigfillset(&action.sa_mask);
        action.sa_flags = 0;  // No SA_RESTART: let poll() return EINTR.
        ::sigaction(signal_number, &action, &saved);
    }

    void install_signal_handlers() {
        install_one(SIGWINCH, old_winch_);
        install_one(SIGINT, old_int_);
        install_one(SIGTERM, old_term_);
        install_one(SIGHUP, old_hup_);
        signals_installed_ = true;
    }

    void drain_wake() {
        if (wake_read_fd_ < 0) {
            return;
        }
        unsigned char buffer[64];
        while (::read(wake_read_fd_, buffer, sizeof buffer) > 0) {
        }
    }

    void restore() {
        if (alt_screen_) {
            // Show cursor, reset attributes, leave the alternate screen.
            write_all(STDOUT_FILENO, "\033[?25h\033[0m\033[?1049l");
            alt_screen_ = false;
        }
        if (signals_installed_) {
            ::sigaction(SIGWINCH, &old_winch_, nullptr);
            ::sigaction(SIGINT, &old_int_, nullptr);
            ::sigaction(SIGTERM, &old_term_, nullptr);
            ::sigaction(SIGHUP, &old_hup_, nullptr);
            signals_installed_ = false;
        }
        g_wake_write_fd = -1;
        if (wake_write_fd_ >= 0) {
            ::close(wake_write_fd_);
            wake_write_fd_ = -1;
        }
        if (wake_read_fd_ >= 0) {
            ::close(wake_read_fd_);
            wake_read_fd_ = -1;
        }
        if (raw_active_) {
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
            raw_active_ = false;
        }
    }

    struct termios original_ {};
    bool raw_active_ = false;
    bool alt_screen_ = false;
    bool signals_installed_ = false;
    int wake_read_fd_ = -1;
    int wake_write_fd_ = -1;
    struct sigaction old_winch_ {};
    struct sigaction old_int_ {};
    struct sigaction old_term_ {};
    struct sigaction old_hup_ {};
    InputDecoder decoder_{};
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

bool stdin_is_interactive() { return ::isatty(STDIN_FILENO) == 1; }
bool stdout_is_interactive() { return ::isatty(STDOUT_FILENO) == 1; }

bool interactive_display_supported(bool env_ansi) {
    return stdin_is_interactive() && stdout_is_interactive() && env_ansi;
}

}  // namespace nam::console
