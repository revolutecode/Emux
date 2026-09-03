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
    Number,
    String,

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

    Assign, // = 

    // Controle
    NewLine, // \n
    EndOfFile, // \0

    Unknown
};

}
