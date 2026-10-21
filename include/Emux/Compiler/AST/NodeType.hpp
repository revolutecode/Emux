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
    Literal,
    Return,
    IR
};

}
