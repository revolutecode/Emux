#include <Emux/CLI/Arguments.hpp>

#include "cxxopts.hpp"
#include <iostream>


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
        ("input","Input file", cxxopts::value<std::string>());
   
    options.parse_positional({"input"});

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << "\n";
        args.help = true;
        return args;
    }

    if (result.count("version"))
    {
        args.version = true;
        return args;
    }

    if (result.count("input"))
    {
        args.file = result["input"].as<std::string>();
    }

    return args;
}


}
