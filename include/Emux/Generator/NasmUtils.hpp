#pragma once

#include "Emux/CLI/Arguments.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Core/Abi.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <Emux/Core/Arch.hpp>

namespace Emux
{

namespace NasmUtils
{

std::pair<std::string, std::size_t> SplitOne(std::string_view input, char separator = ' ');

std::vector<std::string> Split(std::string_view input, char separator = ' ', bool respectQuotes = false);

std::string Mangled(std::string_view name, uint32_t size, Abi abi, Architecture arch);

// win32 (x86) decora símbolos com sublinhado; x64 não.
std::string Exported(std::string_view name, Architecture arch);

std::string NasmSize(int bits);

std::pair<std::string, Type> ParseDecl(std::string_view input, uint32_t pointerBits = 64);

template<typename T = int>
T ConvertString(std::string_view str)
{
    std::stringstream sstr(str.data());
    T var{};
    sstr >> var;
    return var;
}

} // namespace NasmUtils

} // namespace Emux
