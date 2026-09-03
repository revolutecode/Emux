#pragma once

namespace Emux
{

enum class NodeType
{
    Program,
    Section,
    Variable,
    VariableCall,
    Function,
    FunctionCall,
    Assign,
    Binary,
    Literal,
    Return,
    IR
};

}
