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
    Index,
    Cast,
    Enum,
    Literal,
    Return,
    IR
};

}
