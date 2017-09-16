#pragma once

#include <string>
#include <map>

#include "map.h"

const string map1 = R"(30 10
=============================
|...@...~~~~~......@.....xxx|
|.@@@@..~~~~~......@@....xxx|
|.@@@@..~~~~......@@......xx|
|..@@...~~~.......@@@......x|
|...........................|
|x......x@@x....^^^.......~~|
|xxx....x@@x...^^^.......~~~|
|xxxxx..........^^^......~~~|
=============================
)";

map<string, string> messageMap ={
	{ "new_terrain",          "Well this looks like a completely new terrain :)     " },
	{ "unreachable_location", "Can't go there :(                                    " },
	{ "water",				  "Good thing you know how to swim -.-                  " },
	{ "mountain",			  "It took a while to get here, but the view is amazing!" },
	{ "hill",			      "Over the hills a far away...                         " },
	{ "fields",			      "Lots of green in those open fields :D                " }
};

map<char, Location> terrainMap = {
	{ '.', { true, messageMap["new_terrain"] } },
	{ '@', { true, messageMap["mountain"] }    },
	{ '~', { true, messageMap["water"] }       },
	{ 'x', { true, messageMap["fields"] }      },
	{ '^', { true, messageMap["hill"] }        }
};

map<Direction, Coordinates> moveAxis= {
	{ Direction::up,    { 0, -1 }  },
	{ Direction::down,  { 0, 1 } },
	{ Direction::right, { 1, 0 }  },
	{ Direction::left,  { -1, 0 } }
};
