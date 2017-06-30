#include <iostream>
#include <string>
#include <vector>

#include "map.h"
#include "console.h"

#include <conio.h>

using namespace std;

void move_actor(MapData *map_data, Direction direction)
{
	if ((*map_data).move_actor(direction))
	{
		cout << "Well this looks like a completely new terrain :)" << endl;
	}
	else
	{
		cout << "Can't go there :(" << endl;
	}
}

void move(MapData *map_data, int ch)
{
	switch (ch) {
		case (int) Keys::up: move_actor(map_data, Direction::up); break;
		case (int) Keys::down: move_actor(map_data, Direction::down); break;
		case (int) Keys::left: move_actor(map_data, Direction::left);	break;
		case (int) Keys::right: move_actor(map_data, Direction::right); break;
		default: break;
	}
}

int main() 
{
	ShowConsoleCursor(FALSE);

	const char *file_name = "temp.map";
	MapData map_data(file_name);
	vector<const char*> keys;
	/* main game loop */
	bool game_loop_flag = TRUE;
	while (game_loop_flag)
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
			move(&map_data, ch);
		}
		else
		{
			keys.push_back(get_key_name(ch)); // Save key

			switch (ch)
			{
			case (int) Keys::esc:
				cout << "Good bye..." << endl;
				game_loop_flag = FALSE;
				break;
			default: break;
			}
		}

		Sleep(300);
	}
		
	return 0;
}
 