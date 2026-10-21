#pragma once
#include <Emux/Compiler/Token.hpp>
#include <optional>
#include <functional>

namespace Emux {


using OptionalToken =
    std::optional<
        std::reference_wrapper<const Token>
    >;

using TokenRef =
    std::reference_wrapper<const Token>;

}
