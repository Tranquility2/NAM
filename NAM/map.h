#pragma once

#include <iostream>
#include <fstream>

using namespace std;

class MapData
{
public:
	MapData(const char * file_name);
	~MapData();
	
	char *map_data() { return _map_data; };
	int rows() { return _rows; };
	int columns() { return _columns; };
private:
	char *_map_data;
	int _rows;
	int _columns;
	
	char *load_file(const char *file_name);
	void load_map(const char *file_name);
};
