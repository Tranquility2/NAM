#pragma once

#include <iostream>
#include <fstream>

using namespace std;

enum class Direction {up, down, left, right};

class MapData
{
public:
	MapData(const char * file_name);
	~MapData();
	
	char **map_data() { return _map_data; };
	int rows() { return _rows; };
	int columns() { return _columns; };
	char *printable_map();
	bool move_actor(Direction direction);
private:
	char **_map_data; 
	int _rows;
	int _columns;
	int _player_cel_number;
	
	char *load_binary_file(const char *file_name);
	void load_ascii_map_file(const char *file_name);
	void zero_map_fill();
	bool set_player_at_location(int row, int column);
};
