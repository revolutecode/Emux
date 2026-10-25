#pragma once

namespace Emux
{

enum class NodeType
{
    Program,
    Variable,
    VariableCall,
    Function,
    FunctionCall,
    Assign,
    Binary,
    Unary,
    Literal,
    Return,
    IR
};

}
