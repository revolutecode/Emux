#include "Emux/Compiler/Parser/ExprParser.hpp"
#include <string>

namespace Emux
{

ExprParser::ExprParser(const std::vector<Token>& tokens)
    : m_Tokens(tokens)
{
}

int64_t ExprParser::Parse()
{
    if (m_Tokens.empty()) return 0;
    return ParseLogicalOr();
}

int64_t ExprParser::Evaluate(const std::vector<Token>& tokens)
{
    ExprParser parser(tokens);
    return parser.Parse();
}

const Token* ExprParser::Peek() const
{
    if (m_Pos < m_Tokens.size()) return &m_Tokens[m_Pos];
    return nullptr;
}

const Token* ExprParser::Advance()
{
    if (m_Pos < m_Tokens.size()) return &m_Tokens[m_Pos++];
    return nullptr;
}

bool ExprParser::Match(std::string_view text)
{
    if (m_Pos < m_Tokens.size() && m_Tokens[m_Pos].Text == text)
    {
        ++m_Pos;
        return true;
    }
    return false;
}

int64_t ExprParser::ParseLogicalOr()
{
    int64_t left = ParseLogicalAnd();
    while (Match("||"))
    {
        int64_t right = ParseLogicalAnd();
        left = (left != 0 || right != 0) ? 1 : 0;
    }
    return left;
}

int64_t ExprParser::ParseLogicalAnd()
{
    int64_t left = ParseBitwiseOr();
    while (Match("&&"))
    {
        int64_t right = ParseBitwiseOr();
        left = (left != 0 && right != 0) ? 1 : 0;
    }
    return left;
}

int64_t ExprParser::ParseBitwiseOr()
{
    int64_t left = ParseBitwiseXor();
    while (Match("|"))
    {
        int64_t right = ParseBitwiseXor();
        left = left | right;
    }
    return left;
}

int64_t ExprParser::ParseBitwiseXor()
{
    int64_t left = ParseBitwiseAnd();
    while (Match("^"))
    {
        int64_t right = ParseBitwiseAnd();
        left = left ^ right;
    }
    return left;
}

int64_t ExprParser::ParseBitwiseAnd()
{
    int64_t left = ParseEquality();
    while (Match("&"))
    {
        int64_t right = ParseEquality();
        left = left & right;
    }
    return left;
}

int64_t ExprParser::ParseEquality()
{
    int64_t left = ParseRelational();
    while (true)
    {
        if (Match("=="))
        {
            int64_t right = ParseRelational();
            left = (left == right) ? 1 : 0;
        }
        else if (Match("!="))
        {
            int64_t right = ParseRelational();
            left = (left != right) ? 1 : 0;
        }
        else
        {
            break;
        }
    }
    return left;
}

int64_t ExprParser::ParseRelational()
{
    int64_t left = ParseShift();
    while (true)
    {
        if (Match("<="))
        {
            int64_t right = ParseShift();
            left = (left <= right) ? 1 : 0;
        }
        else if (Match(">="))
        {
            int64_t right = ParseShift();
            left = (left >= right) ? 1 : 0;
        }
        else if (Match("<"))
        {
            int64_t right = ParseShift();
            left = (left < right) ? 1 : 0;
        }
        else if (Match(">"))
        {
            int64_t right = ParseShift();
            left = (left > right) ? 1 : 0;
        }
        else
        {
            break;
        }
    }
    return left;
}

int64_t ExprParser::ParseShift()
{
    int64_t left = ParseAdditive();
    while (true)
    {
        if (Match("<<"))
        {
            int64_t right = ParseAdditive();
            left = left << right;
        }
        else if (Match(">>"))
        {
            int64_t right = ParseAdditive();
            left = left >> right;
        }
        else
        {
            break;
        }
    }
    return left;
}

int64_t ExprParser::ParseAdditive()
{
    int64_t left = ParseMultiplicative();
    while (true)
    {
        if (Match("+"))
        {
            int64_t right = ParseMultiplicative();
            left = left + right;
        }
        else if (Match("-"))
        {
            int64_t right = ParseMultiplicative();
            left = left - right;
        }
        else
        {
            break;
        }
    }
    return left;
}

int64_t ExprParser::ParseMultiplicative()
{
    int64_t left = ParseUnary();
    while (true)
    {
        if (Match("*"))
        {
            int64_t right = ParseUnary();
            left = left * right;
        }
        else if (Match("/"))
        {
            int64_t right = ParseUnary();
            left = (right != 0) ? (left / right) : 0;
        }
        else
        {
            break;
        }
    }
    return left;
}

int64_t ExprParser::ParseUnary()
{
    if (Match("!"))
    {
        return (ParseUnary() == 0) ? 1 : 0;
    }
    if (Match("~"))
    {
        return ~ParseUnary();
    }
    if (Match("-"))
    {
        return -ParseUnary();
    }
    if (Match("+"))
    {
        return ParseUnary();
    }
    return ParsePrimary();
}

int64_t ExprParser::ParsePrimary()
{
    const Token* tk = Peek();
    if (!tk) return 0;

    if (Match("("))
    {
        int64_t val = ParseLogicalOr();
        Match(")");
        return val;
    }

    if (tk->Type == TokenType::Number)
    {
        Advance();
        try
        {
            if (tk->Text.starts_with("0x") || tk->Text.starts_with("0X"))
            {
                return std::stoll(tk->Text, nullptr, 16);
            }
            return std::stoll(tk->Text, nullptr, 10);
        }
        catch (...)
        {
            return 0;
        }
    }

    Advance();
    return 0;
}

} // namespace Emux
