#pragma once

#include <Emux/Compiler/Type/Type.hpp>

#include <Emux/Compiler/SourceLocation.hpp>

#include <vector>

namespace Emux
{

namespace  {
    
}

struct FunctionCallSignature
{
    std::vector<Type> ParameterTypes;
    SourceLocation Location;
};

}