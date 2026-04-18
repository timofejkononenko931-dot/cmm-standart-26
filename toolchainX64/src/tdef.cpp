#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

bool g_m64 = false;
bool g_fasm_only = false;

bool g_in_enum = false;
std::string g_current_enum_name = "";

bool g_in_fasm = false;
std::string g_fasm_buffer = "";

bool g_in_struct = false;
std::string g_struct_name = "";
int g_struct_size = 0;
bool g_struct_has_align = false;

bool g_in_function = false;
std::string g_function_name = "";
std::vector<std::pair<std::string, std::string>> g_function_args; // type, name
bool g_function_has_braces = false;

int get_type_size(const std::string& type) {
    if (type.find("char") != std::string::npos) return 1;
    if (type.find("short") != std::string::npos) return 2;
    if (type.find("int") != std::string::npos || type.find("dword") != std::string::npos) return 4;
    if (type.find("long") != std::string::npos || type.find("qword") != std::string::npos) return 8;
    if (type.find("double") != std::string::npos) return 8;
    if (type.find("float") != std::string::npos) return 4;
    return 0;
}

std::string add_struct_alignment() {
    if (!g_in_struct || g_struct_has_align) {
        return "";
    }
    
    // Выравниваем на границу 16 байт если структура не выравнена
    int remainder = g_struct_size % 16;
    if (remainder != 0) {
        int padding = 16 - remainder;
        std::stringstream ss;
        ss << "  db ";
        for (int i = 0; i < padding; ++i) {
            ss << "0x00" << (i == padding - 1 ? "" : ", ");
        }
        return ss.str();
    }
    return "";
}

std::string get_register_for_arg(const std::string& type, int arg_index, bool is_64bit) {
    if (is_64bit) {
        // Для 64-битного режима используем rcx, rdx, r8, r9
        const char* regs_64[] = {"rcx", "rdx", "r8", "r9"};
        if (arg_index < 4) return regs_64[arg_index];
    } else {
        // Для 32-битного режима используем регистры в зависимости от типа
        if (type.find("char") != std::string::npos) {
            // Для char используем al (нижнюю часть ax)
            return "al";
        } else if (type.find("short") != std::string::npos) {
            // Для short используем ax
            return "ax";
        } else {
            // Для int/long используем eax
            return "eax";
        }
    }
    return "";
}

std::string generate_function_prologue() {
    if (!g_m64 || !g_in_function || g_function_args.empty()) return "";
    
    std::stringstream ss;
    
    for (size_t i = 0; i < g_function_args.size(); ++i) {
        const auto& arg = g_function_args[i];
        std::string reg = get_register_for_arg(arg.first, i, g_m64);
        
        if (!reg.empty()) {
            if (g_m64) {
                // Используем fasm для 64-битного режима
                ss << "fasm { mov [" << arg.second << "], " << reg << " }\n";
            } else {
                // Используем asm для 32-битного режима
                ss << "asm { mov [" << arg.second << "], " << reg << "; }\n";
            }
        }
    }
    
    return ss.str();
}

bool is_register_name(const std::string& name) {
    std::string upper_name = name;
    // Преобразуем в верхний регистр для проверки
    for (auto& c : upper_name) c = toupper(c);
    
    // 64-битные регистры
    static const std::vector<std::string> regs = {
        "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RSP", "RBP",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"
    };
    
    for (const auto& reg : regs) {
        if (upper_name == reg) return true;
    }
    return false;
}

std::string transform_register_assignment(std::string line) {
    // Ищем паттерн: РЕГИСТР = ЗНАЧЕНИЕ;
    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) return line;
    
    size_t semi_pos = line.find(';', eq_pos);
    if (semi_pos == std::string::npos) return line;
    
    // Получаем левую часть (имя регистра)
    std::string left_part = line.substr(0, eq_pos);
    left_part.erase(0, left_part.find_first_not_of(" \t"));
    left_part.erase(left_part.find_last_not_of(" \t") + 1);
    
    // Получаем правую часть (значение)
    std::string right_part = line.substr(eq_pos + 1, semi_pos - eq_pos - 1);
    right_part.erase(0, right_part.find_first_not_of(" \t"));
    right_part.erase(right_part.find_last_not_of(" \t") + 1);
    
    // Проверяем, что это регистр
    if (is_register_name(left_part)) {
        // Преобразуем в нижний регистр
        std::string reg_lower = left_part;
        for (auto& c : reg_lower) c = tolower(c);
        
        // Получаем отступ в начале строки
        std::string indent = "";
        size_t first_non_space = line.find_first_not_of(" \t");
        if (first_non_space != std::string::npos) {
            indent = line.substr(0, first_non_space);
        }
        
        // Получаем остаток строки после ;
        std::string after_semi = line.substr(semi_pos + 1);
        
        // Формируем новую строку
        return indent + "mov " + reg_lower + ", " + right_part + after_semi;
    }
    
    return line;
}

