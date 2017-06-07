#include <iostream>
#include <fstream>
#include <string>
#include <locale>
#include <codecvt>
#include <sstream>

#include <io.h>    // for _setmode
#include <fcntl.h> // for _O_U16TEXT

using namespace std;

int main() {
	//system("chcp 65001 >nul");

	int oldMode = _setmode(_fileno(stdout), _O_U16TEXT);
	const char* filename = "Temp.map";
	
	wifstream map_file;
	map_file.open(filename);
	locale utf8_locale(locale(), new codecvt_utf8<wchar_t>);
	map_file.imbue(utf8_locale);

	wstringstream contents;
	int cols;
	int rows;

	map_file >> cols >> rows >> contents;
	wcout << cols << endl;
	wcout << rows << endl;
	wcout << contents << endl;

	//while (getline(map_file, line)) {
	//	wcout << line << endl;
	//}



	map_file.close();

	getchar();
	return 0;
}