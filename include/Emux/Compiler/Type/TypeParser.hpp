#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "Type.hpp"

namespace Emux
{

class TypeParser
{
public:

    // pointerBits define a largura dos ponteiros nesta arquitetura.
    static std::optional<Type> Parse(
        std::string_view text,
        std::uint32_t pointerBits = 64
    );

};

}