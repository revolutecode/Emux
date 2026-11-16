#pragma once

namespace Emux
{

enum class NodeType
{
    Program,
    Using,
    // Declaracao '<nome> : <tipo> [= valor]'. StorageClass distingue a regiao
    // estatica (global) do slot da frame (local).
    Declaration,
    VariableCall,
    Function,
    FunctionCall,
    Assign,
    Binary,
    Unary,
    Index,
    // base.campo: acesso a um campo de struct.
    MemberAccess,
    // Literal agregado anonimo '{ a, b, ... }' para inicializar/atribuir structs.
    Aggregate,
    Cast,
    Enum,
    // Declaracao de struct (tipo nominal com layout de campos).
    Struct,
    Literal,
    Return,
    // Corpo '{ ... }' de uma construcao de controlo de fluxo.
    Block,
    If,
    While,
    For,
    Break,
    Continue,
    IR
};

}
