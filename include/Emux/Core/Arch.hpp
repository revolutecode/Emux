#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace Emux
{

enum class Architecture
{
    X86,
    X86_64
};

inline constexpr std::uint32_t PointerBits(Architecture arch)
{
    return arch == Architecture::X86 ? 32u : 64u;
}

inline constexpr Architecture ArchFromString(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(),
    [](unsigned char c) { return std::tolower(c); });

    if (str == "x86" || str == "32" || str == "i386")
    {
        return Architecture::X86;
    }
    else if (str == "x86_64" || str == "x86-64" || str == "x64" || str == "64" || str == "amd64")
    {
        return Architecture::X86_64;
    }

    return static_cast<Architecture>(0);
}

}