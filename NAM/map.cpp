#include "map.h"

using namespace std;

MapData::MapData(const char *file_name)
{
	load_map(file_name);
}

MapData::~MapData() { }

/* read binary data from file */
char *MapData::load_file(const char *file_name) {
	streampos size;
	char *memblock;

	ifstream file;
	file.open(file_name);
	if (file.is_open())
	{
		size = file.tellg();
		memblock = new char[size];
		file.seekg(0, ios::beg);
		file.read(memblock, size);
		file.close();

		cout << "file loaded" << endl;
		return memblock;
	}
	else {
		cout << "Error opening file" << endl;
		return NULL;
	}
}

/* read map data from file */
void MapData::load_map(const char *file_name) {
	char * memblock = load_file(file_name);


}