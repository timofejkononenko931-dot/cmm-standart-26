#include <iostream>
#include <string>
#include <regex>
#include <vector>
#include <fstream>
#include <stack>
#include <map>

struct ControlBlock {
    int id;
    int exit_id;
    bool has_else;
    bool is_loop;
};

std::map<std::string, std::string> symbol_table;
std::stack<ControlBlock> control_stack;
int label_count = 0;
std::smatch match;

std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return s;
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

std::string get_fasm_type(std::string c_type) {
    if (c_type.find("char") != std::string::npos) return "db";
    if (c_type.find("short") != std::string::npos) return "dw";
    if (c_type.find("int") != std::string::npos) return "dd";
    return "dq";
}

std::string get_mov_size(std::string fasm_type) {
    if (fasm_type == "db") return "byte";
    if (fasm_type == "dw") return "word";
    if (fasm_type == "dd") return "dword";
    return "qword";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: t26.exe <file.t26>" << std::endl;
        return 1;
    }

    std::ifstream sourceFile(argv[1]);
    std::ofstream outFile("output.asm");

    if (!sourceFile.is_open()) {
        std::cerr << "Error: Could not open file!" << std::endl;
        return 1;
    }

    outFile << "format PE64 GUI 5.0\nentry main\ninclude 'win64a.inc'\n";
    outFile << "macro __fastcall [rest] { common invoke rest }\n\nsection '.code' code readable executable\n\n";

    // Прямые регулярки без динамической склейки (чтобы избежать Mismatched '(')
    std::regex func_regex(R"(^\s*(unsigned\s+)?(int|long|char|short|ptr|dq|dd|db|dw|void)\s+([a-zA-Z0-9_]+)\s*\((.*)\))");
    std::regex var_decl_regex(R"(^\s*(unsigned\s+)?(int|long|char|short|ptr|dq|dd|db|dw)\s+([a-zA-Z0-9_]+)(?:\s*=\s*(.*))?$)");
    std::regex while_regex(R"(while\s*\((.*)\s*==\s*(.*)\))");
    std::regex for_regex(R"(for\s*\((.*=.*);\s*(.*==.*);\s*(.*)\))");
    std::regex if_regex(R"(if\s*\((.*)\s*==\s*(.*)\))");
    std::regex elif_regex(R"(else\s+if\s*\((.*)\s*==\s*(.*)\))");
    std::regex assign_regex(R"(^([a-zA-Z0-9_]+)\s*=\s*(.*)$)");
    std::regex call_regex(R"(^([a-zA-Z0-9_]+)\s*\((.*)\)$)");

    std::string line;
    while (std::getline(sourceFile, line)) {
        line = trim(line);
        if (line.empty() || line == "{") continue;
        if (line.back() == ';') line.pop_back();
        line = trim(line);

        if (line == "}") {
            if (!control_stack.empty()) {
                ControlBlock current = control_stack.top();
                control_stack.pop();
                if (current.is_loop) {
                    outFile << "    jmp .L_start_" << current.id << "\n.L_exit_" << current.exit_id << ":" << std::endl;
                } else {
                    if (!current.has_else) outFile << ".L_next_" << current.id << ":" << std::endl;
                    outFile << ".L_exit_" << current.exit_id << ":" << std::endl;
                }
            } else outFile << "endp\n\n";
            continue;
        }

        // 1. Функция
        if (std::regex_search(line, match, func_regex)) {
            outFile << "proc " << (std::string)match[3] << " " << (std::string)match[4] << std::endl;
            continue;
        }

        // 2. Объявление переменной
        if (std::regex_search(line, match, var_decl_regex)) {
            std::string fasm_type = get_fasm_type(match[2]);
            std::string var_name = match[3];
            symbol_table[var_name] = fasm_type;
            outFile << "    local " << var_name << ":" << fasm_type << std::endl;
            if (match[4].matched) {
                 outFile << "    mov " << get_mov_size(fasm_type) << " [" << var_name << "], " << (std::string)match[4] << std::endl;
            }
            continue;
        }

        // 3. While
        if (std::regex_search(line, match, while_regex)) {
            label_count++;
            outFile << ".L_start_" << label_count << ":\n    mov rax, [" << (std::string)match[1] << "]\n    cmp rax, " << (std::string)match[2] << "\n    jne .L_exit_" << label_count << std::endl;
            control_stack.push({label_count, label_count, false, true});
            continue;
        }

        // 4. For
        if (std::regex_search(line, match, for_regex)) {
            label_count++;
            outFile << "    " << (std::string)match[1] << "\n.L_start_" << label_count << ":" << std::endl;
            outFile << "    ; [For Condition Check]\n";
            control_stack.push({label_count, label_count, false, true});
            continue;
        }

        // 5. IF / ELIF / ELSE
        if (std::regex_search(line, match, if_regex)) {
            label_count++;
            outFile << "    mov rax, [" << (std::string)match[1] << "]\n    cmp rax, " << (std::string)match[2] << "\n    jne .L_next_" << label_count << std::endl;
            control_stack.push({label_count, label_count, false, false});
            continue;
        } else if (std::regex_search(line, match, elif_regex)) {
            if (!control_stack.empty()) {
                ControlBlock& current = control_stack.top();
                outFile << "    jmp .L_exit_" << current.exit_id << "\n.L_next_" << current.id << ":" << std::endl;
                label_count++; 
                current.id = label_count;
                outFile << "    mov rax, [" << (std::string)match[1] << "]\n    cmp rax, " << (std::string)match[2] << "\n    jne .L_next_" << label_count << std::endl;
            }
            continue;
        } else if (line == "else" || line == "else {") {
            if (!control_stack.empty()) {
                ControlBlock& current = control_stack.top();
                outFile << "    jmp .L_exit_" << current.exit_id << "\n.L_next_" << current.id << ":" << std::endl;
                current.has_else = true;
            }
            continue;
        }

        // 6. Присваивание
        if (std::regex_search(line, match, assign_regex)) {
            std::string var_name = match[1];
            if (symbol_table.count(var_name)) {
                 outFile << "    mov " << get_mov_size(symbol_table[var_name]) << " [" << var_name << "], " << (std::string)match[2] << std::endl;
            } else {
                 outFile << "    mov [" << var_name << "], " << (std::string)match[2] << std::endl;
            }
            continue;
        }

        // 7. Вызов функций
        if (std::regex_search(line, match, call_regex)) {
            outFile << "    __fastcall " << (std::string)match[1] << ", " << (std::string)match[2] << std::endl;
            continue;
        }

        if (!line.empty()) outFile << "    " << line << std::endl;
    }

    outFile << "\nsection '.idata' import data readable\nlibrary kernel32,'KERNEL32.DLL'\nimport kernel32,ExitProcess,'ExitProcess'\n";
    return 0;
}