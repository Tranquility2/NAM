#pragma once

#include <iostream>
#include <fstream>

using namespace std;

class MapData
{
public:
	MapData(const char * file_name);
	~MapData();
	
	char **map_data() { return _map_data; };
	int rows() { return _rows; };
	int columns() { return _columns; };

	char *printable_map();
private:
	char **_map_data;
	int _rows;
	int _columns;
	
	char *load_binary_file(const char *file_name);
	void load_ascii_map_file(const char *file_name);
	void zero_map_fill();
};
