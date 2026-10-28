#pragma once

#include "Emux/CLI/Arguments.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Core/Arch.hpp"
#include <cstddef>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Emux
{

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
    bool isSigned = false;
};

struct NasmContext
{
    Architecture arch = Architecture::X86_64;

    std::ostringstream header;
    std::map<std::string, std::ostringstream> bodies;

    std::unordered_map<std::string, std::pair<ExternType, std::size_t>> externsEmitted;
    std::unordered_set<std::string> functionsEmitted;
    std::unordered_set<std::string> dataLabelsEmitted;
    std::unordered_map<std::string, SlotInfo> currentRegMap;
    std::map<std::string, Type> variablesEmitted;

    std::size_t currentSpillCount = 0;
    bool frameEmitted = false;

    explicit NasmContext(Architecture targetArch = Architecture::X86_64)
        : arch(targetArch)
    {
    }

    void EnsureFrame()
    {
        if (!frameEmitted)
        {
            if (arch == Architecture::X86)
            {
                bodies["text"] << "push ebp\nmov ebp, esp\n";
            }
            else
            {
                bodies["text"] << "push rbp\nmov rbp, rsp\n";
            }
            frameEmitted = true;
        }
    }

    void Reset(Architecture newArch)
    {
        arch = newArch;
        header.str("");
        header.clear();
        bodies.clear();
        externsEmitted.clear();
        functionsEmitted.clear();
        dataLabelsEmitted.clear();
        currentRegMap.clear();
        variablesEmitted.clear();
        currentSpillCount = 0;
        frameEmitted = false;
    }
};

} // namespace Emux
