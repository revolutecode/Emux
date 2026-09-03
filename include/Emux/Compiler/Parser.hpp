#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <optional>

#include <Emux/Compiler/CompilerContext.hpp>
#include <Emux/Compiler/OptionalToken.hpp>
#include <Emux/Compiler/AST/Program.hpp>
#include <Emux/Compiler/AST/SectionNode.hpp>
#include <Emux/Compiler/AST/FunctionParameter.hpp>


namespace Emux
{

class Parser
{

public:

    explicit Parser(
        CompilerContext& context
    );

    void Parse();

private:

    bool IsAtEnd() const;

    const Token& Current() const;

    const Token& Previous() const;

    const Token& Peek(size_t offset = 0) const;

    void Advance();

    bool Check(TokenType type) const;

    OptionalToken Match(TokenType type);

    OptionalToken Consume(
        TokenType type,
        std::string_view message
    );

    void SkipNewLines();
    void AdvanceAndSNL();

    void Synchronize();
    
    bool IsSectionStart() const;
    bool IsFunctionStart() const;
    bool IsFunctionStart(const Token& token) const;
    bool IsLiteral() const;
    bool IsLiteral(const Token& token) const;
    bool IsBinary() const;
    bool IsBinary(const Token& token) const;

private:

    void ParseSection(Program& program);
    void ParseDependencies(SectionNode& section);

    void ParseFunction(SectionNode& node);

    void ParseVariable(SectionNode& node);
    void ParseExpression(Node& node);
    void ParseFunctionCall(Node& node);
    void ParseVariableCall(Node& node);
    void ParseAssignment(Node& node);
    void ParseLiteral(Node& node);
    void ParseIR(Node& node);
    void ParseReturn(Node& node);
    void ParseBinary(Node& node);

    void ParseStatement(Node& node);

private:

    CompilerContext& m_Context;

    std::unordered_set<std::string> m_Variables;
    std::unordered_map<std::string, std::reference_wrapper<const std::vector<FunctionParameter>>> m_Functions;

    size_t m_Position = 0;

};

}
