#include "Emux/Generator/NasmGenerator.hpp"
#include "Emux/Compiler/Semantic/Type.hpp"
#include "Emux/Compiler/Semantic/TypeParser.hpp"
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
        return {std::string(input.substr(0, pos)),pos};
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
                bool end = *it == separator && ((iss)? !isString : true);
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

    std::string mangled(std::string_view name, uint32_t size, bool yes = true)
    {
        std::string res(name);
        if (yes)
        {
            res.insert(res.begin(), '_');
            res += "@";
            res += std::to_string(size);
        }
        return res;
    }

    template<typename T=int>
    T convertString(std::string_view str)
    {
        std::stringstream sstr(str.data());
        T var;
        sstr >> var;
        return var;
    }

    std::string nasmSize(std::string_view type)
    {
        int bits = convertString<int>(type.substr(1));
        if (bits <= 8) return "byte";
        else if (bits <= 16) return "word";
        else if (bits <= 32) return "dword";
        else if (bits <= 64) return "qword";
        throw std::runtime_error("tipo maior que 64 bits não suportado no backend nasm");
    }

    std::string nasmSize(int bits)
    {
        if (bits <= 8) return "byte";
        else if (bits <= 16) return "word";
        else if (bits <= 32) return "dword";
        else if (bits <= 64) return "qword";
        throw std::runtime_error("tipo maior que 64 bits não suportado no backend nasm");
    }

    using Header = std::ostringstream;
    using Body = std::ostringstream;
    using Bodies = std::map<std::string,Body>;
    
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

    std::unordered_map<std::string, std::pair<ExternType, std::size_t>> externsEmitted;
    std::unordered_set<std::string> functionsEmitted;


    struct SlotInfo
    {
        bool isMemory;
        std::string memoryText;  // usado quando isMemory == true (já vem com nasmSize aplicado)
        std::size_t physIndex;   // usado quando isMemory == false (índice em physRegForms)
        int bits;                // largura declarada — usada como padrão se o uso não tiver sufixo :N    
    };

    std::unordered_map<std::string, SlotInfo> currentRegMap; // regs + args juntos
    std::size_t currentSpillCount = 0;
    bool frameEmitted = false;

    std::map<std::string, Emux::Type> variablesEmitted;

    void ensureFrame(Data& data)
    {
        if (!frameEmitted)
        {
            data.bodies["text"] << "push ebp\nmov ebp, esp\n";
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
        { "rr1", {"dl", "dx", "edx", "rdx"} }, // par de retorno 64 bits (edx:eax)
    };

    // A 4ª forma (64 bits) de cada tabela existe pra quando a Emux mirar x86-64;
    // no backend atual (win32, bits 32) ela é inatingível de propósito — o guard
    // abaixo barra bits>32 antes de indexar nela, com mensagem explicando o motivo,
    // em vez de deixar "rbx"/"rax" vazar pro nasm e falhar sem explicação.
    std::string sizedRegName(const std::vector<std::string>& forms, int bits, std::string_view baseName)
    {
        if (bits > 32)
            throw std::runtime_error(
                "'" + std::string(baseName) + "': backend x86-32 nao suporta registrador de " +
                std::to_string(bits)+" bits — decomponha em duas metades de 32 bits antes de chegar na Exir");

        int sizeIdx = (bits <= 8) ? 0 : (bits <= 16) ? 1 : 2;
        return forms.at(sizeIdx);
    }

    std::string RegOrVarOrValue(std::string_view input)
    {
        std::string str(input);

        auto colonPos = str.find(':');
        bool hasOverride = colonPos != std::string::npos;
        std::string name = hasOverride ? str.substr(0, colonPos) : str;
        int overrideBits = hasOverride ? std::stoi(str.substr(colonPos + 1)) : 0;

        if (auto it = reservedRegForms.find(name); it != reservedRegForms.end())
            return sizedRegName(it->second, hasOverride ? overrideBits : 32, name);

        if (auto it = currentRegMap.find(name); it != currentRegMap.end())
        {
            const SlotInfo& slot = it->second;
            if (slot.isMemory)
            {
                if (hasOverride)
                    Emux::Logger::Error("'"+name+"' é memória (spill/arg) — não aceita sufixo ':N', já tem tamanho fixo declarado");
                return slot.memoryText;
            }
            return sizedRegName(physRegForms.at(slot.physIndex), hasOverride ? overrideBits : slot.bits, name);
        }

        if (auto it = variablesEmitted.find(name); it != variablesEmitted.end())
        {
            if (hasOverride)
                Emux::Logger::Error("'"+name+"' é variável global — não aceita sufixo ':N', já tem tamanho fixo declarado");
            std::stringstream sstr;
            sstr << nasmSize(it->second.Bits);
            sstr << " [";
            sstr << name;
            sstr << "]";
            return sstr.str();
        }

        if (looksLikeReg(name))
            Emux::Logger::Error("registrador '" + name + "' usado sem ter sido declarado em 'regs' ou 'args'");

        return str; // imediato, label, ou registrador físico cru escrito direto em exir{}
    }

    std::pair<std::string, Emux::Type> ParseDecl(std::string_view input)
    {
        static Emux::TypeParser parser;

        auto [name, namePos] = split_one(input, ':');
        
        if (namePos == std::string::npos)
            Emux::Logger::Error("variável '"+name+"' sem tipo e bits declarado");

        auto type = parser.Parse(input.substr(namePos + 1));

        if (!type.has_value())
        {
            Emux::Logger::Error("Tipo invalido");
            return std::make_pair(name, Emux::Type{});
        }
        return std::make_pair(name, type.value());
    }

    void DoubleMathInstruction(std::string_view line, Data& data)
    {
        std::string correctLine(line.substr(line.find_first_of(' ') + 1));
        auto args = split(correctLine, ',');

        std::string strInst(line.substr(0u, line.find_first_of(' ')));

        if (args.size() < 2) Emux::Logger::Error("Instruction received quantity of args less than 2");
        
        data.bodies["text"]<<strInst<<" "<<RegOrVarOrValue(args[0])<< ", "<<RegOrVarOrValue(args[1])<<'\n';
    }
}

