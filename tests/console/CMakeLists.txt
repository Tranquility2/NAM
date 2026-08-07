# Console behaviour. nam_console_lib is the whole frontend minus main(), so the
# app loops run here against string streams and a scripted input reader rather
# than a real terminal.
nam_add_test_executable(nam_console_tests
    LINK nam_console_lib
    LABELS "console;unit"
    SOURCES
        settings.cpp
        input_decoder.cpp
        renderer.cpp
        messages.cpp
        app_state.cpp
        console_app.cpp
        journal.cpp
        replay.cpp
        expedition_report.cpp)
