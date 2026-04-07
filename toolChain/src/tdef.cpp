#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;


std::string transform_line(std::string line, bool& should_rename) {
    std::string trimmed = line;
    size_t first = trimmed.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return line;
    trimmed = trimmed.substr(first);

    if (trimmed.rfind("typedef ", 0) == 0) {
        if (trimmed.find("struct") != std::string::npos) {
            size_t pos = line.find("typedef");
            std::string result = line;
            result.erase(pos, 8); 
            return result;
        }

        std::string content = trimmed.substr(8); 
        size_t semi = content.find(';');
        if (semi != std::string::npos) content = content.substr(0, semi);

        std::stringstream ss(content);
        std::string t;
        std::vector<std::string> tokens;
        while (ss >> t) tokens.push_back(t);

        
        if (tokens.size() >= 3) {
            should_rename = true; 
            std::string name = tokens.back(); 
            std::string type = "";
            
            
            for (size_t i = 0; i < tokens.size() - 1; ++i) {
                type += tokens[i] + (i == tokens.size() - 2 ? "" : " ");
            }
            return "#define " + name + " {" + type + "}";
        }
        
        
        if (tokens.size() == 2) {
            return "#define " + tokens[1] + " " + tokens[0];
        }
    }
    return line;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: t26 <file>\n";
        return 1;
    }

    std::system("cls");
    
    std::cout << "\033[36m" << R"(
  _____________________________________________________________
 /                                                             \
 |     _________             ___   __                          |
 |    |___   ___|           |__ \ / /                          |
 |        | |                 / /| | _                         |
 |        | |                / /_| |(_)                        |
 |        |_|               |____|\_\                          |
 |                                                             |
 | > C-- Compiler Infrastructure [Standard 2026]               |
 | > Sphinx-based Toolchain v2.6                               |
 |_____________________________________________________________|
 \_____________________________________________________________/
    )" << "\033[0m\n";

    fs::path p(argv[1]);
    if (!fs::exists(p)) {
        std::cerr << "File not found!\n";
        return 1;
    }

    std::vector<std::string> lines;
    std::ifstream in(argv[1]);
    std::string line;
    bool needs_special_name = false;

    
    while (std::getline(in, line)) {
        lines.push_back(transform_line(line, needs_special_name));
    }
    in.close();

    
    std::string out_name;
    if (needs_special_name) {
        out_name = "_" + p.filename().string(); 
    } else {
        out_name = "fixed_" + p.filename().string();
    }

    std::ofstream out(out_name);

    out << "// C calling convension defines for C--\n";
    out << "#define __stdcall stdcall\n";
    out << "#define __cdecl cdecl\n";
    out << "#define __STDCALL stdcall\n";
    out << "#define __CDECL cdecl\n";
    out << "#define __Stdcall stdcall\n";
    out << "#define __Cdecl cdecl\n";
    
    for (const auto& l : lines) {
        out << l << "\n";
    }
    out.close();

    std::cout << "\033[32m[DONE]\033[0m Result saved to: " << out_name << "\n";
    
    return 0;
}
