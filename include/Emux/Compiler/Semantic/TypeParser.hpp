#pragma once

#include <optional>
#include <string_view>

#include "Type.hpp"

namespace Emux
{

class TypeParser
{
public:

    static std::optional<Type> Parse(
        std::string_view text
    );

};

}