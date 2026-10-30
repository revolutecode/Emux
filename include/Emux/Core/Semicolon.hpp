#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace Emux
{

enum class SemicolonMode
{
    Optional,
    Required
};

inline std::optional<SemicolonMode> SemicolonModeFromString(std::string_view text)
{
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "optional" || lower == "off" || lower == "false")
    {
        return SemicolonMode::Optional;
    }
    if (lower == "required" || lower == "mandatory" || lower == "on" || lower == "true")
    {
        return SemicolonMode::Required;
    }

    return std::nullopt;
}

inline const char* SemicolonModeName(SemicolonMode mode)
{
    return mode == SemicolonMode::Required ? "required" : "optional";
}

}