namespace Emux
{

using Func = void(*)(std::string_view,Data& data);
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
                    std::string memText = nasmSize(type.Bits) + " [ebp-" + std::to_string(4 * (++spillCount)) + "]";
                    currentRegMap[name] = { true, memText, 0, (int)type.Bits };
                }
            }
            currentSpillCount = spillCount;

            if (spillCount > 0)
            {
                ensureFrame(data);
                data.bodies["text"] << "sub esp, " << (4 * spillCount) << "\n";
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
                data.bodies["bss"] << name << " : res" << nasmSize(type.Bits)[0] << " 1\n"; 
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
                
                auto bytes = type.Bits+7/8;
                std::string memText = nasmSize(type.Bits) + " [ebp+" + std::to_string(8 + bytes * i) + "]";
                currentRegMap[name] = { true, memText, 0, (int)type.Bits };
            }

            ensureFrame(data);
        }
    },
    {"returns", [](std::string_view, Data&)
        {
            // rr0/rr1 já são fixos via reservedRegs — não emite nada.
            // Serve de contrato pro SemanticAnalyzer validar antes do lowering.
        }
    },
    // bytes <name>,<value>
    {"bytes", [](std::string_view line, Data& data)
        {
            std::string correctLine(line.substr(6u));
            
            auto args = split(correctLine, ',', true);

            std::string message{};
            for (auto it = ++args.begin(); it != args.end(); ++it)
            {
                message += (*it);
                message += ',';
            }
            message.pop_back();

            data.bodies["data"] << args[0] << " db " << message << '\n';
            data.bodies["data"] << args[0] << "_len" << " equ $ - " << args[0] << '\n';
        }
    },
    {"shl", &DoubleMathInstruction },
    {"shr", &DoubleMathInstruction },
    {"or", &DoubleMathInstruction },
    {"xor", &DoubleMathInstruction },
    {"and", &DoubleMathInstruction },
    // mov <reg or var>,<reg or var>
    {"movzx", &DoubleMathInstruction },
    {"movsx", &DoubleMathInstruction },
    {"mov", &DoubleMathInstruction },
    // add <reg or var>,<reg or var>
    {"add", &DoubleMathInstruction },
    // sub <reg or var>,<reg or var>
    {"sub", &DoubleMathInstruction },
    // mul <reg or var>,<reg or var>
    {"mul", [](std::string_view line, Data& data)
        {
            std::string correctLine(line.substr(4u));
            auto args = split(correctLine, ',');

            if (args.size() < 2) throw std::runtime_error("Invalid mul");
            data.bodies["text"] << "imul " << RegOrVarOrValue(args[0]) << ", " << RegOrVarOrValue(args[1]) << '\n';
        }
    },
    // div <reg or var>
    {"div", [](std::string_view line, Data& data)
        {
            std::string correctLine(line.substr(4u));
            auto args = split(correctLine, ',');

            if (args.size() < 1) throw std::runtime_error("Invalid div");
            data.bodies["text"] << "cdq\n"; // Extende EAX para EDX
            data.bodies["text"] << "idiv " << RegOrVarOrValue(args[0]) << '\n';
        }
    },
    // import <name>,<type>,<bytes>
    {"import", [](std::string_view line, Data& data)
        {
            std::string correctLine(line.substr(7u));
            auto [externName, pos] = split_one(correctLine, ',');
            auto [stype, newPos] = split_one(correctLine.substr(pos + 1), ',');
            auto [ssize, _] = split_one(correctLine.substr(pos + newPos + 2), ',');

            ExternType type;

            if (stype == "stdcall")
            {
                type = ExternType::STDCALL;
            } else if (stype == "cdecl")
            {
                type = ExternType::CDECL;
            }

            std::size_t size = convertString<std::size_t>(ssize);

            if (!externsEmitted.contains(externName))
            {
                externsEmitted[externName] = { type, size };
                data.header << "extern " << mangled(externName,size) << '\n';
            }
        }
    },
    // call <name>,[args]
    {"call", [](std::string_view line, Data& data)
        {
            std::string correctLine(line.substr(5u));
            auto [callName, callPos] = split_one(correctLine, ',');

            std::string resolved = RegOrVarOrValue(callName);
            bool isSolved = (resolved != callName);

            std::size_t size;
            if (externsEmitted.contains(callName))
            {
                size = externsEmitted[callName].second;
            }
            else if (functionsEmitted.contains(callName) || isSolved)
            {
                size = 0;
            }
            else
            {
                throw std::runtime_error("Invalid call: '"+callName +"' não é extern, função, variavel ou registrador conhecido");
            }

            if (correctLine.size() > callName.size())
            {
                auto args = split(correctLine.substr(callPos + 1), ',');
                std::reverse(args.begin(), args.end());

                for (const std::string& arg : args)
                    data.bodies["text"] << "push " << RegOrVarOrValue(arg) << '\n';
            }

            std::string target = isSolved ? resolved : mangled(callName, size, size > 0);
            data.bodies["text"] << "call " << target << '\n';
        }
    },
    // func <name>
    {"func", [](std::string_view line, Data& data)
        {
            std::string name(line.substr(5u));
            if (functionsEmitted.contains(name))
                Emux::Logger::Error("Function redefined");
            functionsEmitted.emplace(name);

            currentRegMap.clear();
            currentSpillCount = 0;
            frameEmitted = false;

            data.bodies["text"] << name << ":\n";
        }
    },
    // ret
    {"ret", [](std::string_view, Data& data)
        {
            if (frameEmitted)
                data.bodies["text"] << "mov esp, ebp\npop ebp\n";
            data.bodies["text"] << "ret\n";
        }
    },
    // exit <code>
    {"exit", [](std::string_view line, Data& data)
        {
            std::string externName = "ExitProcess";
            if (!externsEmitted.contains(externName))
            {
                externsEmitted[externName] = { ExternType::STDCALL, 4 };
                data.header << "extern " << mangled(externName,4) << '\n';
            }
            
            auto& body = data.bodies["text"];
            body << "push " << RegOrVarOrValue(line.substr(5u)) << '\n';
            body << "call " << mangled(externName,4) << '\n';
        }
    }
};

