#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;


std::string process_header_line(std::string line, bool& in_multiline_define) {
    
    std::string trimmed = line;
    size_t first = trimmed.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    if (trimmed.rfind("#define", 0) == 0 && trimmed.find('{') != std::string::npos) {
        in_multiline_define = true;
    }

    if (in_multiline_define) {
         
        if (!trimmed.empty() && trimmed.back() != '\\') {
            
            if (trimmed.find('}') != std::string::npos) {
                in_multiline_define = false;
            } else {
                line += " \\";
            }
        }
        return line;
    }

    
    if (trimmed.rfind("typedef ", 0) == 0) {
        
        if (trimmed.find("struct") != std::string::npos) {
            size_t pos = line.find("typedef struct");
            line.replace(pos, 14, "struct");
            return line;
        }

        
        std::string content = trimmed.substr(8); 
        size_t semi = content.find(';');
        if (semi != std::string::npos) content = content.substr(0, semi);

        size_t last_space = content.find_last_of(" \t");
        if (last_space != std::string::npos) {
            std::string type = content.substr(0, last_space);
            std::string name = content.substr(last_space + 1);
            
            
            name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
            type.erase(std::remove(type.begin(), type.end(), ' '), type.end());

            if (!name.empty() && !type.empty()) {
                return "#define " + name + " " + type;
            }
        }
    }

    return line;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: hfix <file.h> or <file.h-->\n";
        return 1;
    }

    std::system("cls");

    std::cout << "\033[36m"; 
    std::cout << R"(
  _____________________________________________________________
 /                                                             \
 |   ____  __  __ __  __     ___   __                          |
 |  / ___||  \/  |  \/  |   |__ \ / /                          |
 | | |    | |\/| | |\/| |     / /| | _                         |
 | | |___ | |  | | |  | |    / /_| |(_)                        |
 |  \____||_|  |_|_|  |_|   |____|\_\                          |
 |                                                             |
 | > C-- Compiler Infrastructure [Standard 2026]               |
 | > Sphinx-based Toolchain v2.6                               |
 |_____________________________________________________________|
 \_____________________________________________________________/
    )" << "\033[0m\n";

    for (int i = 1; i < argc; i++) {
        fs::path input_path(argv[i]);
        std::string out_name = "tmp_" + input_path.stem().string() + ".h--";

        std::ifstream in(argv[i]);
        if (!in.is_open()) {
            std::cerr << "Could not open: " << argv[i] << "\n";
            continue;
        }

        std::ofstream out(out_name);
        std::string line;
        bool in_macro = false;

        std::cout << "Fixing: " << argv[i] << " -> " << out_name << "\n";

        while (std::getline(in, line)) {
            out << process_header_line(line, in_macro) << "\n";
        }

        in.close();
        out.close();
    }

    std::cout << "Done! All headers are ready for CMM_26.\n";
    return 0;
}
