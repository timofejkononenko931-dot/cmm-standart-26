#include <iostream>
#include <string>
#include <fstream>

using namespace std;

void read(string filename) {
  ifstream file(filename);

  string line;
  while (getline(file,line)) {

  }
  
  file.close();
}

void write(std::string outFilename) {
  ofstream out(outFilename);

  if (out.is_open()) {
    out << "format PE64 Console\n";
    out << "entry start\n";

    out.close();
  }

}

int main(int argc, char* argv[]) {

  return 0;
}
