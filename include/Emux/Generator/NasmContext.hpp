#pragma once

#include "Emux/CLI/Arguments.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Core/Abi.hpp"
#include "Emux/Core/Arch.hpp"
#include <cstddef>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Emux
{

struct ExternInfo
{
    Abi CallingConvention = Abi::Cdecl;
    std::size_t Size = 0;
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

    std::unordered_map<std::string, ExternInfo> externsEmitted;
    // ABI de funções conhecidas (inclusive de outros módulos).
    std::unordered_map<std::string, ExternInfo> callAbis;
    Abi currentFuncAbi = Abi::Cdecl;
    std::size_t currentFuncStack = 0;
    bool needsUId64Helper = false;
    bool needsId64Helper = false;
    std::unordered_set<std::string> functionsEmitted;
    std::unordered_set<std::string> dataLabelsEmitted;
    // Símbolos chamados ou referenciados que podem vir de outro módulo.
    std::unordered_set<std::string> callTargets;
    std::unordered_set<std::string> referencedSymbols;
    std::unordered_map<std::string, SlotInfo> currentRegMap;
    std::map<std::string, Type> variablesEmitted;
    // Globais declaradas em outro módulo (diretiva 'extvars').
    std::map<std::string, Type> externalVariables;
    // Nomes nativos adicionais para símbolos definidos aqui (diretiva 'export').
    std::map<std::string, std::vector<std::string>> exportAliases;

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
        callAbis.clear();
        currentFuncAbi = Abi::Cdecl;
        currentFuncStack = 0;
        needsUId64Helper = false;
        needsId64Helper = false;
        functionsEmitted.clear();
        dataLabelsEmitted.clear();
        callTargets.clear();
        referencedSymbols.clear();
        currentRegMap.clear();
        variablesEmitted.clear();
        externalVariables.clear();
        exportAliases.clear();
        currentSpillCount = 0;
        frameEmitted = false;
    }
};

} // namespace Emux
