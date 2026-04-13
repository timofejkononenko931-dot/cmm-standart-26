#include <iostream>
#include <string>
#include <fstream>

using namespace std;

void read(string filename) {
  ifstream file(filename);

  while (getline(file,line)) {

  }
  
  file.close();
}

void write(std::string outFilename) {
  ofstream out(outFilename);

  if (out.is_open()) {


    out.close();
  }

}

int main(char* argv[], int argc) {

  return 0;
}
