#pragma once

#include "Emux/Compiler/CompilerContext.hpp"

namespace Emux
{

class Preprocessor
{
public:
    Preprocessor(CompilerContext& context);

    void Process();
private:

    CompilerContext& m_Context;
};

}