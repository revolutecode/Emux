#include "Emux/Core/Arch.hpp"
#include <Emux/Core/Config.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <fstream>
#include <optional>

namespace fs = std::filesystem;

namespace Emux::Config
{
    std::optional<Project> Load(const fs::path& path)
    {
        std::ifstream file(path);

        if (!file.is_open())
            return std::nullopt;

        try
        {
            const auto json = nlohmann::json::parse(file);

            Project project;
            project.name = json.at("project").at("name").get<std::string>();
            project.type = json.at("project").at("type").get<std::string>();
            project.include = json.at("project").at("include").get<std::vector<fs::path>>();


            for (auto& p : project.include)
            {
                
                p = (fs::absolute(path).parent_path() / p).lexically_normal();
            }

            project.sources = json.at("project").at("sources").get<std::vector<fs::path>>();
            
            for (auto& p : project.sources)
            {
                p = (fs::absolute(path).parent_path() / p).lexically_normal();
            }

            project.output = json.at("build").at("output").get<fs::path>();
            if (project.output.is_relative())
            {
                project.output = (fs::absolute(path).parent_path() / project.output).lexically_normal();
            }
            std::string archStr = json.at("build").at("arch").get<std::string>();
            project.arch = Emux::ArchFromString(archStr);

            project.jobs = json.at("build").value("jobs", 0u);

            auto subsystem = SubsystemFromString(
                json.at("build").value("subsystem", std::string("console"))
            );
            if (!subsystem)
            {
                return std::nullopt;
            }
            project.subsystem = *subsystem;

            auto semicolons = SemicolonModeFromString(
                json.at("build").value("semicolons", std::string("optional"))
            );
            if (!semicolons)
            {
                return std::nullopt;
            }
            project.semicolons = *semicolons;

            return project;
        }
        catch (const nlohmann::json::exception&)
        {
            return std::nullopt;
        }
    }
}