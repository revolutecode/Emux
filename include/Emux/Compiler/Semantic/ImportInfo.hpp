#pragma once

#include <Emux/Core/Abi.hpp>

#include <cstddef>
#include <string>

namespace Emux
{

// Metadados de uma função nativa declarada com [[import(...)]].
struct ImportInfo
{
    std::string Symbol;     // símbolo no objecto/import library
    std::string Library;    // biblioteca a ligar (-l<Library>)
    Abi CallingConvention = Abi::Cdecl;
    std::size_t StackSize = 0; // bytes de argumentos, para decoração e limpeza
};

}
