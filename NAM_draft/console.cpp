#include "console.h"

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>

static struct termios g_original_termios;
static bool g_termios_saved = false;

static void console_atexit()
{
	console_restore();
}

void console_init()
{
	if (tcgetattr(STDIN_FILENO, &g_original_termios) == 0)
	{
		g_termios_saved = true;
		std::atexit(console_atexit);
	}

	struct termios raw = g_original_termios;
	/* Disable canonical mode and echo so we get keys as they are pressed. */
	raw.c_lflag &= ~(ICANON | ECHO);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

void console_restore()
{
	if (g_termios_saved)
	{
		tcsetattr(STDIN_FILENO, TCSANOW, &g_original_termios);
	}
	ShowConsoleCursor(true);
}

bool kb_hit()
{
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);

	struct timeval tv = { 0, 0 };
	return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

int kb_getch()
{
	unsigned char c;
	for (;;)
	{
		ssize_t n = read(STDIN_FILENO, &c, 1);
		if (n == 1) return c;
		if (n == 0) return -1;                 /* EOF (e.g. piped stdin closed) */
		if (n < 0 && errno != EINTR) return -1;
	}
}

void gotoxy(short int x, short int y)
{
	/* ANSI cursor positioning is 1-indexed. */
	std::printf("\033[%d;%dH", y + 1, x + 1);
	std::fflush(stdout);
}

void ShowConsoleCursor(bool showFlag)
{
	std::printf("%s", showFlag ? "\033[?25h" : "\033[?25l");
	std::fflush(stdout);
}
