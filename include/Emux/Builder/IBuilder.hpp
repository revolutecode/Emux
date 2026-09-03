#pragma once

#include <string>
namespace Emux
{

class Program;

class IBuilder
{
    virtual std::string Build(const Program& program)=0;
};

}
