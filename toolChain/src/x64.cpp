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
    out << "Format PE 64 Console\n";

    out.close();
  }

}

int main(int argc, char* argv[]) {

  return 0;
}
