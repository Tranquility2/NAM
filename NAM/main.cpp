#include <iostream>
#include <string>

#include "map.h"
#include "console.h"

using namespace std;
	
int main() 
{
	const char *file_name = "temp.map";

	MapData map_data(file_name);

	while (1)
	{
		gotoxy(0, 0);
		//ClearScreenWin();
		cout << map_data.columns() << 'x' << map_data.rows() << endl;
		cout << map_data.printable_map() << endl;
		getchar();
		Sleep(200);
	}
		
	return 0;
}
 