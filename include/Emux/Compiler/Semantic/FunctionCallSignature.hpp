#pragma once

#include <Emux/Compiler/Type/Type.hpp>

#include <Emux/Compiler/SourceLocation.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace Emux
{

struct FunctionCallSignature
{
    std::vector<Type> ParameterTypes;
    // Valor de literais inteiros nao negativos, para permitir estreitar
    // literais (ex: 1 para bool/u1) sem abrir conversoes implicitas.
    std::vector<std::optional<std::int64_t>> LiteralValues;
    SourceLocation Location;
};

}
