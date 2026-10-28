#pragma once

#include <string>

namespace Emux
{

struct Arguments
{
    std::string file;
    std::string output;
    std::string config;
    std::string arch;

    bool help = false;
    bool version = false;
};

Arguments ParseArguments(
    int argc,
    char** argv
);

}
