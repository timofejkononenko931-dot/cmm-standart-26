#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

bool g_in_enum = false;
std::string g_current_enum_name = "";

std::string transform_line(std::string line, bool& should_rename) {
    std::string trimmed = line;
    size_t first = trimmed.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return line;
    trimmed = trimmed.substr(first);

    if (trimmed.find("enum") != std::string::npos) {
        g_in_enum = true;
        std::string result = line;
        
        size_t td_pos = result.find("typedef");
        if (td_pos != std::string::npos) {
            result.erase(td_pos, 8);
        }

        size_t enum_pos = result.find("enum");
        std::string head = result.substr(enum_pos + 4);
        size_t brace_open = head.find('{');
        if (brace_open != std::string::npos) {
            std::string potential_name = head.substr(0, brace_open);

            size_t f = potential_name.find_first_not_of(" \t\r\n");
            size_t l = potential_name.find_last_not_of(" \t\r\n");
            if (f != std::string::npos) {
                g_current_enum_name = potential_name.substr(f, l - f + 1);
            }
        }

        size_t close_brace = result.find('}');
        if (close_brace != std::string::npos) {
            size_t semi = result.find(';', close_brace);
            if (semi != std::string::npos) {
                result.erase(close_brace + 1, semi - (close_brace + 1));
                g_in_enum = false;
            }
        }
        return result;
    }
    if (g_in_enum) {
        size_t close_brace = line.find('}');
        if (close_brace != std::string::npos) {
            size_t semi = line.find(';', close_brace);
            if (semi != std::string::npos) {
                std::string result = line;
            
                result.erase(close_brace + 1, semi - (close_brace + 1));
             
                if (g_current_enum_name.empty()) {
                    std::string tail = line.substr(close_brace + 1, semi - (close_brace + 1));
                    
                }

                g_in_enum = false;
                g_current_enum_name = "";
                return result;
            }
        }
        return line;
    }
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
    bool needs_special_name = true;

    
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
    out << "#define __STdcall stdcall\n";
    out << "#define __CDecl cdecl\n";
    out << "#define __STDcall stdcall\n";
    out << "#define __CDEcl cdecl\n";
    out << "#define __STDCall stdcall\n";
    out << "#define __CDECl cdecl\n";
    out << "#define __STDCAll stdcall\n";

    out << "#pragma option IA\n";
    out << "#define __attribute__ {#define }\n";
    out << "#define __addr__ {#define }\n";
    out << "#define volatile\n";
    out << "#define uint32_t {unsigned int}\n";
    out << "#define uint64_t qword\n";
    out << "#define __saveReg__ {asm{pusha};}\n";
    out << "#define __popReg__ {asm{popa};}\n";
    out << "#define __saveReg32__ {asm{pushad};}\n";
    out << "#define __popReg32__ {asm{popad};}\n";

    out << "#define __attribute__regArg16(a,b,c,d) {
        __saveReg__
        asm {
            mov ax, [a]
            mov bx, [b]
            mov cx, [c]
            mov dx, [d]
        };
        __popReg__}\n";

    out << "#define __attribute__regArg32(a,b,c,d) {
        __saveReg32__
        asm {
            mov eax, [a]
            mov ebx, [b]
            mov ecx, [c]
            mov edx, [d]
        };
        __popReg32__}\n";


    out << "//Made with c--_26 by tipoCrutoi228(timoxa)\n";
    out << "\n";
    
    for (const auto& l : lines) {
        out << l << "\n";
    }
    out.close();

    std::cout << "\033[32m[DONE]\033[0m Result saved to: " << out_name << "\n";
    
    return 0;
}
