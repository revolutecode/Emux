#pragma once

#include <Emux/Compiler/Type/Type.hpp>
#include <Emux/Core/Abi.hpp>

#include <Emux/Compiler/SourceLocation.hpp>

#include <vector>

namespace Emux
{

struct FunctionSignature
{
    Type ReturnType;
    std::vector<Type> ParameterTypes;
    Abi CallingConvention = Abi::Cdecl;
    SourceLocation Location;
};

}