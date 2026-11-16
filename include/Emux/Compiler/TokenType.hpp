#pragma once

namespace Emux
{

enum class TokenType
{
    // Palavras-chave. Cada uma tem o seu proprio valor: o parser compara
    // tipos em vez de textos. 'func' e 'var' foram removidos; 'static_cast'
    // e 'reinterpret_cast' sao operadores textualmente reservados.
    Return,
    Exir,
    Local,
    Global,
    Using,
    Enum,
    Struct,
    If,
    Else,
    While,
    For,
    Break,
    Continue,
    StaticCast,
    ReinterpretCast,

    // Identificadores
    Identifier,

    // Literais
    Number, // 0
    String, // ""

    // Símbolos
    Hash, // # (prefixo de diretiva do preprocessador)

    LeftParen, // (
    RightParen, // )

    LeftBracket, // [
    RightBracket, // ]

    LeftBrace, // {
    RightBrace, // }

    Colon, // :
    Comma, // ,
    Dot, // . (acesso a campo: base.campo)
    Pointer, // ->

    Plus, // +
    Minus, // -
    Mul, // *
    Div, // /
    Mod, // %
    LeftShift, // <<
    RightShift, // >>
    Less, // <
    Greater, // >
    LessEqual, // <=
    GreaterEqual, // >=
    Equal, // ==
    NotEqual, // !=
    And, // &
    Or, // |
    Xor, // ^
    Not, // ~
    LogicalNot, // !
    LogicalAnd, // &&
    LogicalOr, // ||

    Assign, // = 
    PlusAssign, // +=
    MinusAssign, // -=
    MulAssign, // *=
    DivAssign, // /=
    ModAssign, // %=
    AndAssign, // &=
    OrAssign, // |=
    XorAssign, // ^=
    LeftShiftAssign, // <<=
    RightShiftAssign, // >>=

    PlusPlus, // ++
    MinusMinus, // --

    Semicolon, // ;

    // Controle
    NewLine, // \n
    EndOfFile, // \0

    Unknown
};

// Verdadeiro para os tokens que sao palavras-chave. Usado na recuperacao de
// erros e onde um nome de atributo pode coincidir com uma palavra reservada.
inline bool IsKeyword(TokenType type)
{
    switch (type)
    {
        case TokenType::Return:
        case TokenType::Exir:
        case TokenType::Local:
        case TokenType::Global:
        case TokenType::Using:
        case TokenType::Enum:
        case TokenType::Struct:
        case TokenType::If:
        case TokenType::Else:
        case TokenType::While:
        case TokenType::For:
        case TokenType::Break:
        case TokenType::Continue:
        case TokenType::StaticCast:
        case TokenType::ReinterpretCast:
            return true;
        default:
            return false;
    }
}

}
