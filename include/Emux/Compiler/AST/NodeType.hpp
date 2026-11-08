#pragma once

namespace Emux
{

enum class NodeType
{
    Program,
    Using,
    Variable,
    // Declaracao 'local' no corpo de uma funcao (slot da frame).
    Local,
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
    If,
    While,
    For,
    IR
};

}
