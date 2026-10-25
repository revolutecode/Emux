#pragma once

#include <unordered_map>

#include <Emux/Compiler/CompilerContext.hpp>
#include <Emux/Compiler/Type/Type.hpp>
#include <Emux/Compiler/AST/VariableNode.hpp>
#include <Emux/Compiler/AST/VariableCallNode.hpp>
#include <Emux/Compiler/AST/FunctionNode.hpp>
#include <Emux/Compiler/AST/FunctionCallNode.hpp>
#include <Emux/Compiler/AST/AssignmentNode.hpp>
#include <Emux/Compiler/AST/BinaryNode.hpp>
#include <Emux/Compiler/AST/UnaryNode.hpp>

#include <Emux/Compiler/Semantic/FunctionSignature.hpp>

namespace Emux
{

enum class VisitState
{
    NotVisited,
    Visiting,
    Visited
};

class SemanticAnalyzer
{
public:
    explicit SemanticAnalyzer(
        CompilerContext& context
    );

    void Analyze();

private:
    void AnalyzeVariable(
        VariableNode& variable
    );

    Type AnalyzeVariableCall(
        VariableCallNode& variable
    );

    void AnalyzeFunction(
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

    bool IsVisited(std::string_view name);
    bool IsVisiting(std::string_view name);
    bool IsVisitedOrVisiting(std::string_view name);

private:
    CompilerContext& m_Context;
    std::unordered_map<std::string_view, VisitState> m_VisitStates;
};

}
