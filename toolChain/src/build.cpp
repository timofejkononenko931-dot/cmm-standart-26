#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <regex>
#include <sstream>
#include <cstdlib>

namespace fs = std::filesystem;


std::string get_ini_value(const std::string& filename, const std::string& key) {
    std::ifstream file(filename);
    if (!file.is_open()) return "";
    std::string line;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n")); 
        if (line.find(key + "=") == 0) {
            return line.substr(line.find("=") + 1);
        }
    }
    return "";
}


std::string apply_smart_slashes(const std::string& input) {
    std::stringstream in(input);
    std::stringstream out;
    std::string line;
    int brace_depth = 0;
    bool in_macro = false;

    while (std::getline(in, line)) {
        
        size_t last = line.find_last_not_of(" \t\r\n");
        if (last != std::string::npos) line = line.substr(0, last + 1);
        else line = "";

        
        if (line.find("#define") != std::string::npos) {
            in_macro = true;
        }

        if (in_macro) {
            /
            for (char c : line) {
                if (c == '{') brace_depth++;
                if (c == '}') brace_depth--;
            }

            if (brace_depth > 0) {
                
                out << line << " \\\n";
            } else {
            
                out << line << "\n";
                in_macro = false;
                brace_depth = 0; 
            }
        } else {
            out << line << "\n";
        }
    }
    return out.str();
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: builder <make.ini> <file.c-->" << std::endl;
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

    std::string ini_file = argv[1];
    std::string src_file = argv[2];
    std::string tmp_file = "tmp_" + src_file;

    // 1. Читаем конфиг
    std::string ip = get_ini_value(ini_file, "ip");
    
    // 2. Читаем исходник целиком
    std::ifstream in(src_file);
    if (!in.is_open()) {
        std::cerr << "Error: Cant open " << src_file << std::endl;
        return 1;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string content = buffer.str();
    in.close();

    if (!ip.empty()) {
        std::regex inc_re(R"(#include\s+\"([^\"]+)\")");
        content = std::regex_replace(content, inc_re, "#include \"" + ip + "/$1\"");
    }

    std::regex struct_re(R"(typedef\s+struct\s*\{([\s\S]*?)\}\s*(\w+)\s*;)");
    content = std::regex_replace(content, struct_re, "#define $2 struct {$1}");

    
    std::regex alias_re(R"(typedef\s+(.+)\s+(\w+)\s*;)");
    content = std::regex_replace(content, alias_re, "#define $2 {$1}");

    
    content = apply_smart_slashes(content);

    
    std::ofstream out(tmp_file);
    out << content;
    out.close();

    
    std::string cmd = "c-- " + ini_file + " " + tmp_file;
    std::cout << "[BUILDER] Compiling with Sphinx C--..." << std::endl;
    int res = std::system(cmd.c_str());

    
    if (fs::exists(tmp_file)) fs::remove(tmp_file);

    if (res == 0) {
        std::cout << "[BUILDER] Success!" << std::endl;
    } else {
        std::cout << "[BUILDER] Compilation failed. Check tmp file logic." << std::endl;
    }

    return 0;
}
