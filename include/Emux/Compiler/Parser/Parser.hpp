#pragma once

#include "Emux/Compiler/AttrSet.hpp"
#include "Emux/Compiler/Scope.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <optional>

#include <Emux/Compiler/CompilerContext.hpp>
#include <Emux/Compiler/Parser/OptionalToken.hpp>
#include <Emux/Compiler/AST/Program.hpp>
#include <Emux/Compiler/AST/FunctionParameter.hpp>
#include <Emux/Compiler/AST/BinaryNode.hpp>
#include <Emux/Compiler/AST/NodePtr.hpp>

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

    // Lê um tipo terminado opcionalmente por '*', ex: u8, i64*, void**.
    std::optional<Token> ParseTypeToken(std::string_view message);

    void SkipNewLines();
    void AdvanceAndSNL();

    void Synchronize();
    
    bool IsGroupAttrStart() const;
    bool IsFunctionStart() const;
    bool IsFunctionStart(const Token& token) const;
    bool IsCallStart() const;
    bool IsAssignmentStart() const;
    bool IsLiteral() const;
    bool IsLiteral(const Token& token) const;
    bool IsBinary() const;
    bool IsBinary(const Token& token) const;

private:
    void ParseGroupAttr();

    void ParseDependencies(Node& section);

    void ParseFunction(Node& node);

    void ParseVariable(Node& node);

    void ParseUsing(Node& node);
    void ParseEnum(Node& node);
    
    std::optional<BinaryOperation> TokenToBinaryOperation(TokenType type) const;
    std::unique_ptr<Node> ParseExpression(int precedence = 0);
    std::unique_ptr<Node> ParsePrimary();
    // Aplica sufixos de expressão (indexação) ao nó base já construído.
    std::unique_ptr<Node> ParsePostfix(std::unique_ptr<Node> base);
    void ParseExpression(Node& node);
    void ParseFunctionCall(Node& node);
    void ParseVariableCall(Node& node);
    void ParseAssignment(Node& node);
    void ParseDerefAssignment(Node& node);
    bool IsDerefAssignment() const;
    // base[índice] = valor (escrita por índice).
    bool IsIndexAssignment() const;
    void ParseIndexAssignment(Node& node);
    // nome++ / nome-- (postfix): desugerado para nome += 1 / nome -= 1.
    bool IsIncrementStart() const;
    void ParseIncrement(Node& node);
    void ParseLiteral(Node& node);
    std::unique_ptr<Node> ParseCast();
    void ParseIR(Node& node);
    void ParseReturn(Node& node);

    // Controlo de fluxo. Each parses a full statement and pushes the resulting
    // If/While/For node into `node.Children`.
    void ParseIf(Node& node);
    void ParseWhile(Node& node);
    void ParseFor(Node& node);
    // Lê '( <expr> )' como condição; devolve o nó da expressão.
    NodePtr ParseCondition(std::string_view keyword);
    // Recolhe os statements entre '{' e '}' no vetor; consome as chavetas.
    bool ParseBlock(std::vector<NodePtr>& statements);

    void ParseStatement(Node& node);

    // Consome terminadores ';' e, no modo required, exige pelo menos um.
    void EndStatement();
    
    std::optional<std::string> HelperScopedName();
    
    void OpenGlobalScope(const Token& nameTok, const AttrSet& attrs);      // Chamar sempre que o Parser cria um node de declaração.
    void StampAttrs(Node& n) {
        n.Attrs.Merge(m_Pending);       // locais: consumidos aqui
        n.Attrs.Merge(m_CurrentScope.Attrs);  // globais do escopo ativo
        m_Pending = {};
    }  
        
private:

    CompilerContext& m_Context;
    std::unordered_map<std::string, Scope> m_Scopes;
    
    size_t m_Position = 0;
    Scope m_CurrentScope;
    AttrSet m_Pending;
    int m_ParenDepth = 0;
};

}
