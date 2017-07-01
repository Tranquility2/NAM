#include "map.h"
#include "resources.h"

using namespace std;

MapData::MapData(const string file_name)
{
	/* init */
	_columns = _rows = 0;
	_map_data = NULL;

	if (!file_name.empty())
	{
		load_ascii_map_file(file_name);
	}
	else
	{
		load_ascii_map_stream(map1);
	}
	
	/* TODO: player startup location should be refactored: 
		1. player will start in the middle for now
		2. consider other map size
		3. check if starting location is valid
		4. try and randomize the location */
	_actor_cell_number = (_rows * _columns / 2) - (_columns / 2);
}

MapData::~MapData() 
{ 
	/* clean map_data */
	for (int i = 0; i < _rows; ++i)
		delete _map_data[i];

	delete _map_data;
}

/* read binary data from file */
char *MapData::load_binary_file(const string file_name) {
	long  size;
	char *memblock;

	ifstream infile;
	infile.open(file_name, ios::in | ios::binary);

	if (infile.is_open())
	{
		size = (long)infile.tellg();
		memblock = new char[size];
		infile.seekg(0, ios::beg);
		infile.read(memblock, size);
		infile.close();

		cout << "file loaded" << endl;
		return memblock;
	}

	else 
	{
		cout << "Error opening file" << endl;
		return NULL;
	}
}

/* read ascii map data from file */
void MapData::load_ascii_map_file(const string file_name)
{
	ifstream infile;
	infile.open(file_name, ios::in);

	if (infile.is_open())
	{
		ostringstream ss;
		ss << infile.rdbuf();
		string buffer = ss.str();
		load_ascii_map_stream(buffer);
		infile.close();
	}
	else 
	{
		cout << "Error opening file" << endl;
	}
}


void MapData::load_ascii_map_stream(string map)
{
	std::istringstream buffer (map);

	buffer >> _columns;
	buffer >> _rows;
	buffer.get(); // newlines char
	/* init 2d array */
	_map_data = new char*[_rows];
	for (int i = 0; i < _rows; ++i)
		_map_data[i] = new char[_columns];

	zero_map_fill();
	/* get map data */
	for (int i = 0; i < _rows; i++)
	{
		for (int j = 0; j < _columns; j++)
		{
			char ch = (char)buffer.get();
			//cout << '[' << i << ',' << j << ']' << ch << endl;
			_map_data[i][j] = ch;
		}
	}
}


void MapData::zero_map_fill()
{
	for (int i = 0; i < _rows; i++)
	{
		for (int j = 0; j < _columns; j++)
		{
			_map_data[i][j] = { 0 };
		}
	}
}

Location MapData::move_actor(Direction direction)
{
	int new_actor_cell_number;
	Coordinates actor_c = actor_coordinates(_actor_cell_number);
	
	actor_c += moveAxis[direction];
	new_actor_cell_number = actor_cell_number(actor_c);

	/* some senity checks are needed */
	if (actor_c.X == 0 || actor_c.X == _columns - 2 || actor_c.Y == 0 || actor_c.Y == rows() -1)
	{
		return {false, messageMap["unreachable_location"]};
	}
	
	_actor_cell_number = new_actor_cell_number;

	return terrainMap[terrain_on(actor_c)];
}

Coordinates MapData::actor_coordinates(int cel_number)
{
	short int x, y;

	y = cel_number / _columns;
	x = cel_number - y * _columns;

	return { x , y };
}

const char MapData::terrain_on(Coordinates coordinates)
{
	char terrain = _map_data[coordinates.Y][coordinates.X];

	return terrain;
}

string MapData::printable_map()
{
	/* total size + room for newlines char */
	char *buff = new char[_rows * _columns];
	int buff_pos = 0;
	string result;

	for (int i = 0; i < _rows; i++)
	{
		for (int j = 0; j < _columns; j++)
		{
			if (buff_pos == _actor_cell_number)
			{
				buff[buff_pos++] = (char) '+';
			}
			else
			{
				buff[buff_pos++] =_map_data[i][j];
			}
		}
	}

	buff[buff_pos++] = { 0 };
	result = buff;

	return result;
}