int NasmGenerator::Generate(std::string_view code, std::string_view outputName)
{
    namespace fs = std::filesystem;


    std::string dir{};
    if (auto lastBar = outputName.find_last_of('/'); lastBar != outputName.npos)
    {
        dir = outputName.substr(0, lastBar + 1);
    } else if (auto lastBar = outputName.find_last_of('\\'); lastBar != outputName.npos)
    {
        dir = outputName.substr(0, lastBar + 1);
    }

    if (dir != outputName && !fs::exists(dir))   
        fs::create_directories(dir);

    Data data;
    data.header << "global Main_Start\n";
    
    std::istringstream ir(code.data());
    std::string line;
    while(std::getline(ir,line))
    {
        if (line.empty()) continue;

        auto [ opcode, _ ] = split_one(line);
        if (auto it = instructions.find(opcode); it != instructions.end())
        {
            it->second(line, data);
        } else {
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

    int r=system(std::format("nasm -f win32 -o {0}.obj {0}.asm", outputName).c_str());
    
    if (r != 0)
    {
        return r;
    }

    r = system(std::format(
        "gcc {0}.obj -o {0}.exe -nostdlib -e Main_Start -Wl,--entry=Main_Start -lkernel32",
        outputName).c_str()
    );

    return r;
}

}
