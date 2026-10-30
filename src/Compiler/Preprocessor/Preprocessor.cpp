#include <Emux/Compiler/Preprocessor/Preprocessor.hpp>
#include <Emux/CLI/FileReader.hpp>
#include "Emux/Compiler/CompilerContext.hpp"
#include "Emux/Compiler/Diagnostic.hpp"
#include "Emux/Compiler/Lexer/Lexer.hpp"
#include "Emux/Compiler/Parser/ExprParser.hpp"
#include "Emux/Compiler/SourceFile.hpp"
#include "Emux/Compiler/TokenType.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace Emux
{

Preprocessor::Preprocessor(CompilerContext& context)
    : m_Context(context), m_Defines(m_LocalDefines)
{
}

Preprocessor::Preprocessor(CompilerContext& context, std::unordered_map<std::string, std::vector<Token>>& defines)
    : m_Context(context), m_Defines(defines)
{
}

bool Preprocessor::IsActive() const
{
    return m_ConditionStack.empty() || m_ConditionStack.back().IsActive;
}

void Preprocessor::HandleIf(bool condition, const Token& directive)
{
    bool parentActive = m_ConditionStack.empty() || m_ConditionStack.back().IsActive;
    bool isActive = parentActive && condition;
    m_ConditionStack.push_back({
        .ParentActive = parentActive,
        .ConditionMet = condition,
        .IsActive = isActive,
        .InElse = false,
        .Directive = directive
    });
}

void Preprocessor::HandleElif(bool condition, const Token& directive)
{
    if (m_ConditionStack.empty())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            directive.Location,
            "'@" + directive.Text + "' without matching '@if'"
        );
        return;
    }

    auto& frame = m_ConditionStack.back();
    if (frame.InElse)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            directive.Location,
            "'@" + directive.Text + "' after '@else'"
        );
        return;
    }

    if (frame.ParentActive && !frame.ConditionMet)
    {
        if (condition)
        {
            frame.ConditionMet = true;
            frame.IsActive = true;
        }
        else
        {
            frame.IsActive = false;
        }
    }
    else
    {
        frame.IsActive = false;
    }
}

void Preprocessor::HandleElse(const Token& directive)
{
    if (m_ConditionStack.empty())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            directive.Location,
            "'@else' without matching '@if'"
        );
        return;
    }

    auto& frame = m_ConditionStack.back();
    if (frame.InElse)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            directive.Location,
            "Multiple '@else' directives for the same '@if'"
        );
        return;
    }

    frame.InElse = true;
    if (frame.ParentActive && !frame.ConditionMet)
    {
        frame.ConditionMet = true;
        frame.IsActive = true;
    }
    else
    {
        frame.IsActive = false;
    }
}

void Preprocessor::HandleEndif(const Token& directive)
{
    if (m_ConditionStack.empty())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            directive.Location,
            "'@endif' without matching '@if'"
        );
        return;
    }

    m_ConditionStack.pop_back();
}

void Preprocessor::ExpandToken(const Token& tk, std::vector<Token>& output, std::unordered_set<std::string>& activeMacros)
{
    if (tk.Type == TokenType::Identifier && !activeMacros.contains(tk.Text))
    {
        auto it = m_Defines.find(tk.Text);
        if (it != m_Defines.end())
        {
            activeMacros.insert(tk.Text);
            for (const Token& rep : it->second)
            {
                ExpandToken(rep, output, activeMacros);
            }
            activeMacros.erase(tk.Text);
            return;
        }
    }

    output.push_back(tk);
}

