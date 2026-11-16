#pragma once

#include "Emux/Compiler/TokenType.hpp"

#include <array>
#include <string_view>

namespace Emux
{

struct Keyword
{
    std::string_view Text;
    TokenType Type;
};

// Tabela unica de palavras reservadas. O lexer converte o texto no TokenType
// correspondente; o parser nunca precisa de comparar textos de palavras-chave.
// 'func' e 'var' ja nao existem: a linguagem usa 'global' e 'local'.
inline constexpr std::array<Keyword, 15> kKeywords{{
    { "return", TokenType::Return },
    { "exir", TokenType::Exir },
    { "local", TokenType::Local },
    { "global", TokenType::Global },
    { "using", TokenType::Using },
    { "enum", TokenType::Enum },
    { "struct", TokenType::Struct },
    { "if", TokenType::If },
    { "else", TokenType::Else },
    { "while", TokenType::While },
    { "for", TokenType::For },
    { "break", TokenType::Break },
    { "continue", TokenType::Continue },
    { "static_cast", TokenType::StaticCast },
    { "reinterpret_cast", TokenType::ReinterpretCast },
}};

} // namespace Emux
