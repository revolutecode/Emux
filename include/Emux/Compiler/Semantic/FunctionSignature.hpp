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
    // [[restrict]] por parametro; por omissao, usings restritos mantem a
    // restricao e tipos anonimos usam semelhanca estrutural.
    std::vector<bool> ParameterRestricted;
    Abi CallingConvention = Abi::Cdecl;
    SourceLocation Location;
    bool IsExported = true;
};

}