bool Preprocessor::EvaluateCondition(const std::vector<Token>& tokens)
{
    if (tokens.empty()) return false;

    // 1. Process `defined(...)` or `defined NAME`
    std::vector<Token> withDefined;
    for (size_t k = 0; k < tokens.size(); ++k)
    {
        if (tokens[k].Type == TokenType::Identifier && tokens[k].Text == "defined")
        {
            std::string macroName;
            SourceLocation loc = tokens[k].Location;
            if (k + 1 < tokens.size() && tokens[k + 1].Type == TokenType::LeftParen)
            {
                if (k + 2 < tokens.size() && tokens[k + 2].Type == TokenType::Identifier &&
                    k + 3 < tokens.size() && tokens[k + 3].Type == TokenType::RightParen)
                {
                    macroName = tokens[k + 2].Text;
                    k += 3;
                }
                else
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        loc,
                        "Malformed defined(...) in preprocessor condition"
                    );
                    return false;
                }
            }
            else if (k + 1 < tokens.size() && tokens[k + 1].Type == TokenType::Identifier)
            {
                macroName = tokens[k + 1].Text;
                k += 1;
            }
            else
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Error,
                    loc,
                    "Expected identifier after 'defined'"
                );
                return false;
            }

            bool isDef = m_Defines.contains(macroName);
            withDefined.push_back(Token{
                .Type = TokenType::Number,
                .Text = isDef ? "1" : "0",
                .Location = loc
            });
        }
        else
        {
            withDefined.push_back(tokens[k]);
        }
    }

    // 2. Expand macros, replace unknown identifiers with 0
    std::vector<Token> macroExpanded;
    std::unordered_set<std::string> active;
    for (const auto& t : withDefined)
    {
        if (t.Type == TokenType::Identifier)
        {
            if (m_Defines.contains(t.Text))
            {
                ExpandToken(t, macroExpanded, active);
            }
            else
            {
                macroExpanded.push_back(Token{
                    .Type = TokenType::Number,
                    .Text = "0",
                    .Location = t.Location
                });
            }
        }
        else
        {
            macroExpanded.push_back(t);
        }
    }

    // 3. Coalesce compound operators
    std::vector<Token> coalesced;
    for (size_t j = 0; j < macroExpanded.size(); ++j)
    {
        const Token& cur = macroExpanded[j];
        if (j + 1 < macroExpanded.size())
        {
            const Token& next = macroExpanded[j + 1];
            if (cur.Type == TokenType::Assign && next.Type == TokenType::Assign)
            {
                coalesced.push_back(Token{ .Type = TokenType::Unknown, .Text = "==", .Location = cur.Location });
                ++j;
                continue;
            }
            if (cur.Type == TokenType::Not && cur.Text == "!" && next.Type == TokenType::Assign)
            {
                coalesced.push_back(Token{ .Type = TokenType::Unknown, .Text = "!=", .Location = cur.Location });
                ++j;
                continue;
            }
            if (cur.Text == "<" && next.Type == TokenType::Assign)
            {
                coalesced.push_back(Token{ .Type = TokenType::Unknown, .Text = "<=", .Location = cur.Location });
                ++j;
                continue;
            }
            if (cur.Text == ">" && next.Type == TokenType::Assign)
            {
                coalesced.push_back(Token{ .Type = TokenType::Unknown, .Text = ">=", .Location = cur.Location });
                ++j;
                continue;
            }
            if (cur.Type == TokenType::And && next.Type == TokenType::And)
            {
                coalesced.push_back(Token{ .Type = TokenType::Unknown, .Text = "&&", .Location = cur.Location });
                ++j;
                continue;
            }
            if (cur.Type == TokenType::Or && next.Type == TokenType::Or)
            {
                coalesced.push_back(Token{ .Type = TokenType::Unknown, .Text = "||", .Location = cur.Location });
                ++j;
                continue;
            }
        }
        coalesced.push_back(cur);
    }

    return ExprParser::Evaluate(coalesced) != 0;
}

