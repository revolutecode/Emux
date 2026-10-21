#pragma once

#include <string>

namespace Emux
{

enum class Architecture
{
    X86,
    X86_64
};

struct Arguments
{
    std::string file;
    std::string output;
    Architecture arch = Architecture::X86_64;

    bool help = false;
    bool version = false;
};

Arguments ParseArguments(
    int argc,
    char** argv
);

}
