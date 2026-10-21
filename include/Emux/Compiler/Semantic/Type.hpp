#pragma once

#include <cstdint>

namespace Emux
{

enum class TypeKind
{
    Unsigned,
    Signed,
    Float,
    Buffer
};


struct Type
{
    TypeKind Kind;

    std::uint32_t Bits;
};

}