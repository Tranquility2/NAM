#include <iostream>
#include <string>
#include <vector>

#include "map.h"
#include "console.h"

#include <conio.h>

using namespace std;

#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77
#define KEY_ESC 27

const char *get_key_name(int ch)
{
	switch (ch) {
	case KEY_UP: return "Up";
	case KEY_DOWN: return "Down";
	case KEY_LEFT: return "Left";
	case KEY_RIGHT:	return "Right";
	default: return "Other";
	}
}

int main() 
{
	ShowConsoleCursor(FALSE);

	const char *file_name = "temp.map";
	MapData map_data(file_name);
	vector<const char*> keys;
	/* main game loop */
	bool loop_flag = TRUE;
	while (loop_flag)
	{
		ClearScreenWin();
		/* deplay last keys */
		for (auto i = keys.begin(); i != keys.end(); ++i)
		{
			cout << *i << "->";
		}

		cout << endl;

		cout << map_data.columns() << 'x' << map_data.rows() << endl;
		cout << map_data.printable_map() << endl;

		int ch = _getch();
		/* For the arrow keys, it returns 224 first followed by 72 (up), 80 (down), 75 (left) and 77 (right). 
		If the num-pad arrow keys (with NumLock off) are pressed, getch () returns 0 first instead of 224. */
		if (ch == 0 || ch == 224)
		{
			ch = _getch();
			keys.push_back(get_key_name(ch)); // Save key

			switch (ch) {
			case KEY_UP: 
				break;
			case KEY_DOWN: 
				break;
			case KEY_LEFT: 
				break;
			case KEY_RIGHT:	
				break;
			default: break;
			}
		}
		else
		{
			keys.push_back(get_key_name(ch)); // Save key

			switch (ch)
			{
			case KEY_ESC:
				cout << "Good bye..." << endl;
				loop_flag = FALSE;
				break;
			default: break;
			}
		}

		gotoxy(5, 5);

		Sleep(300);
	}
		
	return 0;
}
 