#pragma once

#include "Emux/Compiler/CompilerContext.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Emux
{

struct ConditionalFrame
{
    bool ParentActive = true;
    bool ConditionMet = false;
    bool IsActive = false;
    bool InElse = false;
    Token Directive;
};

class Preprocessor
{
public:
    Preprocessor(CompilerContext& context);
    Preprocessor(CompilerContext& context, std::unordered_map<std::string, std::vector<Token>>& defines);

    void Process();

private:
    void ExpandToken(const Token& tk, std::vector<Token>& output, std::unordered_set<std::string>& activeMacros);
    bool EvaluateCondition(const std::vector<Token>& tokens);
    bool IsActive() const;

    void HandleIf(bool condition, const Token& directive);
    void HandleElif(bool condition, const Token& directive);
    void HandleElse(const Token& directive);
    void HandleEndif(const Token& directive);

private:
    CompilerContext& m_Context;
    std::unordered_map<std::string, std::vector<Token>> m_LocalDefines;
    std::unordered_map<std::string, std::vector<Token>>& m_Defines;
    std::vector<ConditionalFrame> m_ConditionStack;
};

}
