#include <Emux/CLI/Arguments.hpp>

#include "cxxopts.hpp"
#include <iostream>
#include <algorithm>
#include <filesystem>

namespace Emux
{

Arguments ParseArguments(
    int argc,
    char** argv
)
{
    Arguments args;

    cxxopts::Options options("Emux", "Emux compiler");

    options.add_options()
        ("h,help", "Show help")
        ("v,version", "Show version")
        ("a,arch", "Target architecture: x86 or x86_64", cxxopts::value<std::string>())
        ("s,subsystem", "PE subsystem: console or windows", cxxopts::value<std::string>())
        ("semicolons", "Semicolon requirement: optional or required", cxxopts::value<std::string>())
        ("o,output", "Output file", cxxopts::value<std::string>())
        ("j,jobs", "Parallel jobs (default: hardware threads)", cxxopts::value<unsigned>())
        ("force", "Rebuild all modules, ignoring the cache")
        ("c,config", "Config file", cxxopts::value<std::string>()->default_value("./emux.json"))
        ("input", "Input file", cxxopts::value<std::string>());
   
    options.parse_positional({"input"});

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << '\n';
        args.help = true;
        return args;
    }

    if (result.count("version"))
    {
        args.version = true;
        return args;
    }

    if (result.count("arch"))
    {
        args.arch = result["arch"].as<std::string>();
    }

    if (result.count("subsystem"))
    {
        args.subsystem = result["subsystem"].as<std::string>();
    }

    if (result.count("semicolons"))
    {
        args.semicolons = result["semicolons"].as<std::string>();
    }

    if (result.count("input"))
    {
        args.file = result["input"].as<std::string>();
    }

    if (result.count("config"))
    {
        args.config = result["config"].as<std::string>();
    }

    if (result.count("output"))
    {
        args.output = result["output"].as<std::string>();
    }

    if (result.count("jobs"))
    {
        args.jobs = result["jobs"].as<unsigned>();
    }

    if (result.count("force"))
    {
        args.force = true;
    }

    return args;
}

}
