#include "terminal.h"

namespace nam::console {

std::string_view describe(TerminalError error) noexcept {
    switch (error) {
        case TerminalError::none:
            return "no error";
        case TerminalError::not_a_terminal:
            return "standard input/output is not an interactive terminal";
        case TerminalError::mode_query_failed:
            return "could not read the current terminal mode";
        case TerminalError::mode_set_failed:
            return "could not switch the terminal into interactive mode";
    }
    return "unknown terminal error";
}

}  // namespace nam::console
