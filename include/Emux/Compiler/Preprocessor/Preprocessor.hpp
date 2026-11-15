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

// Macro com parametros: '@define NOME(a, b) corpo'. Os parametros sao nomes
// substituidos pelos argumentos (ja expandidos) em cada invocacao NOME(x, y).
struct FunctionMacro
{
    std::vector<std::string> Params;
    std::vector<Token> Body;
};

class Preprocessor
{
public:
    Preprocessor(CompilerContext& context);
    Preprocessor(CompilerContext& context,
                 std::unordered_map<std::string, std::vector<Token>>& defines,
                 std::unordered_map<std::string, FunctionMacro>& functionMacros);

    void Process();

private:
    void ExpandToken(const Token& tk, std::vector<Token>& output, std::unordered_set<std::string>& activeMacros);
    void ExpandSequence(const std::vector<Token>& input, std::vector<Token>& output, std::unordered_set<std::string>& activeMacros);
    // Le os argumentos de uma invocacao a partir de input[open] == '('; devolve
    // o indice seguinte ao ')' e preenche args (um vector por argumento).
    size_t ParseMacroArgs(const std::vector<Token>& input, size_t open, std::vector<std::vector<Token>>& args);
    void ExpandFunctionMacro(const std::string& name, const FunctionMacro& macro, const std::vector<std::vector<Token>>& args, std::vector<Token>& output, std::unordered_set<std::string>& activeMacros);
    bool EvaluateCondition(const std::vector<Token>& tokens);
    bool IsActive() const;
    bool IsDefined(const std::string& name) const;

    void HandleIf(bool condition, const Token& directive);
    void HandleElif(bool condition, const Token& directive);
    void HandleElse(const Token& directive);
    void HandleEndif(const Token& directive);

private:
    CompilerContext& m_Context;
    std::unordered_map<std::string, std::vector<Token>> m_LocalDefines;
    std::unordered_map<std::string, std::vector<Token>>& m_Defines;
    std::unordered_map<std::string, FunctionMacro> m_LocalFunctionMacros;
    std::unordered_map<std::string, FunctionMacro>& m_FunctionMacros;
    std::vector<ConditionalFrame> m_ConditionStack;
};

}
