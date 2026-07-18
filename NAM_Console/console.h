#pragma once

#include <iostream>
#include <string>
#include <map>

/*
 * Key codes on Linux terminals:
 *   ESC is 27 (0x1B).
 *   Arrow keys are transmitted as an escape sequence: ESC '[' 'A'/'B'/'C'/'D'.
 * We map arrow keys to their final byte so the existing dispatcher can reuse them.
 */
#define KEY_ESC   27
#define KEY_UP    'A'
#define KEY_DOWN  'B'
#define KEY_RIGHT 'C'
#define KEY_LEFT  'D'

enum class Keys {
	esc   = KEY_ESC,
	up    = KEY_UP,
	down  = KEY_DOWN,
	right = KEY_RIGHT,
	left  = KEY_LEFT
};

/* Terminal setup / teardown for raw, non-blocking input. */
void console_init();
void console_restore();

/* Non-blocking check for a pending byte on stdin. */
bool kb_hit();

/* Read a single byte from stdin (blocks until one is available). */
int kb_getch();

/* Move the cursor to (x, y), 0-indexed from the top-left. */
void gotoxy(short int x, short int y);

/* Show or hide the terminal cursor. */
void ShowConsoleCursor(bool showFlag);
