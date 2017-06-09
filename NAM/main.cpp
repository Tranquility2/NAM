#include "map.h"

using namespace std;
	
int main() {
	const char *file_name = "temp.map";

	MapData map_data(file_name);

	cout << map_data.columns << endl;
	cout << map_data.rows << endl;
	cout << map_data.map_data << endl;

	getchar();
	return 0;
}