std::string handle_x64_call(const std::string& line) {
    size_t open_bracket = line.find('(');
    size_t close_bracket = line.rfind(')');
    
    if (open_bracket == std::string::npos || close_bracket == std::string::npos) return line;

    std::string func_name = line.substr(0, open_bracket);
    // Убираем пробелы
    func_name.erase(0, func_name.find_first_not_of(" \t"));
    func_name.erase(func_name.find_last_not_of(" \t") + 1);

    std::string args_raw = line.substr(open_bracket + 1, close_bracket - open_bracket - 1);
    std::vector<std::string> args;
    std::stringstream ss(args_raw);
    std::string arg;
    while (std::getline(ss, arg, ',')) {
        // Убираем лишние пробелы в аргументах
        arg.erase(0, arg.find_first_not_of(" \t"));
        arg.erase(arg.find_last_not_of(" \t") + 1);
        if(!arg.empty()) args.push_back(arg);
    }

    std::stringstream result;
    result << "fasm {use64\n";

    // 1. Считаем сколько аргументов пойдут в стек
    int stack_args_count = (args.size() > 4) ? (args.size() - 4) : 0;

    // 2. Считаем общий размер: Shadow Space (32) + Аргументы (count * 8)
    int total_stack_used = 32 + (stack_args_count * 8);

    // 3. ПРОВЕРКА ВЫРАВНИВАНИЯ
    // Если число не делится на 16, добавляем 8 байт для выравнивания
    int alignment_padding = 0;
    if (total_stack_used % 16 != 0) {
        alignment_padding = 8;
    }

    int final_sub = total_stack_used + alignment_padding;

    // Генерируем код
    result << "    sub rsp, " << final_sub << " ; Shadow Space + Stack Args + Alignment\n";

    const char* regs[] = {"rcx", "rdx", "r8", "r9"};

    // Регистры
    for (size_t i = 0; i < args.size() && i < 4; ++i) {
        if (isdigit(args[i][0]) || (args[i].size() > 1 && args[i][1] == 'x')) {
            result << "    mov " << regs[i] << ", " << args[i] << "\n";
        } else {
            result << "    movzx " << regs[i] << ", qword [" << args[i] << "]\n";
        }
    }

// Пушим аргументы в стек (начиная с 4-го)
// Но внимание! Так как мы уже сделали SUB RSP, мы не используем PUSH, 
// а записываем напрямую через MOV по смещениям выше Shadow Space (32).
    for (size_t i = 4; i < args.size(); ++i) {
        int offset = 32 + (i - 4) * 8;
        result << "    mov rax, qword [" << args[i] << "]\n";
        result << "    mov [rsp + " << offset << "], rax\n";
    }

    result << "}; asm {    call " << func_name << "};\n";
    result << "fasm { add rsp, " << final_sub << "\n";
    result << "};";

    return result.str();
}


std::string compile_fasm_block(const std::string& asm_code) {
    std::ofstream tmp_asm("tmp_t26.asm");
    tmp_asm << "format binary\n" << asm_code << "\n";
    tmp_asm.close();


    if (std::system("fasm tmp_t26.asm tmp_t26.bin > nul") != 0) {
        return "/* FASM COMPILATION ERROR */";
    }

    std::ifstream bin("tmp_t26.bin", std::ios::binary);
    if (!bin) return "/* BINARY NOT FOUND */";

    std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(bin), {});
    bin.close();

    std::stringstream ss;
    ss << "db ";
    for (size_t i = 0; i < buffer.size(); ++i) {
        char hex[10];
        sprintf(hex, "0x%02X", buffer[i]);
        ss << hex << (i == buffer.size() - 1 ? "" : ", ");
    }

   
    fs::remove("tmp_t26.asm");
    fs::remove("tmp_t26.bin");

    return ss.str();
}

