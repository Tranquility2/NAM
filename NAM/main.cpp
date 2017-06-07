#include <iostream>
#include <fstream>

using namespace std;

int main() {
	const char* filename = "temp.map";
	fstream map_file;
	map_file.open(filename);

	cout << map_file.rdbuf();

	map_file.close();

	getchar();
	return 0;
}