#pragma once

#include <unordered_set>
#include <unordered_map>
#include <functional>

#include <Emux/Compiler/CompilerContext.hpp>
#include <Emux/Compiler/AST/SectionNode.hpp>
#include <Emux/Compiler/AST/VariableNode.hpp>
#include <Emux/Compiler/AST/VariableCallNode.hpp>
#include <Emux/Compiler/AST/FunctionNode.hpp>
#include <Emux/Compiler/AST/FunctionCallNode.hpp>
#include <Emux/Compiler/AST/AssignmentNode.hpp>

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

    void AnalyzeSection(
        SectionNode& section
    );    

    void AnalyzeDependencies(
        SectionNode& section
    );

    void AnalyzeDependence(
        SectionNode& section,
        const Token& dependency,
        std::string_view rootName
    );

    void AnalyzeVariable(
        VariableNode& variable
    );

    void AnalyzeVariableCall(
        VariableCallNode& variable
    );

    void AnalyzeFunction(
        FunctionNode& function
    );

    void AnalyzeFunctionCall(
        FunctionCallNode& function
    );

    void AnalyzeAssignment(
        AssignmentNode& node
    );

    void AnalyzeExpression(
        Node& node
    );

    bool IsVisited(std::string_view name);
    bool IsVisiting(std::string_view name);
    bool IsVisitedOrVisiting(std::string_view name);


private:

    CompilerContext& m_Context;

    std::unordered_set<std::string> m_Variables;
    std::unordered_set<std::string> m_Functions;
    std::unordered_map<std::string, std::reference_wrapper<const SourceLocation>> m_FunctionCalls;
    std::unordered_map<std::string_view, VisitState> m_VisitStates;
};

}
