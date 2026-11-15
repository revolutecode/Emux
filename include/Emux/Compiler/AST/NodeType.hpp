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
    // base.campo: acesso a um campo de struct.
    MemberAccess,
    Cast,
    Enum,
    // Declaracao de struct (tipo nominal com layout de campos).
    Struct,
    Literal,
    Return,
    If,
    While,
    For,
    Break,
    Continue,
    IR
};

}
