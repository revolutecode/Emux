#pragma once

#include <string>

#include <Emux/Compiler/TokenType.hpp>
#include <Emux/Compiler/SourceLocation.hpp>


namespace Emux
{

struct Token
{
    TokenType Type;

    std::string Text;

    SourceLocation Location;
};

}