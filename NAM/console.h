#pragma once

#include <iostream>
#include <string>

#include <windows.h>

using namespace std;

#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77
#define KEY_ESC 27

enum class Keys {
	esc = KEY_ESC,
	up = KEY_UP,
	left = KEY_LEFT,
	right = KEY_RIGHT,
	down = KEY_DOWN
};

void ClearScreenWin();
void SetScreenPosition();
void gotoxy(short int x, short int y);
void ShowConsoleCursor(bool showFlag);
const char *get_key_name(int ch);
