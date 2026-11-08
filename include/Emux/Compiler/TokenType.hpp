#pragma once

namespace Emux
{

enum class TokenType
{
    // palavra-chave
    Keyword,

    // Identificadores
    Identifier,

    // Literais
    Number, // 0
    String, // ""

    // Símbolos
    At, // @

    LeftParen, // (
    RightParen, // )

    LeftBracket, // [
    RightBracket, // ]

    LeftBrace, // {
    RightBrace, // }

    Colon, // :
    Comma, // ,
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

}
