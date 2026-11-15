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
    : m_Context(context), m_Defines(m_LocalDefines), m_FunctionMacros(m_LocalFunctionMacros)
{
}

Preprocessor::Preprocessor(CompilerContext& context,
                           std::unordered_map<std::string, std::vector<Token>>& defines,
                           std::unordered_map<std::string, FunctionMacro>& functionMacros)
    : m_Context(context), m_Defines(defines), m_FunctionMacros(functionMacros)
{
}

bool Preprocessor::IsDefined(const std::string& name) const
{
    return m_Defines.contains(name) || m_FunctionMacros.contains(name);
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
            "'#" + directive.Text + "' without matching '#if'"
        );
        return;
    }

    auto& frame = m_ConditionStack.back();
    if (frame.InElse)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            directive.Location,
            "'#" + directive.Text + "' after '#else'"
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
            "'#else' without matching '#if'"
        );
        return;
    }

    auto& frame = m_ConditionStack.back();
    if (frame.InElse)
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            directive.Location,
            "Multiple '#else' directives for the same '#if'"
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
            "'#endif' without matching '#if'"
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
            ExpandSequence(it->second, output, activeMacros);
            activeMacros.erase(tk.Text);
            return;
        }
    }

    output.push_back(tk);
}

size_t Preprocessor::ParseMacroArgs(const std::vector<Token>& input, size_t open, std::vector<std::vector<Token>>& args)
{
    // input[open] == '('; recolhe ate ao ')' correspondente, separando por
    // virgulas de nivel zero. Parenteses aninhados ficam dentro do argumento.
    size_t k = open + 1;
    int depth = 0;
    std::vector<Token> current;
    bool any = false;

    for (; k < input.size(); ++k)
    {
        const Token& t = input[k];
        if (t.Type == TokenType::NewLine || t.Type == TokenType::EndOfFile)
        {
            continue;
        }
        if (t.Type == TokenType::LeftParen)
        {
            ++depth;
            current.push_back(t);
            any = true;
        }
        else if (t.Type == TokenType::RightParen)
        {
            if (depth == 0)
            {
                if (any || !current.empty())
                {
                    args.push_back(std::move(current));
                }
                return k + 1;
            }
            --depth;
            current.push_back(t);
            any = true;
        }
        else if (t.Type == TokenType::Comma && depth == 0)
        {
            args.push_back(std::move(current));
            current.clear();
            any = true;
        }
        else
        {
            current.push_back(t);
            any = true;
        }
    }

    return std::string::npos;
}

void Preprocessor::ExpandFunctionMacro(const std::string& name, const FunctionMacro& macro, const std::vector<std::vector<Token>>& args, std::vector<Token>& output, std::unordered_set<std::string>& activeMacros)
{
    // Os argumentos sao expandidos uma vez antes da substituicao (como em C,
    // sem stringificacao '#' nem concatenacao '##'). A macro ainda NAO esta
    // marcada ativa: uma macro aninhada no argumento (INC(INC(x))) expande-se.
    std::vector<std::vector<Token>> expandedArgs;
    expandedArgs.reserve(args.size());
    for (const auto& arg : args)
    {
        std::vector<Token> expanded;
        ExpandSequence(arg, expanded, activeMacros);
        expandedArgs.push_back(std::move(expanded));
    }

    std::vector<Token> substituted;
    for (const Token& bodyToken : macro.Body)
    {
        if (bodyToken.Type == TokenType::Identifier)
        {
            bool isParam = false;
            for (size_t p = 0; p < macro.Params.size(); ++p)
            {
                if (macro.Params[p] == bodyToken.Text)
                {
                    if (p < expandedArgs.size())
                    {
                        for (const Token& argToken : expandedArgs[p])
                        {
                            substituted.push_back(argToken);
                        }
                    }
                    isParam = true;
                    break;
                }
            }
            if (isParam)
            {
                continue;
            }
        }
        substituted.push_back(bodyToken);
    }

    // Reexamina o corpo substituido para expandir macros aninhadas. So agora a
    // macro fica marcada ativa, impedindo a expansao recursiva infinita do
    // proprio nome no corpo (F(x) -> F(x) termina).
    activeMacros.insert(name);
    ExpandSequence(substituted, output, activeMacros);
    activeMacros.erase(name);
}

