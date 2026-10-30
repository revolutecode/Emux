#pragma once

#include <Emux/Core/Arch.hpp>
#include <Emux/Core/Subsystem.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Emux
{

// Invólucro das ferramentas externas (nasm + linker) usado por todos os
// backends. Não conhece a Exir nem a arquitetura do gerador.
class NasmToolchain
{
public:
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
};

} // namespace Emux
