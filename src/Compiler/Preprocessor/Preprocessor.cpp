#include "Emux/CLI/FileReader.hpp"
#include "Emux/Compiler/CompilerContext.hpp"
#include "Emux/Compiler/Diagnostic.hpp"
#include "Emux/Compiler/Lexer/Lexer.hpp"
#include "Emux/Compiler/SourceFile.hpp"
#include "Emux/Compiler/TokenType.hpp"
#include <Emux/Compiler/Preprocessor/Preprocessor.hpp>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace Emux
{

Preprocessor::Preprocessor(CompilerContext& context) : m_Context(context)
{
}

void Preprocessor::Process()
{
    std::vector<Token> tokens; 

    for (size_t i = 0; i < m_Context.Tokens.size(); ++i)
    {
        const Token& tk = m_Context.Tokens[i];

        if (tk.Type == TokenType::At)
        {
            if (i + 1 >= m_Context.Tokens.size())
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Error,
                    tk.Location,
                    "Expected preprocessor directive after '@'"
                );
                continue;
            }

            Token directive = m_Context.Tokens[++i];

            if (directive.Text == "include")
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

                std::filesystem::path currentFile(tk.Location.File);
                std::string solvedPath = (currentFile.parent_path() / path.Text).lexically_normal().string();

                if (!std::filesystem::exists(solvedPath))
                {
                    solvedPath = std::filesystem::path(path.Text).lexically_normal().string();
                    if (!std::filesystem::exists(solvedPath))
                    {
                        m_Context.Diagnostics.Add(
                            DiagnosticLevel::Fatal,
                            path.Location, 
                            "Can not find file '" + path.Text + "'."
                        );
                        continue;
                    }
                }

                if (m_Context.Sources.contains(solvedPath))
                {
                    continue;
                }

                SourceFile file = FileReader::Read(solvedPath);
                m_Context.Sources[solvedPath] = file;

                CompilerContext subContext;
                subContext.Source = file;
                subContext.Sources = m_Context.Sources;

                Lexer lexer(subContext);
                lexer.Tokenize();

                Preprocessor preprocessor(subContext);
                preprocessor.Process();

                for (auto& [name, src] : subContext.Sources)
                {
                    m_Context.Sources[name] = src;
                }

                for (Token& token : subContext.Tokens)
                {
                    if (token.Type != TokenType::EndOfFile)
                    {
                        tokens.push_back(std::move(token));
                    }
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
            tokens.push_back(tk);
        }
    }

    m_Context.Tokens = std::move(tokens);
}

}
