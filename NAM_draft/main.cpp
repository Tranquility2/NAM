#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <map>

#include <conio.h>

#include "map.h"
#include "console.h"

using namespace std;

map<Keys, const char*> keyLetterMap = {
	{ Keys::up,  "U" },
	{ Keys::down, "D" },
	{ Keys::left, "L" },
	{ Keys::right, "R" }
};

map<Keys, Direction> keys2directionMap = {
	{ Keys::up, Direction::up },
	{ Keys::down, Direction::down },
	{ Keys::left, Direction::left },
	{ Keys::right, Direction::right }
};

string display(MapData *map_data, vector<const char*> *keys, string *message)
{
	ostringstream out;
	Coordinates current_location = map_data->actor_coordinates(map_data->actor_cell_number());

	gotoxy(0, 0);
	out.str("");
	out.clear();
	
	out << (*map_data).printable_map() << endl;
	out << *message << endl << endl << endl;
	out << "DEBUG:" << map_data->columns() << 'x' << (*map_data).rows();
	out << "(@" << map_data->actor_cell_number() << ')';
	out << '[' << current_location.X << 'x' << current_location.Y << ']';
	out << "->" << map_data->terrain_on(current_location) << "<-"<< endl;
	/* deplay last keys */
	for (auto i = keys->begin(); i != keys->end(); ++i)
	{
		out << *i;
	}

	return out.str();
}

int main() 
{
	MapData map_data("");
	vector<const char*> keys;
	string message;
	bool game_loop_flag = TRUE;
	
	ShowConsoleCursor(FALSE);

	///* main game loop */
	while (game_loop_flag)
	{
		if (_kbhit()) {
			int ch = _getch();
			/* For the arrow keys, it returns 224 first followed by 72 (up), 80 (down), 75 (left) and 77 (right).
			If the num-pad arrow keys (with NumLock off) are pressed, getch () returns 0 first instead of 224. */
			if (ch == 0 || ch == 224)
			{
				ch = _getch();
				Location new_location = map_data.move_actor(keys2directionMap[Keys(ch)]);
				message = new_location.message;
				if (new_location.reachable)
				{
					keys.push_back(keyLetterMap[Keys(ch)]); // Save key
				}
			}
			else
			{
				if (ch == (int)Keys::esc)
				{
					message = "Good bye...";
					game_loop_flag = FALSE;
					break;
				}
			}
		}

		cout << display(&map_data, &keys, &message);
	}

	return 0;
}
 