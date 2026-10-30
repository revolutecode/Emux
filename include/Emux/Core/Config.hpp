#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <Emux/Core/Arch.hpp>
#include <Emux/Core/Semicolon.hpp>
#include <Emux/Core/Subsystem.hpp>

namespace Emux::Config
{
    struct Project
    {
        std::string name;
        std::string type;
        Architecture arch;
        Subsystem subsystem = Subsystem::Console;
        SemicolonMode semicolons = SemicolonMode::Optional;
        unsigned jobs = 0;
        std::filesystem::path output;
        std::vector<std::filesystem::path> include;
        std::vector<std::filesystem::path> sources;
    };

    std::optional<Project> Load(const std::filesystem::path& path);
}