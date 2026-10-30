#pragma once

#include <Emux/Compiler/AST/AssignmentNode.hpp>
#include <Emux/Compiler/AST/BinaryNode.hpp>
#include <Emux/Compiler/AST/FunctionCallNode.hpp>
#include <Emux/Compiler/AST/FunctionNode.hpp>
#include <Emux/Compiler/AST/Program.hpp>
#include <Emux/Compiler/AST/UnaryNode.hpp>
#include <Emux/Compiler/AST/VariableCallNode.hpp>
#include <Emux/Compiler/AST/VariableNode.hpp>
#include <Emux/Compiler/Diagnostics.hpp>
#include <Emux/Compiler/Semantic/SemanticContext.hpp>
#include <Emux/Compiler/Type/Type.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>

namespace Emux
{

class SemanticAnalyzer
{
public:
    SemanticAnalyzer(
        Program& program,
        SemanticContext& semantic,
        Diagnostics& diagnostics,
        std::uint32_t pointerBits = 64
    );

    // Primeira fase: registra declarações de todos os módulos antes de
    // analisar qualquer corpo, para que a ordem dos arquivos não importe.
    void RegisterDeclarations();

    // Segunda fase: analisa corpos e chamadas usando as tabelas globais.
    void AnalyzeBodies();

private:
    void AnalyzeVariable(
        VariableNode& variable
    );

    Type AnalyzeVariableCall(
        VariableCallNode& variable
    );

    void DeclareFunction(
        FunctionNode& function
    );

    void RegisterExportSymbol(
        const std::string& symbol,
        const std::string& owner,
        const SourceLocation& location
    );

    void AnalyzeFunctionBody(
        FunctionNode& function
    );

    Type AnalyzeFunctionCall(
        FunctionCallNode& function
    );

    void AnalyzeAssignment(
        AssignmentNode& node
    );

    Type AnalyzeUnary(
        UnaryNode& node
    );

    Type AnalyzeBinary(
        BinaryNode& node
    );

    Type AnalyzeExpression(
        Node& node
    );

private:
    Program& m_Program;
    SemanticContext& m_Semantic;
    Diagnostics& m_Diagnostics;
    std::uint32_t m_PointerBits = 64;
    // Parâmetros da função em análise: &param não é suportado pelo backend.
    std::unordered_set<std::string> m_CurrentParameters;
    std::optional<Type> m_CurrentReturnType;
    std::string m_CurrentFunctionName;
};

// Valida o programa inteiro depois que todos os módulos foram analisados.
void ValidateSemantic(
    const SemanticContext& semantic,
    Diagnostics& diagnostics
);

}