std::string transform_line(std::string line, bool& should_rename) {

    if (!g_in_fasm && line.find("fasm {") != std::string::npos) {
        g_in_fasm = true;
        g_fasm_buffer = "";
        
        size_t start_pos = line.find("{");
        std::string rest = line.substr(start_pos + 1);
        
 
        if (rest.find("}") != std::string::npos) {
            g_in_fasm = false;
            return compile_fasm_block(rest.substr(0, rest.find("}")));
        }
        return "// FASM BLOCK START"; 
    }

 
    if (g_in_fasm) {
        size_t end_pos = line.find("}");
        if (end_pos != std::string::npos) {
            g_fasm_buffer += " " + line.substr(0, end_pos);
            g_in_fasm = false;
            return compile_fasm_block(g_fasm_buffer);
        }
        g_fasm_buffer += " " + line + "\n";
        return "// ..."; 
    }

    // Если режим только FASM обработки, пропускаем остальные трансформации
    if (g_fasm_only) {
        return line;
    }

    // Преобразование регистровых присваиваний (RAX = 1; -> mov rax, 1)
    line = transform_register_assignment(line);

    std::string trimmed = line;
    size_t first = trimmed.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return line;
    trimmed = trimmed.substr(first);

    // Обработка функций в режиме m64
    if (g_m64 && !g_in_function && trimmed.find("void") != std::string::npos && 
        trimmed.find("(") != std::string::npos) {
        
        // Ищем название функции
        size_t void_pos = trimmed.find("void");
        size_t open_paren = trimmed.find("(", void_pos);
        
        if (open_paren != std::string::npos) {
            std::string before_paren = trimmed.substr(void_pos + 4, open_paren - (void_pos + 4));
            
            // Убираем лишние пробелы и находим имя функции
            size_t name_start = before_paren.find_first_not_of(" \t");
            size_t name_end = before_paren.find_last_not_of(" \t");
            
            if (name_start != std::string::npos && name_end != std::string::npos) {
                g_function_name = before_paren.substr(name_start, name_end - name_start + 1);
                g_in_function = true;
                g_function_args.clear();
                g_function_has_braces = false;
                
                // Парсим аргументы
                size_t close_paren = trimmed.find(")", open_paren);
                if (close_paren != std::string::npos) {
                    std::string args_str = trimmed.substr(open_paren + 1, close_paren - open_paren - 1);
                    
                    // Разбираем аргументы через запятую
                    std::stringstream ss(args_str);
                    std::string arg;
                    while (std::getline(ss, arg, ',')) {
                        // Убираем пробелы
                        arg.erase(0, arg.find_first_not_of(" \t"));
                        arg.erase(arg.find_last_not_of(" \t") + 1);
                        
                        if (!arg.empty()) {
                            // Разделяем тип и имя
                            std::stringstream arg_ss(arg);
                            std::string type, name;
                            arg_ss >> type >> name;
                            
                            if (!type.empty() && !name.empty()) {
                                g_function_args.push_back({type, name});
                            }
                        }
                    }
                }
            }
        }
    }

    // Проверяем на наличие фигурных скобок в функции
    if (g_in_function && !g_function_has_braces && trimmed.find("{") != std::string::npos) {
        g_function_has_braces = true;
        // Генерируем пролог функции
        std::string prologue = generate_function_prologue();
        if (!prologue.empty()) {
            return line + "\n" + prologue;
        }
    }

    // Завершение функции
    if (g_in_function && trimmed.find("}") != std::string::npos && 
        trimmed.find("{") == std::string::npos) {
        g_in_function = false;
        g_function_name = "";
        g_function_args.clear();
        g_function_has_braces = false;
    }

    // Обработка структур для выравнивания
    if (trimmed.find("struct") != std::string::npos && trimmed.find('{') != std::string::npos) {
        g_in_struct = true;
        g_struct_size = 0;
        g_struct_has_align = (line.find("align") != std::string::npos);
        
        size_t struct_pos = trimmed.find("struct");
        size_t brace_pos = trimmed.find('{', struct_pos);
        std::string struct_header = trimmed.substr(struct_pos, brace_pos - struct_pos);
        
        size_t name_start = struct_header.find_first_not_of(" \t", 6);
        if (name_start != std::string::npos) {
            size_t name_end = struct_header.find_first_of(" \t{", name_start);
            if (name_end != std::string::npos) {
                g_struct_name = struct_header.substr(name_start, name_end - name_start);
            }
        }
    }

    // Отслеживание размеров членов структуры
    if (g_in_struct && trimmed.find('}') == std::string::npos && trimmed.find(';') != std::string::npos) {
        std::stringstream ss(trimmed);
        std::string type, name;
        ss >> type >> name;
        
        int member_size = get_type_size(type);
        if (member_size > 0) {
            g_struct_size += member_size;
        }
    }

    // Завершение структуры и добавление выравнивания
    if (g_in_struct && trimmed.find("}") != std::string::npos) {
        std::string result = line;
        g_in_struct = false;
        
        std::string alignment = add_struct_alignment();
        if (!alignment.empty()) {
            result += "\n" + alignment;
        }
        
        g_struct_has_align = false;
        return result;
    }

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
    
    // Внутри transform_line, перед обработкой '&'
    if (g_m64 && line.find('(') != std::string::npos && line.find(';') != std::string::npos) {
    // Простая проверка, что это не объявление функции
        if (line.find("void ") == std::string::npos && line.find("int ") == std::string::npos) {
            return handle_x64_call(line);
        }
    }

    std::string processed = "";
    for (size_t i = 0; i < line.length(); ++i) {
        if (line[i] == '&') {
            bool is_bitwise = false;
            
            
            int j = (int)i - 1;
            while (j >= 0 && (line[j] == ' ' || line[j] == '\t')) j--;

            if (j >= 0) {
                char left = line[j];
                if (std::isalnum(static_cast<unsigned char>(left)) || left == ')' || left == ']' || left == '_') {
                    is_bitwise = true;
                }
            }

            if (line[i] == '&' && i + 1 < line.length() && line[i+1] == '&') {
                processed += "&&";
                i++; 
                continue;
            }

            if (is_bitwise) {
                processed += '&'; 
            } else {
                processed += '#'; 
            }
        } else {
            processed += line[i];
        }
    }
    line = processed;

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
        std::cout << "Usage: t26 <file> [-m64] [-fasm_only]\n";
        return 1;
        
    }
    
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-m64") g_m64 = true;
        if (std::string(argv[i]) == "-fasm_only") g_fasm_only = true;
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
    out << "#define __fastcall fastcall\n";
    out << "#define __declspec(x) _export\n";
    out << "#define ddbf {db 0xDE, 0xAD, 0xBE, 0xEF}\n";
    out << "#define __align16 {db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,\\\n";
    out << "                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}\n";
    out << "#define __align8 {db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}\n";

    out << "#pragma option IA\n";
    out << "#pragma option AS=32\n";
    out << "#define volatile\n";
    out << "#define uint32_t {unsigned int}\n";
    out << "#define uint64_t qword\n";

    out << R"(#define __saveReg__ {asm{push ax
    push bx
    push cx
    push dx};})" << "\n";

    out << R"(#define __popReg__ {asm{pop ax
    pop bx
    pop cx
    pop dx};})" << "\n";

    out << R"(#define __saveReg32__ {asm{push eax
    push ebx
    push ecx
    push edx};})" << "\n";

    out << R"(#define __popReg32__ {asm{pop eax
    pop ebx
    pop ecx
    pop edx};})" << "\n";

    out << "#define __argv16 word\n";
    out << "#define __argv32 dword\n";

    out << "#define __reg32Arg1__ EAX\n";
    out << "#define __reg32Arg2__ EBX\n";
    out << "#define __reg32Arg3__ ECX\n";
    out << "#define __reg32Arg4__ EDX\n";
    out << "#define __reg16Arg1__ AX\n";
    out << "#define __reg16Arg2__ BX\n";
    out << "#define __reg16Arg3__ CX\n";
    out << "#define __reg16Arg4__ DX\n";
    
    out << R"(#define __attribute__regArg16(a,c,d) { 
        asm { 
            mov ax, a 
            mov cx, c 
            mov dx, d 
        }; 
    })" << "\n";

    out << R"(#define __attribute__regArg32(a,c,d) { 
        asm { 
            mov eax, a 
            mov ecx, c
            mov edx, d 
        }; 
    })" << "\n";


    out << "//Made with c--_26 by tipoCrutoi228(timoxa)\n";
    out << "\n";
    
    for (const auto& l : lines) {
        out << l << "\n";
    }
    out.close();

    std::cout << "\033[32m[DONE]\033[0m Result saved to: " << out_name << "\n";
    
    return 0;
}