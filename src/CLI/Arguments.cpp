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
        ("o,output", "Output file", cxxopts::value<std::string>())
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

    return args;
}

}
