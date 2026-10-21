#pragma once
#include <vector>
#include <cstdint>

#include <Emux/Compiler/CompilerContext.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{


class Lexer
{

public:

    Lexer(
        CompilerContext& context
    );


    std::vector<Token> Tokenize();


private:

    char Current() const;
    char Peek() const;
    char PeekNext() const;

    void Advance();

    Token ScanToken();
    Token ScanIdentifierOrKeyword();
    Token ScanNumber();
    Token ScanString();

    void SkipLineComment();
    void SkipBlockComment();

private:

    CompilerContext& m_Context;

    size_t m_Position = 0;

    uint32_t m_Line = 1;

    uint32_t m_Column = 1;

};

};