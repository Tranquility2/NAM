#pragma once

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

using namespace std;

enum class Direction {up, down, left, right};

typedef struct _Coordinates {
	short int X;
	short int Y;

	_Coordinates& operator+=(const _Coordinates& right)
	{
		this->X += right.X;
		this->Y += right.Y;

		return *this;
	}
} Coordinates, *PCoordinates;

typedef struct _Location {
	bool reachable;
	string message;
} Location, *PLocation;

class MapData
{
public:
	MapData(const string file_name);
	~MapData();
	
	char **map_data() { return _map_data; };
	int rows() { return _rows; };
	int columns() { return _columns; };
	int actor_cell_number() { return _actor_cell_number; };
	int actor_cell_number(Coordinates coordinates) { return coordinates.Y * _columns + coordinates.X;};
	const char terrain_on(Coordinates coordinates);
	Location move_actor(Direction direction);
	Coordinates actor_coordinates(int cel_number);
	string printable_map();

private:
	char **_map_data; 
	int _rows;
	int _columns;
	int _actor_cell_number;
	
	char *load_binary_file(const string file_name);
	void load_ascii_map_file(const string file_name);
	void load_ascii_map_stream(string map);
	void zero_map_fill();
};
