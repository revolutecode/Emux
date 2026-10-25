#include "Emux/Generator/NasmGenerator.hpp"
#include "Emux/Generator/NasmContext.hpp"
#include "Emux/Generator/NasmInstructions.hpp"
#include "Emux/Core/Logger.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

namespace Emux
{

NasmGenerator::NasmGenerator(Architecture arch)
    : m_Arch(arch)
{
}

int NasmGenerator::Generate(std::string_view code, std::string_view outputName)
{
    namespace fs = std::filesystem;

    NasmContext ctx(m_Arch);

    // Garante que o diretorio de saida exista
    std::string dir;
    if (auto lastBar = outputName.find_last_of('/'); lastBar != std::string_view::npos)
    {
        dir = outputName.substr(0, lastBar + 1);
    }
    else if (auto lastBar = outputName.find_last_of('\\'); lastBar != std::string_view::npos)
    {
        dir = outputName.substr(0, lastBar + 1);
    }

    if (!dir.empty() && dir != outputName && !fs::exists(dir))
    {
        fs::create_directories(dir);
    }

    // Cabecalhos especificos por arquitetura
    if (m_Arch == Architecture::X86)
    {
        ctx.header << "global _main_Start\n";
        ctx.header << "global main_Start\n";
    }
    else
    {
        ctx.header << "default rel\n";
        ctx.header << "global main_Start\n";
    }

    // Traducao das instrucoes do IR
    std::istringstream ir(code.data());
    std::string line;
    while (std::getline(ir, line))
    {
        if (line.empty()) continue;

        if (!NasmInstructions::Dispatch(line, ctx))
        {
            ctx.bodies["text"] << line << '\n';
        }
    }

    std::string name(outputName);

    // Emissao do arquivo .asm
    std::ofstream output(name + ".asm");
    output << ctx.header.str() << '\n';
    for (auto& [sectionName, sectionBody] : ctx.bodies)
    {
        output << "section ." << sectionName << '\n';
        output << sectionBody.str() << '\n';
    }
    output.close();

    // Emissao do arquivo .exir para depuracao
    output.open(name + ".exir");
    output << code;
    output.close();

    // Invocacao do montador e linker
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

    int r = std::system(nasmCmd.c_str());
    if (r != 0)
    {
        Logger::Error("NASM assembly failed with exit code " + std::to_string(r));
        return r;
    }

    r = std::system(gccCmd.c_str());
    if (r != 0)
    {
        Logger::Error("Linker (gcc) failed with exit code " + std::to_string(r));
        return r;
    }

    return r;
}

} // namespace Emux
