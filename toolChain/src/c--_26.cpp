#include <iostream>
#include <cstdlib>
#include <string>

using namespace std;

int main(int argc, char* argv[]) {
  if (argc <= 1) {
    cout << "for using this compiler\n" << "c--_26 <file make.ini or file> <empty or file.c--";
  }

  if (argc == 2) {
    std::string cmdt26 = "t26 " + argv[1];
    std::system(cmdt26.c_str());

    std::string cmdh = "hmm26 " + "_" + argv[1];
    std::system(cmdh.c_str());
    
  } else if (argc == 3) {
    std::string cmdt26 = "t26 " + argv[2];
    std::system(cmdt26.c_str());

    std::string cmdcmm = "cmm26 " + argv[1] + " _" + argv[2];
    std::system(cmdcmm.c_str());
    
  } else {
    std::cout << "use 1 or 2 arg\n" << std::endl;
  }

  return 0;
}
