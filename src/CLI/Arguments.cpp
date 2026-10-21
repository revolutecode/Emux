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
        ("a,arch", "Target architecture: x86 or x86_64", cxxopts::value<std::string>()->default_value("x86_64"))
        ("o,output", "Output file", cxxopts::value<std::string>())
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
        std::string archStr = result["arch"].as<std::string>();
        std::transform(archStr.begin(), archStr.end(), archStr.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (archStr == "x86" || archStr == "32" || archStr == "i386")
        {
            args.arch = Architecture::X86;
        }
        else if (archStr == "x86_64" || archStr == "x86-64" || archStr == "x64" || archStr == "64" || archStr == "amd64")
        {
            args.arch = Architecture::X86_64;
        }
    }

    if (result.count("input"))
    {
        args.file = result["input"].as<std::string>();
    }

    if (result.count("output"))
    {
        args.output = result["output"].as_optional<std::string>().value_or("");
    }

    if (args.output.empty())
    {
        if (!args.file.empty())
        {
            std::filesystem::path p(args.file);
            args.output = p.stem().string();
        }
        else
        {
            args.output = "out";
        }
    }

    return args;
}

}
