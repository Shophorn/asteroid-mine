#include <iostream>
#include <fstream>
#include <string>
#include <vector>

using namespace std;

static string get_uppercase(string const & s)
{
	string result = s;
	for (char & c : result)
	{
		c = toupper(c);
	}
	return result;
}

int main(int argc, char ** argv)
{
	if (argc != 3)
	{
		std::cout << "Invalid arguments, 1st must be input filename, 2nd must be output filename\n";
		return 1;
	}

	string inputFileName 	= argv[1];
	string outfileName 		= argv[2];
	string headerGuard 		= "WIN32_AM_OPENGL_LOADER_H";
 	string loadFunctionName = "win32_am_load_opengl_functions";
	
	auto namesFile 	= ifstream(inputFileName);
	auto names 		= vector<string>();

	string line;
	while(getline(namesFile, line))
	{
		size_t start = line.find_first_not_of(" \n");
		size_t end = line.find_last_not_of(" \n");

		if (start != string::npos && end != string::npos)
		{
			line = line.substr(start, end + 1);

			if (line.empty() == false && line[0] != '#')
			{
				names.push_back(line);
			}
		}

	}

	auto outfile = ofstream(outfileName);

	outfile << "\// Note(Computer): This is generated file. You probably should not modify it." << "\n";
	outfile << "\n";
	outfile << "#if !defined " << headerGuard << "\n";
	outfile << "\n";
	for (auto const & name : names)
	{
		outfile << "PFN" << get_uppercase(name) << "PROC " << name << ";\n";
	}
	outfile << "\n";
	outfile << "static void " << loadFunctionName << "()\n{\n";
	for (auto const & name : names)
	{
		outfile << "\tAssert(" << name << " == nullptr);\n";
	}
	outfile << "\n";
	for (auto const & name : names)
	{
		outfile << "\t" << name << " = reinterpret_cast<decltype(" << name << ")>(wglGetProcAddress(\"" << name << "\"));\n";
	}
	outfile << "}\n";
	outfile << "\n";
	outfile << "#define " << headerGuard << "\n";
	outfile << "#endif\n";
}