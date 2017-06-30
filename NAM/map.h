#pragma once

#include <iostream>
#include <string>
#include <fstream>

using namespace std;

enum class Direction {up, down, left, right};

typedef struct _Coordinates {
	short int X;
	short int Y;
} Coordinates, *PCoordinates;

class MapData
{
public:
	MapData(const char * file_name);
	~MapData();
	
	char **map_data() { return _map_data; };
	int rows() { return _rows; };
	int columns() { return _columns; };
	string printable_map();
	bool move_actor(Direction direction);
	int actor_cell_number() { return _actor_cell_number; };
	Coordinates actor_location(int cel_number);

private:
	char **_map_data; 
	int _rows;
	int _columns;
	int _actor_cell_number;
	
	char *load_binary_file(const char *file_name);
	void load_ascii_map_file(const char *file_name);
	void zero_map_fill();
	bool is_map_barrier_wall(Coordinates coordinates);
};
