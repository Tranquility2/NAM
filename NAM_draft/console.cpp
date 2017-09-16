#include "console.h"

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
