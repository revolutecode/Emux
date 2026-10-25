#include "Emux/Generator/NasmGenerator.hpp"
#include "Emux/CLI/Arguments.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Compiler/Type/TypeParser.hpp"
#include "Emux/Core/Logger.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <filesystem>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    std::pair<std::string, std::size_t> split_one(std::string_view input, char separator = ' ')
    {
        auto pos = input.find_first_of(separator);
        return {std::string(input.substr(0, pos)), pos};
    }

    std::vector<std::string> split(std::string_view input, char separator = ' ', bool iss = false)
    {
        std::vector<std::string> vec; 
        size_t index = 0;
        
        bool isString = false;
        while (index < input.size())
        {
            std::string element;
            for (auto it = input.begin() + index; it != input.end(); it++)
            {
                bool end = *it == separator && ((iss) ? !isString : true);
                if (end) break;
                
                if (*it == '"') isString = !isString;

                element += *it;
                index++;
            }
            vec.push_back(element);
            index++;
        }

        return vec;
    }

    std::string mangled(std::string_view name, uint32_t size, bool isStdcall, Emux::Architecture arch)
    {
        if (arch == Emux::Architecture::X86_64)
        {
            // Windows x64 ABI nao utiliza decoracao de stdcall (@N) nem sublinhado inicial
            return std::string(name);
        }

        std::string res(name);
        res.insert(res.begin(), '_');
        if (isStdcall && size > 0)
        {
            res += "@";
            res += std::to_string(size);
        }
        return res;
    }

    template<typename T = int>
    T convertString(std::string_view str)
    {
        std::stringstream sstr(str.data());
        T var{};
        sstr >> var;
        return var;
    }

    std::string nasmSize(int bits)
    {
        if (bits <= 8) return "byte";
        if (bits <= 16) return "word";
        if (bits <= 32) return "dword";
        if (bits <= 64) return "qword";
        throw std::runtime_error("tipo maior que 64 bits nao suportado no backend nasm");
    }

    using Header = std::ostringstream;
    using Body = std::ostringstream;
    using Bodies = std::map<std::string, Body>;
    
    struct Data
    {
        Header header;
        Bodies bodies;
    };
    
    enum class ExternType
    {
        STDCALL,
        CDECL
    };

    struct SlotInfo
    {
        bool isMemory = false;
        std::string memoryText;
        std::size_t physIndex = 0;
        int bits = 32;
    };

    // Estado da geracao de codigo
    Emux::Architecture g_CurrentArch = Emux::Architecture::X86_64;
    std::unordered_map<std::string, std::pair<ExternType, std::size_t>> externsEmitted;
    std::unordered_set<std::string> functionsEmitted;
    std::unordered_set<std::string> dataLabelsEmitted;
    std::unordered_map<std::string, SlotInfo> currentRegMap;
    std::size_t currentSpillCount = 0;
    bool frameEmitted = false;
    std::map<std::string, Emux::Type> variablesEmitted;

    void resetState(Emux::Architecture arch)
    {
        g_CurrentArch = arch;
        externsEmitted.clear();
        functionsEmitted.clear();
        dataLabelsEmitted.clear();
        currentRegMap.clear();
        currentSpillCount = 0;
        frameEmitted = false;
        variablesEmitted.clear();
    }

    void ensureFrame(Data& data)
    {
        if (!frameEmitted)
        {
            if (g_CurrentArch == Emux::Architecture::X86)
            {
                data.bodies["text"] << "push ebp\nmov ebp, esp\n";
            }
            else
            {
                data.bodies["text"] << "push rbp\nmov rbp, rsp\n";
            }
            frameEmitted = true;
        }
    }

    bool looksLikeReg(std::string_view s)
    {
        return s.size() >= 2 && s[0] == 'r' && std::isdigit(static_cast<unsigned char>(s[1]));
    }

    const std::vector<std::vector<std::string>> physRegForms = {
        {"bl", "bx", "ebx", "rbx"},
        {"cl", "cx", "ecx", "rcx"},
        {"dl", "dx", "edx", "rdx"},
        {"sil", "si", "esi", "rsi"},
        {"dil", "di", "edi", "rdi"}
    };

    const std::unordered_map<std::string, std::vector<std::string>> reservedRegForms {
        { "rr0", {"al", "ax", "eax", "rax"} },
        { "rr1", {"dl", "dx", "edx", "rdx"} },
    };

    std::string sizedRegName(const std::vector<std::string>& forms, int bits, std::string_view baseName)
    {
        if (g_CurrentArch == Emux::Architecture::X86)
        {
            if (bits > 32)
            {
                throw std::runtime_error(
                    "'" + std::string(baseName) + "': backend x86-32 nao suporta registrador de " +
                    std::to_string(bits) + " bits");
            }
            int sizeIdx = (bits <= 8) ? 0 : (bits <= 16) ? 1 : 2;
            return forms.at(sizeIdx);
        }
        else
        {
            int sizeIdx = (bits <= 8) ? 0 : (bits <= 16) ? 1 : (bits <= 32) ? 2 : 3;
            return forms.at(sizeIdx);
        }
    }

    std::string RegOrVarOrValue(std::string_view input)
    {
        std::string str(input);

        auto colonPos = str.find(':');
        bool hasOverride = colonPos != std::string::npos;
        std::string name = hasOverride ? str.substr(0, colonPos) : str;
        int overrideBits = hasOverride ? std::stoi(str.substr(colonPos + 1)) : 0;

        int defaultBits = (g_CurrentArch == Emux::Architecture::X86_64) ? 64 : 32;

        if (auto it = reservedRegForms.find(name); it != reservedRegForms.end())
        {
            return sizedRegName(it->second, hasOverride ? overrideBits : defaultBits, name);
        }

        if (auto it = currentRegMap.find(name); it != currentRegMap.end())
        {
            const SlotInfo& slot = it->second;
            if (slot.isMemory)
            {
                if (hasOverride)
                {
                    Emux::Logger::Error("'" + name + "' e memoria - nao aceita sufixo ':N'");
                }
                return slot.memoryText;
            }
            return sizedRegName(physRegForms.at(slot.physIndex), hasOverride ? overrideBits : slot.bits, name);
        }

        if (auto it = variablesEmitted.find(name); it != variablesEmitted.end())
        {
            if (hasOverride)
            {
                Emux::Logger::Error("'" + name + "' e variavel global - nao aceita sufixo ':N'");
            }
            std::stringstream sstr;
            sstr << nasmSize(it->second.Bits);
            sstr << " [";
            if (g_CurrentArch == Emux::Architecture::X86_64)
            {
                sstr << "rel ";
            }
            sstr << name;
            sstr << "]";
            return sstr.str();
        }

        if (looksLikeReg(name))
        {
            Emux::Logger::Error("registrador '" + name + "' usado sem ter sido declarado em 'regs' ou 'args'");
        }

        return str;
    }

    std::pair<std::string, Emux::Type> ParseDecl(std::string_view input)
    {
        static Emux::TypeParser parser;

        auto [name, namePos] = split_one(input, ':');
        
        if (namePos == std::string::npos)
        {
            Emux::Logger::Error("variavel '" + name + "' sem tipo declarado");
        }

        auto type = parser.Parse(input.substr(namePos + 1));

        if (!type.has_value())
        {
            Emux::Logger::Error("Tipo invalido: " + std::string(input.substr(namePos + 1)));
            return std::make_pair(name, Emux::Type{});
        }
        return std::make_pair(name, type.value());
    }

    void DoubleMathInstruction(std::string_view line, Data& data)
    {
        std::string correctLine(line.substr(line.find_first_of(' ') + 1));
        auto args = split(correctLine, ',');

        std::string strInst(line.substr(0u, line.find_first_of(' ')));

        if (args.size() < 2)
        {
            Emux::Logger::Error("Instruction received quantity of args less than 2: " + std::string(line));
            return;
        }
        
        data.bodies["text"] << strInst << " " << RegOrVarOrValue(args[0]) << ", " << RegOrVarOrValue(args[1]) << '\n';
    }

    using Func = void(*)(std::string_view, Data& data);
    const std::unordered_map<std::string, Func> instructions
    {
        {"regs", [](std::string_view line, Data& data)
            {
                std::string correctLine(line.substr(5u));
                auto decls = split(correctLine, ',');

                std::size_t spillCount = 0;
                for (std::size_t i = 0; i < decls.size(); ++i)
                {
                    auto [name, type] = ParseDecl(decls[i]);
                    if (i < physRegForms.size())
                    {
                        currentRegMap[name] = { false, "", i, (int)type.Bits };
                    }
                    else
                    {
                        int slotBytes = (g_CurrentArch == Emux::Architecture::X86_64) ? 8 : 4;
                        std::string baseReg = (g_CurrentArch == Emux::Architecture::X86_64) ? "rbp" : "ebp";
                        std::string memText = nasmSize(type.Bits) + " [" + baseReg + "-" + std::to_string(slotBytes * (++spillCount)) + "]";
                        currentRegMap[name] = { true, memText, 0, (int)type.Bits };
                    }
                }
                currentSpillCount = spillCount;

                if (spillCount > 0)
                {
                    ensureFrame(data);
                    if (g_CurrentArch == Emux::Architecture::X86)
                    {
                        data.bodies["text"] << "sub esp, " << (4 * spillCount) << "\n";
                    }
                    else
                    {
                        size_t bytes = 8 * spillCount;
                        if (bytes % 16 != 0) bytes += (16 - (bytes % 16));
                        data.bodies["text"] << "sub rsp, " << bytes << "\n";
                    }
                }
            }
        },
        {"vars", [](std::string_view line, Data& data)
            {
                std::string correctLine(line.substr(5u));
                auto decls = split(correctLine, ',');
                
                for (auto& decl : decls)
                {
                    auto [name, type] = ParseDecl(decl);
                    variablesEmitted[name] = type;
                    data.bodies["bss"] << name << ": res" << nasmSize(type.Bits)[0] << " 1\n"; 
                }
            }
        },
        {"args", [](std::string_view line, Data& data)
            {
                std::string correctLine(line.substr(5u));
                auto decls = split(correctLine, ',');

                for (std::size_t i = 0; i < decls.size(); ++i)
                {
                    auto [name, type] = ParseDecl(decls[i]);
                    
                    if (g_CurrentArch == Emux::Architecture::X86)
                    {
                        std::string memText = nasmSize(type.Bits) + " [ebp+" + std::to_string(8 + 4 * i) + "]";
                        currentRegMap[name] = { true, memText, 0, (int)type.Bits };
                    }
                    else
                    {
                        // x86_64: argumentos 0..3 passados em registradores rcx, rdx, r8, r9
                        std::string memText = nasmSize(type.Bits) + " [rbp+" + std::to_string(16 + 32 + 8 * i) + "]";
                        currentRegMap[name] = { true, memText, 0, (int)type.Bits };
                    }
                }

                ensureFrame(data);
            }
        },
        {"returns", [](std::string_view, Data&)
            {
            }
        },
        {"bytes", [](std::string_view line, Data& data)
            {
                std::string correctLine(line.substr(6u));
                auto args = split(correctLine, ',', true);

                if (args.empty()) return;

                std::string label = args[0];
                dataLabelsEmitted.insert(label);

                std::string message{};
                for (auto it = ++args.begin(); it != args.end(); ++it)
                {
                    message += (*it);
                    message += ',';
                }
                if (!message.empty()) message.pop_back();

                data.bodies["data"] << label << " db " << message << '\n';
                data.bodies["data"] << label << "_len equ $ - " << label << '\n';
            }
        },
        {"shl", &DoubleMathInstruction },
        {"shr", &DoubleMathInstruction },
        {"or", &DoubleMathInstruction },
        {"xor", &DoubleMathInstruction },
        {"and", &DoubleMathInstruction },
        {"movzx", &DoubleMathInstruction },
        {"movsx", &DoubleMathInstruction },
        {"mov", &DoubleMathInstruction },
        {"add", &DoubleMathInstruction },
        {"sub", &DoubleMathInstruction },
        {"mul", [](std::string_view line, Data& data)
            {
                std::string correctLine(line.substr(4u));
                auto args = split(correctLine, ',');

                if (args.size() < 2) throw std::runtime_error("Invalid mul");
                data.bodies["text"] << "imul " << RegOrVarOrValue(args[0]) << ", " << RegOrVarOrValue(args[1]) << '\n';
            }
        },
        {"div", [](std::string_view line, Data& data)
            {
                std::string correctLine(line.substr(4u));
                auto args = split(correctLine, ',');

                if (args.empty()) throw std::runtime_error("Invalid div");

                if (args.size() >= 2)
                {
                    std::string dest = RegOrVarOrValue(args[0]);
                    std::string src = RegOrVarOrValue(args[1]);

                    bool is64 = (g_CurrentArch == Emux::Architecture::X86_64) &&
                                ((dest.size() >= 3 && dest[0] == 'r' && std::isalpha(static_cast<unsigned char>(dest[1]))) ||
                                 dest.starts_with("qword"));

                    std::string ax = is64 ? "rax" : "eax";
                    std::string cx = is64 ? "rcx" : "ecx";
                    std::string ext = is64 ? "cqo" : "cdq";

                    data.bodies["text"] << "mov " << ax << ", " << dest << '\n';
                    data.bodies["text"] << ext << '\n';

                    if (!src.empty() && (std::isdigit(static_cast<unsigned char>(src[0])) || src[0] == '-'))
                    {
                        data.bodies["text"] << "mov " << cx << ", " << src << '\n';
                        data.bodies["text"] << "idiv " << cx << '\n';
                    }
                    else
                    {
                        data.bodies["text"] << "idiv " << src << '\n';
                    }
                    data.bodies["text"] << "mov " << dest << ", " << ax << '\n';
                }
                else
                {
                    std::string src = RegOrVarOrValue(args[0]);
                    if (g_CurrentArch == Emux::Architecture::X86)
                    {
                        data.bodies["text"] << "cdq\n";
                        data.bodies["text"] << "idiv " << src << '\n';
                    }
                    else
                    {
                        data.bodies["text"] << "cqo\n";
                        data.bodies["text"] << "idiv " << src << '\n';
                    }
                }
            }
        },
        {"neg", [](std::string_view line, Data& data)
            {
                std::string target = std::string(line.substr(4u));
                while (!target.empty() && std::isspace(static_cast<unsigned char>(target.front())))
                    target.erase(target.begin());
                while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back())))
                    target.pop_back();

                data.bodies["text"] << "neg " << RegOrVarOrValue(target) << '\n';
            }
        },
        {"not", [](std::string_view line, Data& data)
            {
                std::string target = std::string(line.substr(4u));
                while (!target.empty() && std::isspace(static_cast<unsigned char>(target.front())))
                    target.erase(target.begin());
                while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back())))
                    target.pop_back();

                data.bodies["text"] << "not " << RegOrVarOrValue(target) << '\n';
            }
        },
        {"import", [](std::string_view line, Data& data)
            {
                std::string correctLine(line.substr(7u));
                auto [externName, pos] = split_one(correctLine, ',');
                auto [stype, newPos] = split_one(correctLine.substr(pos + 1), ',');
                auto [ssize, _] = split_one(correctLine.substr(pos + newPos + 2), ',');

                ExternType type = (stype == "cdecl") ? ExternType::CDECL : ExternType::STDCALL;
                std::size_t size = convertString<std::size_t>(ssize);

                if (!externsEmitted.contains(externName))
                {
                    externsEmitted[externName] = { type, size };
                    std::string target = mangled(externName, size, type == ExternType::STDCALL, g_CurrentArch);
                    data.header << "extern " << target << '\n';
                }
            }
        },
        {"call", [](std::string_view line, Data& data)
            {
                std::string correctLine(line.substr(5u));
                auto [callName, callPos] = split_one(correctLine, ',');

                std::string resolved = RegOrVarOrValue(callName);
                bool isSolved = (resolved != callName);

                bool isExtern = externsEmitted.contains(callName);
                std::size_t extSize = isExtern ? externsEmitted[callName].second : 0;
                bool isStdcall = isExtern && (externsEmitted[callName].first == ExternType::STDCALL);

                std::vector<std::string> args;
                if (correctLine.size() > callName.size() && callPos != std::string::npos)
                {
                    args = split(correctLine.substr(callPos + 1), ',');
                }

                if (g_CurrentArch == Emux::Architecture::X86)
                {
                    // Backend x86 (32 bits Win32): empurra argumentos na pilha da direita para a esquerda
                    auto pushArgs = args;
                    std::reverse(pushArgs.begin(), pushArgs.end());

                    for (const std::string& arg : pushArgs)
                    {
                        data.bodies["text"] << "push " << RegOrVarOrValue(arg) << '\n';
                    }

                    std::string target;
                    if (isSolved)
                    {
                        target = resolved;
                    }
                    else if (isExtern)
                    {
                        target = mangled(callName, extSize, isStdcall, g_CurrentArch);
                    }
                    else
                    {
                        target = callName;
                    }

                    data.bodies["text"] << "call " << target << '\n';

                    if (isExtern && !isStdcall && !args.empty())
                    {
                        data.bodies["text"] << "add esp, " << (4 * args.size()) << '\n';
                    }
                }
                else
                {
                    // Backend x86_64 (Windows x64 ABI):
                    // 1o arg: rcx, 2o arg: rdx, 3o arg: r8, 4o arg: r9
                    // 5o em diante: [rsp + 32 + 8*(i-4)]
                    // Shadow space: 32 bytes + espaco adicional alinhado em 16 bytes
                    size_t numArgs = args.size();
                    size_t stackSpace = 32;
                    if (numArgs > 4)
                    {
                        stackSpace += 8 * (numArgs - 4);
                    }
                    // Alinha stackSpace para que a pilha fique alinhada a 16 bytes no call
                    if (stackSpace % 16 != 0)
                    {
                        stackSpace += (16 - (stackSpace % 16));
                    }

                    data.bodies["text"] << "sub rsp, " << stackSpace << '\n';

                    const std::vector<std::string> x64ArgRegs64 = {"rcx", "rdx", "r8", "r9"};
                    const std::vector<std::string> x64ArgRegs32 = {"ecx", "edx", "r8d", "r9d"};

                    auto emitArg = [&](const std::string& arg, const std::string& reg64, const std::string& reg32, size_t stackOffset)
                    {
                        std::string cleanArg = arg;
                        // Verifica se e label de dados (ex: buffer message) ou variavel para passar endereco
                        bool isDataLabel = dataLabelsEmitted.contains(cleanArg);
                        bool isVar = variablesEmitted.contains(cleanArg);

                        if (stackOffset == 0) // Vai em registrador
                        {
                            if (isDataLabel || isVar)
                            {
                                data.bodies["text"] << "lea " << reg64 << ", [" << cleanArg << "]\n";
                            }
                            else if (cleanArg == "rr0")
                            {
                                data.bodies["text"] << "mov " << reg64 << ", rax\n";
                            }
                            else
                            {
                                std::string val = RegOrVarOrValue(cleanArg);
                                data.bodies["text"] << "mov " << reg32 << ", " << val << '\n';
                            }
                        }
                        else // Vai na pilha em [rsp + offset]
                        {
                            if (isDataLabel || isVar)
                            {
                                data.bodies["text"] << "lea rax, [" << cleanArg << "]\n";
                                data.bodies["text"] << "mov [rsp + " << stackOffset << "], rax\n";
                            }
                            else if (cleanArg == "rr0")
                            {
                                data.bodies["text"] << "mov [rsp + " << stackOffset << "], rax\n";
                            }
                            else
                            {
                                std::string val = RegOrVarOrValue(cleanArg);
                                data.bodies["text"] << "mov dword [rsp + " << stackOffset << "], " << val << '\n';
                            }
                        }
                    };

                    for (size_t i = 0; i < numArgs; ++i)
                    {
                        if (i < 4)
                        {
                            emitArg(args[i], x64ArgRegs64[i], x64ArgRegs32[i], 0);
                        }
                        else
                        {
                            size_t offset = 32 + 8 * (i - 4);
                            emitArg(args[i], "", "", offset);
                        }
                    }

                    std::string target = isExtern ? std::string(callName) : (isSolved ? resolved : std::string(callName));
                    data.bodies["text"] << "call " << target << '\n';
                    data.bodies["text"] << "add rsp, " << stackSpace << '\n';
                }
            }
        },
        {"func", [](std::string_view line, Data& data)
            {
                std::string name(line.substr(5u));
                if (functionsEmitted.contains(name))
                {
                    Emux::Logger::Error("Function redefined: " + name);
                }
                functionsEmitted.emplace(name);

                currentRegMap.clear();
                currentSpillCount = 0;
                frameEmitted = false;

                if (g_CurrentArch == Emux::Architecture::X86 && (name == "main_Start" || name == "Main_Start"))
                {
                    data.bodies["text"] << "_" << name << ":\n";
                }
                data.bodies["text"] << name << ":\n";
            }
        },
        {"ret", [](std::string_view, Data& data)
            {
                if (frameEmitted)
                {
                    if (g_CurrentArch == Emux::Architecture::X86)
                    {
                        data.bodies["text"] << "mov esp, ebp\npop ebp\n";
                    }
                    else
                    {
                        data.bodies["text"] << "mov rsp, rbp\npop rbp\n";
                    }
                }
                data.bodies["text"] << "ret\n";
            }
        },
        {"exit", [](std::string_view line, Data& data)
            {
                std::string externName = "ExitProcess";
                std::string codeArg = RegOrVarOrValue(line.substr(5u));

                if (!externsEmitted.contains(externName))
                {
                    externsEmitted[externName] = { ExternType::STDCALL, 4 };
                    std::string extTarget = mangled(externName, 4, true, g_CurrentArch);
                    data.header << "extern " << extTarget << '\n';
                }
                
                auto& body = data.bodies["text"];
                if (g_CurrentArch == Emux::Architecture::X86)
                {
                    body << "push " << codeArg << '\n';
                    body << "call " << mangled(externName, 4, true, g_CurrentArch) << '\n';
                }
                else
                {
                    body << "sub rsp, 40\n";
                    body << "mov ecx, " << codeArg << '\n';
                    body << "call " << externName << '\n';
                    body << "add rsp, 40\n";
                }
            }
        }
    };
}

