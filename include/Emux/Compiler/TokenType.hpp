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
    Div,  // /
          
    LeftShift, // <<
    RightShift, // >>
    And, // &
    Or, // |
    Xor, // ^ 

    Assign, // = 

    // Controle
    NewLine, // \n
    EndOfFile, // \0

    Unknown
};

}
