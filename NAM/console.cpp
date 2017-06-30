#include "console.h"

/* http://www.cplusplus.com/forum/articles/10515/ */
/* http://faq.cprogramming.com/cgi-bin/smartfaq.cgi?answer=1031963460&id=1043284385 */

void ClearScreenWin()
{
	DWORD n;                         /* Number of characters written */
	DWORD size;                      /* number of visible characters */
	COORD coord = { 0 };               /* Top left screen position */
	CONSOLE_SCREEN_BUFFER_INFO csbi;

	/* Get a handle to the console */
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

	GetConsoleScreenBufferInfo(h, &csbi);

	/* Find the number of characters to overwrite */
	size = csbi.dwSize.X * csbi.dwSize.Y;

	/* Overwrite the screen buffer with whitespace */
	FillConsoleOutputCharacter(h, TEXT(' '), size, coord, &n);
	GetConsoleScreenBufferInfo(h, &csbi);
	FillConsoleOutputAttribute(h, csbi.wAttributes, size, coord, &n);

	/* Reset the cursor to the top left position */
	SetConsoleCursorPosition(h, coord);
}

void SetScreenPosition()
{
	HWND consoleWindow = GetConsoleWindow();

	SetWindowPos(consoleWindow, 0, 500, 500, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void gotoxy(short int x, short int y)
{
	static HANDLE h = NULL;
	COORD c = { x, y };

	if (!h)
		h = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleCursorPosition(h, c);
}

void ShowConsoleCursor(bool showFlag)
{
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_CURSOR_INFO     cursorInfo;

	GetConsoleCursorInfo(out, &cursorInfo);
	cursorInfo.bVisible = showFlag; // set the cursor visibility
	SetConsoleCursorInfo(out, &cursorInfo);
}

const char *get_key_name(int ch)
{
	switch (ch) {
		case (int) Keys::up: return "Up";
		case (int) Keys::down: return "Down";
		case (int) Keys::left: return "Left";
		case (int) Keys::right:	return "Right";
		default: return "Other";
	}
}
