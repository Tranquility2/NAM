#include <iostream>
#include <string>
#include <vector>
#include <sstream>

#include "map.h"
#include "console.h"

#include <conio.h>

using namespace std;

void move_actor(MapData *map_data, Direction direction, string *message)
{
	if ((*map_data).move_actor(direction))
	{
		*message = "Well this looks like a completely new terrain :)";
	}
	else
	{
		*message = "Can't go there :(                               ";
	}
}

void move(MapData *map_data, int ch, string *message)
{
	switch (ch) {
		case (int) Keys::up: move_actor(map_data, Direction::up, message); break;
		case (int) Keys::down: move_actor(map_data, Direction::down, message); break;
		case (int) Keys::left: move_actor(map_data, Direction::left, message);	break;
		case (int) Keys::right: move_actor(map_data, Direction::right, message); break;
		default: break;
	}
}

string display(MapData *map_data, vector<const char*> *keys, string *message)
{
	ostringstream out;

	gotoxy(0, 0);
	out.str("");
	out.clear();

	out << (*map_data).printable_map() << endl;
	out << *message << endl;
	out << "DEBUG:" << (*map_data).columns() << 'x' << (*map_data).rows();
	out << "(@" << (*map_data).player_cel_number() << ')' << endl;
	/* deplay last keys */
	for (auto i = (*keys).begin(); i != (*keys).end(); ++i)
	{
		out << *i << "->";
	}

	return out.str();
}

int main() 
{
	ShowConsoleCursor(FALSE);

	const char *file_name = "temp.map";
	MapData map_data(file_name);
	vector<const char*> keys;
	string message;
	
	/* main game loop */
	bool game_loop_flag = TRUE;
	while (game_loop_flag)
	{
		cout << display(&map_data, &keys, &message);

		if (_kbhit()) {
			int ch = _getch();
			/* For the arrow keys, it returns 224 first followed by 72 (up), 80 (down), 75 (left) and 77 (right).
			If the num-pad arrow keys (with NumLock off) are pressed, getch () returns 0 first instead of 224. */
			if (ch == 0 || ch == 224)
			{
				ch = _getch();
				keys.push_back(get_key_name(ch)); // Save key
				move(&map_data, ch, &message);
			}
			else
			{
				keys.push_back(get_key_name(ch)); // Save key

				switch (ch)
				{
				case (int)Keys::esc:
					message = "Good bye...";
					game_loop_flag = FALSE;
					break;
				default: break;
				}
			}
		}
	}
		
	return 0;
}
 