#include "Emux/Generator/NasmGenerator.hpp"
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <filesystem>

namespace
{
    std::string split_one(std::string input, char separator = ' ')
    {
        auto pos = input.find_first_of(separator);
        return input.substr(0, pos);
    }

    std::string replaceDoublePoints(std::string text)
    {
        const std::string alvo = "::";
        const std::string novo = "_";
        size_t pos = 0;

        while((pos = text.find(alvo, pos)) != std::string::npos)
        {
            text.replace(pos,alvo.length(),novo);
            pos += novo.length();
        }

        return text;
    }

    std::string mangled(std::string_view name, bool a = true)
    {
        std::string res(name);
        if (a)
        {
            res.insert(res.begin(), '_');
            res += "@4";
        }
        return res;
    }
}

namespace Emux
{

std::set<std::string> externsEmitted;

using Func = void(*)(std::string_view,std::stringstream&,std::stringstream&);
const std::unordered_map<std::string, Func> instructions
{
    {"ret", [](std::string_view line, std::stringstream& body, std::stringstream&){
        body << "ret\n";
    }},
    {"exit", [](std::string_view line, std::stringstream& body, std::stringstream& header)
        {
            std::string externName = "ExitProcess";
            if (externsEmitted.insert(externName).second)
            {
                header << "extern " << mangled(externName) << '\n';
            }
            
            body << "push " << line.substr(5u) << '\n';
            body << "call " << mangled(externName) << '\n';
        }
    }
};

int NasmGenerator::Generate(std::string_view code, std::string_view outputName)
{
    std::string name(outputName);
    namespace fs = std::filesystem;
    if (!fs::exists("build/nasm"))         
        fs::create_directories("build/nasm");

    std::stringstream header, body;

    header << "global Start\n";
    body << "section .text\n";
    
    std::istringstream ir(code.data());
    std::string line;
    while(std::getline(ir,line))
    {
        if (line.empty()) continue;

        std::string opcode = split_one(line);
        if (auto it = instructions.find(opcode); it != instructions.end())
        {
            it->second(line, body, header);
        } else {
            std::string correctLine = replaceDoublePoints(line);
            bool inStart = (correctLine.compare("Main_Start:") == 0);
            
            body << ((inStart) ? "Start:" : correctLine) << '\n';
        }
    }

    std::ofstream output("build/nasm/" + name + ".asm");
    output << header.str() << "\n" << body.str();
    output.close();

    std::string cmd = std::format("nasm -f win32 -o build/nasm/{0}.obj build/nasm/{0}.asm", outputName);
    system(cmd.c_str());
    cmd.clear();
    std::format_to(std::back_inserter(cmd), "gcc build/nasm/{0}.obj -o build/nasm/{0}.exe -nostdlib -e Start -Wl,--entry=Start -lkernel32", outputName);
    system(cmd.c_str());
    
    return EXIT_SUCCESS;
}

}
