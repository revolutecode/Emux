#pragma once

#include <string>


namespace Emux
{

struct Arguments
{
    std::string file;

    bool help = false;
    bool version = false;
};


Arguments ParseArguments(
    int argc,
    char** argv
);


}