void Preprocessor::Process()
{
    std::vector<Token> tokens;
    std::unordered_set<std::string> activeMacros;

    for (size_t i = 0; i < m_Context.Tokens.size(); ++i)
    {
        const Token& tk = m_Context.Tokens[i];

        if (tk.Type == TokenType::At)
        {
            if (i + 1 >= m_Context.Tokens.size())
            {
                if (IsActive())
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        tk.Location,
                        "Expected preprocessor directive after '@'"
                    );
                }
                continue;
            }

            Token directive = m_Context.Tokens[++i];

            bool isConditional = (directive.Text == "if" ||
                                  directive.Text == "ifdef" ||
                                  directive.Text == "ifndef" ||
                                  directive.Text == "elif" ||
                                  directive.Text == "elifdef" ||
                                  directive.Text == "elifndef" ||
                                  directive.Text == "else" ||
                                  directive.Text == "endif");

            if (!IsActive() && !isConditional)
            {
                while (i + 1 < m_Context.Tokens.size() &&
                       m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                       m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                {
                    ++i;
                }
                continue;
            }

            if (directive.Text == "if" || directive.Text == "elif")
            {
                std::vector<Token> condTokens;
                while (i + 1 < m_Context.Tokens.size() &&
                       m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                       m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                {
                    ++i;
                    condTokens.push_back(m_Context.Tokens[i]);
                }

                bool condition = false;
                bool parentActive = m_ConditionStack.empty() || m_ConditionStack.back().IsActive;
                if (directive.Text == "if")
                {
                    if (parentActive)
                    {
                        condition = EvaluateCondition(condTokens);
                    }
                    HandleIf(condition, directive);
                }
                else // elif
                {
                    if (parentActive && !m_ConditionStack.empty() && !m_ConditionStack.back().ConditionMet)
                    {
                        condition = EvaluateCondition(condTokens);
                    }
                    HandleElif(condition, directive);
                }
            }
            else if (directive.Text == "ifdef" || directive.Text == "ifndef" ||
                     directive.Text == "elifdef" || directive.Text == "elifndef")
            {
                if (i + 1 >= m_Context.Tokens.size())
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        directive.Location,
                        "Expected identifier after '@" + directive.Text + "'"
                    );
                    continue;
                }

                Token idToken = m_Context.Tokens[++i];
                if (idToken.Type != TokenType::Identifier)
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        idToken.Location,
                        "Expected identifier after '@" + directive.Text + "'"
                    );
                    while (i + 1 < m_Context.Tokens.size() &&
                           m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                           m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                    {
                        ++i;
                    }
                    continue;
                }

                while (i + 1 < m_Context.Tokens.size() &&
                       m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                       m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                {
                    ++i;
                }

                bool condition = false;
                if (directive.Text == "ifdef" || directive.Text == "elifdef")
                {
                    condition = m_Defines.contains(idToken.Text);
                }
                else
                {
                    condition = !m_Defines.contains(idToken.Text);
                }

                if (directive.Text == "ifdef" || directive.Text == "ifndef")
                {
                    HandleIf(condition, directive);
                }
                else
                {
                    HandleElif(condition, directive);
                }
            }
            else if (directive.Text == "else")
            {
                while (i + 1 < m_Context.Tokens.size() &&
                       m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                       m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                {
                    ++i;
                }
                HandleElse(directive);
            }
            else if (directive.Text == "endif")
            {
                while (i + 1 < m_Context.Tokens.size() &&
                       m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                       m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                {
                    ++i;
                }
                HandleEndif(directive);
            }
            else if (directive.Text == "include")
            {
                if (i + 1 >= m_Context.Tokens.size())
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        directive.Location,
                        "Include expects \"PATH/FILE.ext\""
                    );
                    continue;
                }

                Token path = m_Context.Tokens[++i];

                if (path.Type != TokenType::String)
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        path.Location, 
                        "Include expects \"PATH/FILE.ext\""
                    );
                    continue;
                }
                fs::path configPath(m_Context.Arguments.config);
                std::string solvedPath;

                for (fs::path incPath : m_Context.Project.include)
                {
                    solvedPath = (configPath.parent_path() / incPath / path.Text).lexically_normal().string();
                    if (fs::exists(solvedPath)) break;
                }

                if (!fs::exists(solvedPath))
                {
                    //if (!fs::exists(solvedPath))
                    //{
                        m_Context.Diagnostics.Add(
                            DiagnosticLevel::Fatal,
                            path.Location, 
                            "Can not find file '" + path.Text + "' at " + solvedPath
                        );
                        continue;
                    //}
                }

                fs::path depPath = fs::path(solvedPath).lexically_normal();
                if (depPath.is_relative())
                {
                    depPath = fs::absolute(depPath).lexically_normal();
                }
                const std::string depKey = depPath.string();

                // Módulos do projeto são compilados separadamente: incluir um
                // deles aqui duplicaria funções/variáveis no link.
                if (m_Context.ModulePaths.contains(depKey))
                {
                    continue;
                }

                m_Context.IncludedFiles.insert(depKey);

                if (m_Context.Sources.contains(depKey))
                {
                    continue;
                }

                SourceFile file = FileReader::Read(depPath);
                const std::string sourceKey = file.GetName();

                if (m_Context.Sources.contains(sourceKey))
                {
                    continue;
                }

                m_Context.Sources[sourceKey] = std::move(file);

                CompilerContext subContext;
                subContext.Arguments = m_Context.Arguments;
                subContext.Project = m_Context.Project;
                subContext.Sources = m_Context.Sources;
                subContext.ModulePaths = m_Context.ModulePaths;

                Lexer lexer(subContext, m_Context.Sources[sourceKey]);
                lexer.Tokenize();

                Preprocessor preprocessor(subContext, m_Defines);
                preprocessor.Process();

                for (auto& [name, src] : subContext.Sources)
                {
                    m_Context.Sources[name] = src;
                }

                for (const std::string& dependency : subContext.IncludedFiles)
                {
                    m_Context.IncludedFiles.insert(dependency);
                }

                for (Token& token : subContext.Tokens)
                {
                    if (token.Type != TokenType::EndOfFile)
                    {
                        tokens.push_back(std::move(token));
                    }
                }
            }
            else if (directive.Text == "define")
            {
                if (i + 1 >= m_Context.Tokens.size())
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        directive.Location,
                        "Expected macro name after '@define'"
                    );
                    continue;
                }

                Token macroNameToken = m_Context.Tokens[++i];

                if (macroNameToken.Type != TokenType::Identifier)
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        macroNameToken.Location,
                        "Macro name after '@define' must be an identifier"
                    );
                    while (i + 1 < m_Context.Tokens.size() &&
                           m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                           m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                    {
                        ++i;
                    }
                    continue;
                }

                std::vector<Token> replacement;
                while (i + 1 < m_Context.Tokens.size() &&
                       m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                       m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                {
                    ++i;
                    replacement.push_back(m_Context.Tokens[i]);
                }

                m_Defines[macroNameToken.Text] = std::move(replacement);
            }
            else if (directive.Text == "undef")
            {
                if (i + 1 >= m_Context.Tokens.size())
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        directive.Location,
                        "Expected macro name after '@undef'"
                    );
                    continue;
                }

                Token macroNameToken = m_Context.Tokens[++i];

                if (macroNameToken.Type != TokenType::Identifier)
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        macroNameToken.Location,
                        "Macro name after '@undef' must be an identifier"
                    );
                    while (i + 1 < m_Context.Tokens.size() &&
                           m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                           m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                    {
                        ++i;
                    }
                    continue;
                }

                m_Defines.erase(macroNameToken.Text);

                while (i + 1 < m_Context.Tokens.size() &&
                       m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                       m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                {
                    ++i;
                }
            }
            else
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Error,
                    directive.Location,
                    "Unknown preprocessor directive '@" + directive.Text + "'"
                );
            }
        }
        else
        {
            if (IsActive())
            {
                ExpandToken(tk, tokens, activeMacros);
            }
        }
    }

    if (!m_ConditionStack.empty())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            m_ConditionStack.back().Directive.Location,
            "Unterminated conditional directive (missing '@endif')"
        );
    }

    m_Context.Tokens = std::move(tokens);
}

}
