#include "Emux/Generator/NasmGenerator.hpp"
#include "Emux/Generator/NasmContext.hpp"
#include "Emux/Generator/NasmInstructions.hpp"
#include "Emux/Generator/NasmUtils.hpp"
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

int NasmGenerator::Generate(std::string_view code, const std::filesystem::path& output)
{
    namespace fs = std::filesystem;

    NasmContext ctx(m_Arch);

    // O output e a base do nome: "bin/app" gera bin/app.asm, bin/app.obj e bin/app.exe
    fs::path base = output;
    const std::string ext = base.extension().string();
    if (ext == ".exe" || ext == ".obj" || ext == ".asm" || ext == ".exir")
    {
        base.replace_extension();
    }

    auto parent = base.parent_path();
    if (!parent.empty() && !fs::exists(parent))
    {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec)
        {
            Logger::Error("Could not create output directory - " + parent.string());
            return 1;
        }
    }

    const std::string basePath = base.string();

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

    // Ponto de entrada real: chama Start/_Start e encerra com o valor retornado
    std::string entry;
    for (const std::string& candidate : { std::string("main_Start"), std::string("main__Start") })
    {
        if (ctx.functionsEmitted.contains(candidate))
        {
            entry = candidate;
            break;
        }
    }

    if (entry.empty())
    {
        Logger::Error("Entry point function 'Start' or '_Start' was not emitted.");
        return 1;
    }

    if (!ctx.externsEmitted.contains("ExitProcess"))
    {
        ctx.externsEmitted["ExitProcess"] = { ExternType::STDCALL, 4 };
        ctx.header << "extern " << NasmUtils::Mangled("ExitProcess", 4, true, m_Arch) << '\n';
    }

    auto& text = ctx.bodies["text"];
    if (m_Arch == Architecture::X86)
    {
        text << "_emux_entry:\n";
        text << "call _" << entry << '\n';
        text << "push eax\n";
        text << "call " << NasmUtils::Mangled("ExitProcess", 4, true, m_Arch) << '\n';
    }
    else
    {
        text << "emux_entry:\n";
        text << "and rsp, -16\n";
        text << "sub rsp, 32\n";
        text << "call " << entry << '\n';
        text << "mov ecx, eax\n";
        text << "call ExitProcess\n";
    }

    // Emissao do arquivo .asm
    std::ofstream outputFile(basePath + ".asm");
    if (!outputFile)
    {
        Logger::Error("Could not open output file - " + basePath + ".asm");
        return 1;
    }

    if (m_Arch == Architecture::X86)
    {
        outputFile << "global _emux_entry\n";
        if (ctx.functionsEmitted.contains("main_Start")) outputFile << "global _main_Start\n";
        if (ctx.functionsEmitted.contains("main__Start")) outputFile << "global _main__Start\n";
    }
    else
    {
        outputFile << "default rel\n";
        outputFile << "global emux_entry\n";
        if (ctx.functionsEmitted.contains("main_Start")) outputFile << "global main_Start\n";
        if (ctx.functionsEmitted.contains("main__Start")) outputFile << "global main__Start\n";
    }

    outputFile << ctx.header.str() << '\n';
    for (auto& [sectionName, sectionBody] : ctx.bodies)
    {
        outputFile << "section ." << sectionName << '\n';
        outputFile << sectionBody.str() << '\n';
    }
    outputFile.close();

    // Emissao do arquivo .exir para depuracao
    outputFile.open(basePath + ".exir");
    if (!outputFile)
    {
        Logger::Error("Could not open output file - " + basePath + ".exir");
        return 1;
    }
    outputFile << code;
    outputFile.close();

    // Invocacao do montador e linker
    std::string nasmCmd;
    std::string gccCmd;

    if (m_Arch == Architecture::X86)
    {
        nasmCmd = std::format("nasm -f win32 -o \"{0}.obj\" \"{0}.asm\"", basePath);
        gccCmd  = std::format("gcc -m32 \"{0}.obj\" -o \"{0}.exe\" -nostdlib -e _emux_entry -Wl,--entry=_emux_entry -lkernel32", basePath);
    }
    else
    {
        nasmCmd = std::format("nasm -f win64 -o \"{0}.obj\" \"{0}.asm\"", basePath);
        gccCmd  = std::format("gcc \"{0}.obj\" -o \"{0}.exe\" -nostdlib -e emux_entry -Wl,--entry=emux_entry -lkernel32", basePath);
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
