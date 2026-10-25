#pragma once

#include "Emux/Compiler/Token.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

namespace Emux
{

class ExprParser
{
public:
    explicit ExprParser(const std::vector<Token>& tokens);

    int64_t Parse();
    static int64_t Evaluate(const std::vector<Token>& tokens);

private:
    const Token* Peek() const;
    const Token* Advance();
    bool Match(std::string_view text);

    int64_t ParseLogicalOr();
    int64_t ParseLogicalAnd();
    int64_t ParseBitwiseOr();
    int64_t ParseBitwiseXor();
    int64_t ParseBitwiseAnd();
    int64_t ParseEquality();
    int64_t ParseRelational();
    int64_t ParseShift();
    int64_t ParseAdditive();
    int64_t ParseMultiplicative();
    int64_t ParseUnary();
    int64_t ParsePrimary();

private:
    const std::vector<Token>& m_Tokens;
    size_t m_Pos = 0;
};

}
