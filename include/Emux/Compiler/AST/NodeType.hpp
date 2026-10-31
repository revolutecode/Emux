#pragma once

namespace Emux
{

enum class NodeType
{
    Program,
    Using,
    Variable,
    VariableCall,
    Function,
    FunctionCall,
    Assign,
    Binary,
    Unary,
    Cast,
    Literal,
    Return,
    IR
};

}
