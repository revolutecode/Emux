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
    std::string subsystem;

    unsigned jobs = 0;
    bool force = false;
    bool help = false;
    bool version = false;
};

Arguments ParseArguments(
    int argc,
    char** argv
);

}
