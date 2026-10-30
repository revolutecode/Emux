#include "Emux/Generator/NasmToolchain.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_set>

namespace Emux
{

int NasmToolchain::Assemble(
    const std::filesystem::path& asmPath,
    const std::filesystem::path& objPath,
    Architecture arch,
    const std::filesystem::path& logPath
)
{
    const std::string format = (arch == Architecture::X86) ? "win32" : "win64";

    std::string command =
        "nasm -f " + format +
        " -o \"" + objPath.string() + "\"" +
        " \"" + asmPath.string() + "\"" +
        " > \"" + logPath.string() + "\" 2>&1";

    return std::system(command.c_str());
}

int NasmToolchain::Link(
    const std::vector<std::filesystem::path>& objects,
    const std::filesystem::path& output,
    Architecture arch,
    Subsystem subsystem,
    const std::vector<std::string>& libraries,
    const std::filesystem::path& logPath
)
{
    std::string command = "gcc ";
    if (arch == Architecture::X86)
    {
        command += "-m32 ";
    }

    for (const std::filesystem::path& object : objects)
    {
        command += "\"" + object.string() + "\" ";
    }

    const std::string entry = (arch == Architecture::X86) ? "_emux_entry" : "emux_entry";

    command +=
        "-o \"" + output.string() + "\" " +
        "-nostdlib -e " + entry + " -Wl,--entry=" + entry +
        " -Wl,--subsystem," + SubsystemName(subsystem) +
        " -lkernel32";

    std::unordered_set<std::string> linked = { "kernel32" };
    for (const std::string& library : libraries)
    {
        std::string name = library;
        for (std::string_view suffix : { ".dll", ".lib", ".a" })
        {
            if (name.size() > suffix.size() && name.ends_with(suffix))
            {
                name.resize(name.size() - suffix.size());
            }
        }

        if (name.empty() || !linked.insert(name).second)
        {
            continue;
        }

        command += " -l" + name;
    }

    command += " > \"" + logPath.string() + "\" 2>&1";

    return std::system(command.c_str());
}

bool NasmToolchain::WriteEntryStub(
    const std::filesystem::path& asmPath,
    Architecture arch,
    std::string_view entry
)
{
    std::ofstream file(asmPath);
    if (!file)
    {
        return false;
    }

    if (arch == Architecture::X86)
    {
        file
            << "global _emux_entry\n"
            << "extern " << entry << "\n"
            << "extern _ExitProcess@4\n"
            << "section .text\n"
            << "_emux_entry:\n"
            << "call " << entry << "\n"
            << "push eax\n"
            << "call _ExitProcess@4\n";
    }
    else
    {
        file
            << "default rel\n"
            << "global emux_entry\n"
            << "extern " << entry << "\n"
            << "extern ExitProcess\n"
            << "section .text\n"
            << "emux_entry:\n"
            << "and rsp, -16\n"
            << "sub rsp, 32\n"
            << "call " << entry << "\n"
            << "mov ecx, eax\n"
            << "call ExitProcess\n";
    }

    return static_cast<bool>(file);
}

} // namespace Emux
