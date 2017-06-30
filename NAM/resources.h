#pragma once

#include <string>
#include <map>

const string map1 = R"(30 10
XXXXXXXXXXXXXXXXXXXXXXXXXXXXX
X...@...~~~~~......@.....xxxX
X.@@@@..~~~~~......@@....xxxX
X.@@@@..~~~~......@@......xxX
X..@@...~~~.......@@@......xX
X...........................X
Xx......x@@x....^^^.......~~X
Xxxx....x@@x...^^^.......~~~X
Xxxxxx..........^^^......~~~X
XXXXXXXXXXXXXXXXXXXXXXXXXXXXX
)";

map<string, string> messageMap ={
	{ "new_terrain",          "Well this looks like a completely new terrain :)" },
	{ "unreachable_location", "Can't go there :(                               " }
};