#pragma once

#include <Emux/Core/Arch.hpp>
#include <Emux/Core/Subsystem.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Emux
{

class NasmGenerator
{
public:
    explicit NasmGenerator(Architecture arch = Architecture::X86_64);

    // Traduz o IR de um módulo para assembly NASM, exportando os símbolos
    // definidos e declarando como externos os referenciados e não definidos.
    std::string Translate(std::string_view code);

    static int Assemble(
        const std::filesystem::path& asmPath,
        const std::filesystem::path& objPath,
        Architecture arch,
        const std::filesystem::path& logPath
    );

    static int Link(
        const std::vector<std::filesystem::path>& objects,
        const std::filesystem::path& output,
        Architecture arch,
        Subsystem subsystem,
        const std::vector<std::string>& libraries,
        const std::filesystem::path& logPath
    );

    static bool WriteEntryStub(
        const std::filesystem::path& asmPath,
        Architecture arch,
        std::string_view entry
    );

private:
    Architecture m_Arch = Architecture::X86_64;
};

}
