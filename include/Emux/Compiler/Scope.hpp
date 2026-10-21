#pragma once

#include "Emux/Compiler/AttrSet.hpp"
#include "Emux/Compiler/Token.hpp"

namespace Emux
{

struct Scope
{
    Token Name;
    AttrSet Attrs;    
};

}