void Preprocessor::ExpandSequence(const std::vector<Token>& input, std::vector<Token>& output, std::unordered_set<std::string>& activeMacros)
{
    for (size_t k = 0; k < input.size(); ++k)
    {
        const Token& t = input[k];
        if (t.Type == TokenType::Identifier && !activeMacros.contains(t.Text))
        {
            auto objIt = m_Defines.find(t.Text);
            if (objIt != m_Defines.end())
            {
                activeMacros.insert(t.Text);
                ExpandSequence(objIt->second, output, activeMacros);
                activeMacros.erase(t.Text);
                continue;
            }

            auto fnIt = m_FunctionMacros.find(t.Text);
            if (fnIt != m_FunctionMacros.end() &&
                k + 1 < input.size() && input[k + 1].Type == TokenType::LeftParen)
            {
                std::vector<std::vector<Token>> args;
                size_t end = ParseMacroArgs(input, k + 1, args);
                if (end != std::string::npos)
                {
                    const FunctionMacro& macro = fnIt->second;
                    if (args.empty() && macro.Params.size() == 1)
                    {
                        args.emplace_back();
                    }
                    if (args.size() != macro.Params.size())
                    {
                        m_Context.Diagnostics.Add(
                            DiagnosticLevel::Error,
                            t.Location,
                            "Macro '" + t.Text + "' expects " + std::to_string(macro.Params.size()) +
                            " argument(s), got " + std::to_string(args.size())
                        );
                        output.push_back(t);
                        continue;
                    }
                    ExpandFunctionMacro(t.Text, macro, args, output, activeMacros);
                    k = end - 1;
                    continue;
                }
            }
        }
        output.push_back(t);
    }
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

            bool isDef = IsDefined(macroName);
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
            if (cur.Type == TokenType::LogicalNot && next.Type == TokenType::Assign)
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

        if (tk.Type == TokenType::Hash)
        {
            if (i + 1 >= m_Context.Tokens.size())
            {
                if (IsActive())
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        tk.Location,
                        "Expected preprocessor directive after '#'"
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
                if (directive.Text == "if")
                {
                    const bool parentActive =
                        m_ConditionStack.empty() || m_ConditionStack.back().IsActive;

                    if (parentActive)
                    {
                        condition = EvaluateCondition(condTokens);
                    }
                    HandleIf(condition, directive);
                }
                else // elif
                {
                    // O 'elif' decide com base no contexto que o contem
                    // (ParentActive), nao no IsActive atual: quando o '#if'
                    // falhou, IsActive ja e falso mas o ramo alternativo ainda
                    // pode ser escolhido.
                    const bool parentActive =
                        m_ConditionStack.empty() || m_ConditionStack.back().ParentActive;

                    if (parentActive && !m_ConditionStack.empty() &&
                        !m_ConditionStack.back().ConditionMet)
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
                        "Expected identifier after '#" + directive.Text + "'"
                    );
                    continue;
                }

                Token idToken = m_Context.Tokens[++i];
                if (idToken.Type != TokenType::Identifier)
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        idToken.Location,
                        "Expected identifier after '#" + directive.Text + "'"
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
                    condition = IsDefined(idToken.Text);
                }
                else
                {
                    condition = !IsDefined(idToken.Text);
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
                std::vector<fs::path> triedPaths;

                // Primeiro ao lado do ficheiro que contem a diretiva, como em C.
                // So depois se percorrem os caminhos de include do projeto.
                const fs::path includingFile(directive.Location.File);
                const fs::path localCandidate = includingFile.empty()
                    ? fs::path{}
                    : (includingFile.parent_path() / path.Text).lexically_normal();

                if (!localCandidate.empty())
                {
                    triedPaths.push_back(localCandidate);

                    if (fs::exists(localCandidate))
                    {
                        solvedPath = localCandidate.string();
                    }
                }

                for (const fs::path& incPath : m_Context.Project.include)
                {
                    if (!solvedPath.empty())
                    {
                        break;
                    }

                    const fs::path candidate =
                        (configPath.parent_path() / incPath / path.Text).lexically_normal();

                    if (candidate != localCandidate)
                    {
                        triedPaths.push_back(candidate);
                    }

                    if (fs::exists(candidate))
                    {
                        solvedPath = candidate.string();
                    }
                }

                if (solvedPath.empty())
                {
                    std::string searched;

                    if (triedPaths.empty())
                    {
                        searched = " (nenhum caminho de include configurado)";
                    }
                    else
                    {
                        for (const fs::path& tried : triedPaths)
                        {
                            searched += "\n  " + tried.string();
                        }
                    }

                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Fatal,
                        path.Location,
                        "Can not find file '" + path.Text + "'. Searched:" + searched
                    );
                    continue;
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

                Preprocessor preprocessor(subContext, m_Defines, m_FunctionMacros);
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
                        "Expected macro name after '#define'"
                    );
                    continue;
                }

                Token macroNameToken = m_Context.Tokens[++i];

                if (macroNameToken.Type != TokenType::Identifier)
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        macroNameToken.Location,
                        "Macro name after '#define' must be an identifier"
                    );
                    while (i + 1 < m_Context.Tokens.size() &&
                           m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                           m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                    {
                        ++i;
                    }
                    continue;
                }

                // Macro com parametros: '(' colado ao nome (sem espaco), como em
                // C. '#define MAX(a, b) ...' guarda params + corpo; '#define X (1+2)'
                // (com espaco) continua a ser uma macro objeto cujo corpo comeca por '('.
                if (i + 1 < m_Context.Tokens.size() &&
                    m_Context.Tokens[i + 1].Type == TokenType::LeftParen &&
                    m_Context.Tokens[i + 1].Location.Offset ==
                        macroNameToken.Location.Offset + macroNameToken.Location.Length)
                {
                    ++i; // '('
                    FunctionMacro macro;
                    bool ok = true;

                    if (i + 1 < m_Context.Tokens.size() &&
                        m_Context.Tokens[i + 1].Type == TokenType::RightParen)
                    {
                        ++i; // ')' — zero parametros
                    }
                    else
                    {
                        while (true)
                        {
                            if (i + 1 >= m_Context.Tokens.size() ||
                                m_Context.Tokens[i + 1].Type == TokenType::NewLine ||
                                m_Context.Tokens[i + 1].Type == TokenType::EndOfFile)
                            {
                                m_Context.Diagnostics.Add(
                                    DiagnosticLevel::Error,
                                    macroNameToken.Location,
                                    "Unterminated parameter list in '#define " + macroNameToken.Text + "'"
                                );
                                ok = false;
                                break;
                            }
                            ++i;
                            const Token& param = m_Context.Tokens[i];
                            if (param.Type != TokenType::Identifier)
                            {
                                m_Context.Diagnostics.Add(
                                    DiagnosticLevel::Error,
                                    param.Location,
                                    "Macro parameter must be an identifier"
                                );
                                ok = false;
                                break;
                            }
                            for (const auto& existing : macro.Params)
                            {
                                if (existing == param.Text)
                                {
                                    m_Context.Diagnostics.Add(
                                        DiagnosticLevel::Error,
                                        param.Location,
                                        "Duplicate macro parameter '" + param.Text + "'"
                                    );
                                    ok = false;
                                }
                            }
                            macro.Params.push_back(param.Text);

                            if (i + 1 < m_Context.Tokens.size() &&
                                m_Context.Tokens[i + 1].Type == TokenType::Comma)
                            {
                                ++i; // ','
                                continue;
                            }
                            if (i + 1 < m_Context.Tokens.size() &&
                                m_Context.Tokens[i + 1].Type == TokenType::RightParen)
                            {
                                ++i; // ')'
                                break;
                            }
                            m_Context.Diagnostics.Add(
                                DiagnosticLevel::Error,
                                macroNameToken.Location,
                                "Expected ',' or ')' in '#define " + macroNameToken.Text + "' parameter list"
                            );
                            ok = false;
                            break;
                        }
                    }

                    while (i + 1 < m_Context.Tokens.size() &&
                           m_Context.Tokens[i + 1].Type != TokenType::NewLine &&
                           m_Context.Tokens[i + 1].Type != TokenType::EndOfFile)
                    {
                        ++i;
                        macro.Body.push_back(m_Context.Tokens[i]);
                    }

                    if (ok)
                    {
                        m_Defines.erase(macroNameToken.Text);
                        m_FunctionMacros[macroNameToken.Text] = std::move(macro);
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

                m_FunctionMacros.erase(macroNameToken.Text);
                m_Defines[macroNameToken.Text] = std::move(replacement);
            }
            else if (directive.Text == "undef")
            {
                if (i + 1 >= m_Context.Tokens.size())
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        directive.Location,
                        "Expected macro name after '#undef'"
                    );
                    continue;
                }

                Token macroNameToken = m_Context.Tokens[++i];

                if (macroNameToken.Type != TokenType::Identifier)
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        macroNameToken.Location,
                        "Macro name after '#undef' must be an identifier"
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
                m_FunctionMacros.erase(macroNameToken.Text);

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
                    "Unknown preprocessor directive '#" + directive.Text + "'"
                );
            }
        }
        else
        {
            if (IsActive())
            {
                // Invocacao de macro com parametros no codigo fonte: NOME(...).
                // O '(' pode surgir na linha seguinte (newlines sao ignorados).
                if (tk.Type == TokenType::Identifier &&
                    !activeMacros.contains(tk.Text) &&
                    m_FunctionMacros.contains(tk.Text))
                {
                    size_t open = i + 1;
                    while (open < m_Context.Tokens.size() &&
                           m_Context.Tokens[open].Type == TokenType::NewLine)
                    {
                        ++open;
                    }
                    if (open < m_Context.Tokens.size() &&
                        m_Context.Tokens[open].Type == TokenType::LeftParen)
                    {
                        std::vector<std::vector<Token>> args;
                        size_t end = ParseMacroArgs(m_Context.Tokens, open, args);
                        if (end == std::string::npos)
                        {
                            m_Context.Diagnostics.Add(
                                DiagnosticLevel::Error,
                                tk.Location,
                                "Unterminated argument list for macro '" + tk.Text + "'"
                            );
                            tokens.push_back(tk);
                            continue;
                        }

                        const FunctionMacro& macro = m_FunctionMacros.at(tk.Text);
                        if (args.empty() && macro.Params.size() == 1)
                        {
                            args.emplace_back();
                        }
                        if (args.size() != macro.Params.size())
                        {
                            m_Context.Diagnostics.Add(
                                DiagnosticLevel::Error,
                                tk.Location,
                                "Macro '" + tk.Text + "' expects " + std::to_string(macro.Params.size()) +
                                " argument(s), got " + std::to_string(args.size())
                            );
                            tokens.push_back(tk);
                        }
                        else
                        {
                            ExpandFunctionMacro(tk.Text, macro, args, tokens, activeMacros);
                        }
                        i = end - 1;
                        continue;
                    }
                }
                ExpandToken(tk, tokens, activeMacros);
            }
        }
    }

    if (!m_ConditionStack.empty())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            m_ConditionStack.back().Directive.Location,
            "Unterminated conditional directive (missing '#endif')"
        );
    }

    m_Context.Tokens = std::move(tokens);
}

}
