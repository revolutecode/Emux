#pragma once

#include <string>
namespace Emux
{

class Program;

class IBuilder
{
public:
    virtual ~IBuilder() = default;
    virtual std::string Build(const Program& program)=0;
};

}
