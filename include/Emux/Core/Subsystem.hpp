#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace Emux
{

enum class Subsystem
{
    Console,
    Windows
};

inline std::optional<Subsystem> SubsystemFromString(std::string_view text)
{
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "console")
    {
        return Subsystem::Console;
    }
    if (lower == "windows" || lower == "gui")
    {
        return Subsystem::Windows;
    }

    return std::nullopt;
}

inline const char* SubsystemName(Subsystem subsystem)
{
    return subsystem == Subsystem::Windows ? "windows" : "console";
}

}
