#pragma once

#include <string>


namespace Emux
{

struct Arguments
{
    std::string file;
    std::string output;

    bool help = false;
    bool version = false;
};


Arguments ParseArguments(
    int argc,
    char** argv
);


}
