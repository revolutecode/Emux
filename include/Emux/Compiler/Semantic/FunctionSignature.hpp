#pragma once

#include <Emux/Compiler/Type/Type.hpp>

#include <Emux/Compiler/SourceLocation.hpp>

#include <vector>

namespace Emux
{

struct FunctionSignature
{
    Type ReturnType;
    std::vector<Type> ParameterTypes;
    SourceLocation Location;
};

}