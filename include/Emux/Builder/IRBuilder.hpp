#pragma once

#include "Emux/Builder/IBuilder.hpp"
#include <string>

namespace Emux
{

class Program;
class SectionNode;
class FunctionNode;

class IRBuilder : IBuilder
{
public:
    IRBuilder();
    ~IRBuilder();

    virtual std::string Build(const Program& program) override;    

private:
    std::string BuildSection(const SectionNode& node);
    std::string BuildFunction(const FunctionNode& node);
};

}
