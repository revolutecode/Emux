#pragma once

#include "Emux/Core/Arch.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace Emux
{

enum class Abi
{
    Cdecl,
    Stdcall,
    Fastcall
};

inline std::string_view AbiName(Abi abi)
{
    switch (abi)
    {
        case Abi::Stdcall:  return "stdcall";
        case Abi::Fastcall: return "fastcall";
        default:            return "cdecl";
    }
}

inline std::optional<Abi> AbiFromString(std::string_view name)
{
    if (name == "cdecl")    return Abi::Cdecl;
    if (name == "stdcall")  return Abi::Stdcall;
    if (name == "fastcall") return Abi::Fastcall;
    return std::nullopt;
}

// Bytes que o próprio chamado remove da pilha ao retornar (ret N).
// Em x64 a limpeza é sempre do chamador. Em x86 fastcall os dois primeiros
// argumentos que cabem em 32 bits vão em ecx/edx e não ocupam a pilha.
inline std::size_t AbiStackBytes(
    Abi abi,
    const std::vector<std::uint32_t>& parameterBytes,
    Architecture arch
)
{
    if (arch != Architecture::X86 || abi == Abi::Cdecl)
    {
        return 0;
    }

    std::size_t total = 0;
    int registerSlots = 0;

    for (const std::uint32_t bytes : parameterBytes)
    {
        if (abi == Abi::Fastcall && registerSlots < 2 && bytes <= 4)
        {
            ++registerSlots;
            continue;
        }

        total += bytes <= 4 ? 4 : 8;
    }

    return total;
}

}
