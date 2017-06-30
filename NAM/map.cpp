#include "map.h"

using namespace std;

MapData::MapData(const char *file_name)
{
	/* init */
	_columns = _rows = 0;
	_map_data = NULL;
	load_ascii_map_file(file_name);

	/* TODO: player startup location should be refactored: 
		1. player will start in the middle for now
		2. consider other map size
		3. check if starting location is valid
		4. try and randomize the location */
	_player_cel_number = (_rows * _columns / 2) - (_columns / 2); 
}

MapData::~MapData() 
{ 
	/* clean map_data */
	for (int i = 0; i < _rows; ++i)
		delete _map_data[i];

	delete _map_data;
}

/* read binary data from file */
char *MapData::load_binary_file(const char *file_name) {
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
void MapData::load_ascii_map_file(const char *file_name)
{
	ifstream infile;
	infile.open(file_name, ios::in);

	if (infile.is_open())
	{
		infile >> _columns;
		infile >> _rows;
		infile.get(); // newlines char
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
				char ch = (char)infile.get();
				//cout << '[' << i << ',' << j << ']' << ch << endl;
				_map_data[i][j] = ch;
			}
		}

		infile.close();
	}
	else 
	{
		cout << "Error opening file" << endl;
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

bool MapData::move_actor(Direction direction)
{
	/* calculate */
	switch (direction) {
		case Direction::up:
			_player_cel_number = _player_cel_number - _columns;
			break;
		case Direction::down: 
			_player_cel_number = _player_cel_number + _columns;
			break;
		case Direction::left: 
			_player_cel_number = _player_cel_number - 1;
			break;
		case Direction::right: 
			_player_cel_number = _player_cel_number + 1;
			break;
		default: 
			break;
	}
	/* some senity checks are needed */
	return true;
}

bool MapData::set_player_at_location(int row, int column)
{
	/* calculate */

	/* some senity checks are needed */
	return false;
}

char *MapData::printable_map()
{
	/* total size + room for newlines char */
	char *buff = new char[_rows * _columns];
	int buff_pos = 0;

	for (int i = 0; i < _rows; i++)
	{
		for (int j = 0; j < _columns; j++)
		{
			if (buff_pos == _player_cel_number)
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

	return buff;
}
