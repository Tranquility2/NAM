#include <iostream>
#include <string>
#include <vector>
#include <sstream>

#include "map.h"
#include "console.h"

#include <conio.h>

using namespace std;

bool move_actor(MapData *map_data, Direction direction, string *message)
{
	Location new_location = (*map_data).move_actor(direction);
	*message = new_location.message;

	return new_location.reachable;
}

bool move(MapData *map_data, int ch, string *message)
{
	switch (ch) {
		case (int) Keys::up: return move_actor(map_data, Direction::up, message);
		case (int) Keys::down: return move_actor(map_data, Direction::down, message);
		case (int) Keys::left: return move_actor(map_data, Direction::left, message);
		case (int) Keys::right: return move_actor(map_data, Direction::right, message);
		default: return false;
	}
}

string display(MapData *map_data, vector<const char*> *keys, string *message)
{
	ostringstream out;
	Coordinates current_location = (*map_data).actor_location((*map_data).actor_cell_number());

	gotoxy(0, 0);
	out.str("");
	out.clear();
	
	out << (*map_data).printable_map() << endl;
	out << *message << endl << endl << endl;
	out << "DEBUG:" << (*map_data).columns() << 'x' << (*map_data).rows();
	out << "(@" << (*map_data).actor_cell_number() << ')';
	out << '[' << current_location.X << 'x' << current_location.Y << ']' << endl;
	/* deplay last keys */
	for (auto i = (*keys).begin(); i != (*keys).end(); ++i)
	{
		out << *i << "->";
	}

	return out.str();
}

int main() 
{
	//const string file_name = "temp.map";
	MapData map_data("");
	vector<const char*> keys;
	string message;
	bool game_loop_flag = TRUE;
	
	ShowConsoleCursor(FALSE);

	///* main game loop */
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
				if (move(&map_data, ch, &message))
				{
					keys.push_back(get_key_name(ch)); // Save key
				}
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
 