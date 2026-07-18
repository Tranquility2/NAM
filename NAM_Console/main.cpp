#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <map>

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
	bool game_loop_flag = true;

	console_init();
	ShowConsoleCursor(false);
	/* Clear screen once so gotoxy(0, 0) redraws produce a clean frame. */
	cout << "\033[2J" << flush;

	///* main game loop */
	while (game_loop_flag)
	{
		if (kb_hit()) {
			int ch = kb_getch();
			if (ch < 0) {          /* EOF on stdin */
				message = "Good bye...";
				game_loop_flag = false;
				break;
			}
			/*
			 * On Linux, arrow keys arrive as an escape sequence: ESC '[' 'A'/'B'/'C'/'D'.
			 * A bare ESC (no follow-up bytes) means the user wants to quit.
			 */
			if (ch == (int)Keys::esc)
			{
				if (kb_hit() && kb_getch() == '[')
				{
					if (kb_hit())
					{
						ch = kb_getch();
						Location new_location = map_data.move_actor(keys2directionMap[Keys(ch)]);
						message = new_location.message;
						if (new_location.reachable)
						{
							keys.push_back(keyLetterMap[Keys(ch)]); // Save key
						}
					}
				}
				else
				{
					message = "Good bye...";
					game_loop_flag = false;
					break;
				}
			}
		}

		cout << display(&map_data, &keys, &message) << flush;
	}

	console_restore();
	cout << endl << message << endl;

	return 0;
}
 