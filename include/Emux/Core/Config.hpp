#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <Emux/Core/Arch.hpp>

namespace Emux::Config
{
    struct Project
    {
        std::string name;
        std::string type;
        Architecture arch;
        std::filesystem::path output;
        std::vector<std::filesystem::path> include;
        std::vector<std::filesystem::path> sources;
    };

    std::optional<Project> Load(const std::filesystem::path& path);
}