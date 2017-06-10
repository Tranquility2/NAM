#include "map.h"

using namespace std;

MapData::MapData(const char *file_name)
{
	/* init */
	_columns = this->_rows = 0;
	_map_data = NULL;
	load_ascii_map_file(file_name);
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

char *MapData::printable_map()
{
	/* total size + room for newlines char */
	char *buff = new char[_rows * _columns];
	int buff_pos = 0;

	for (int i = 0; i < _rows; i++)
	{
		for (int j = 0; j < _columns; j++)
		{
			buff[buff_pos++] =_map_data[i][j];
		}
	}

	buff[buff_pos++] = { 0 };

	return buff;
}