namespace Emux
{

NasmGenerator::NasmGenerator(Architecture arch)
    : m_Arch(arch)
{
}

int NasmGenerator::Generate(std::string_view code, std::string_view outputName)
{
    namespace fs = std::filesystem;

    resetState(m_Arch);

    std::string dir{};
    if (auto lastBar = outputName.find_last_of('/'); lastBar != outputName.npos)
    {
        dir = outputName.substr(0, lastBar + 1);
    }
    else if (auto lastBar = outputName.find_last_of('\\'); lastBar != outputName.npos)
    {
        dir = outputName.substr(0, lastBar + 1);
    }

    if (!dir.empty() && dir != outputName && !fs::exists(dir))   
    {
        fs::create_directories(dir);
    }

    Data data;

    if (m_Arch == Architecture::X86)
    {
        data.header << "global _main_Start\n";
        data.header << "global main_Start\n";
    }
    else
    {
        data.header << "default rel\n";
        data.header << "global main_Start\n";
    }
    
    std::istringstream ir(code.data());
    std::string line;
    while(std::getline(ir, line))
    {
        if (line.empty()) continue;

        auto [opcode, _] = split_one(line);
        if (auto it = instructions.find(opcode); it != instructions.end())
        {
            it->second(line, data);
        }
        else
        {
            data.bodies["text"] << line << '\n';
        }
    }

    std::string name(outputName);

    std::ofstream output(name + ".asm");
    output << data.header.str() << '\n';
    for (auto& body : data.bodies)
    {
        output << "section ." << body.first << '\n';
        output << body.second.str() << '\n';
    }
    output.close();

    output.open(name + ".exir");
    output << code;
    output.close();

    std::string nasmCmd;
    std::string gccCmd;

    if (m_Arch == Architecture::X86)
    {
        nasmCmd = std::format("nasm -f win32 -o \"{0}.obj\" \"{0}.asm\"", outputName);
        gccCmd  = std::format("gcc -m32 \"{0}.obj\" -o \"{0}.exe\" -nostdlib -e _main_Start -Wl,--entry=_main_Start -lkernel32", outputName);
    }
    else
    {
        nasmCmd = std::format("nasm -f win64 -o \"{0}.obj\" \"{0}.asm\"", outputName);
        gccCmd  = std::format("gcc \"{0}.obj\" -o \"{0}.exe\" -nostdlib -e main_Start -Wl,--entry=main_Start -lkernel32", outputName);
    }

    int r = system(nasmCmd.c_str());
    if (r != 0)
    {
        Logger::Error("NASM assembly failed with exit code " + std::to_string(r));
        return r;
    }

    r = system(gccCmd.c_str());
    if (r != 0)
    {
        Logger::Error("Linker (gcc) failed with exit code " + std::to_string(r));
        return r;
    }

    return r;
}

}
