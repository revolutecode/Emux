#include <Emux/Compiler/Semantic/SemanticAnalyzer.hpp>
#include <Emux/Compiler/AST/Program.hpp>
#include <Emux/Compiler/AST/LiteralNode.hpp>
#include <Emux/Compiler/AST/ReturnNode.hpp>
#include <Emux/Compiler/Type/TypeParser.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Emux
{

SemanticAnalyzer::SemanticAnalyzer(
    CompilerContext& context
):
    m_Context(context)
{
}

void SemanticAnalyzer::Analyze()
{
    auto& program = *m_Context.AST;
    m_VisitStates.clear();

    for(auto& child : program.Children)
    {
        if (!child) continue;

        NodeType type = child->GetType();
        if(type == NodeType::Variable)
        {
            VariableNode* variable = static_cast<VariableNode*>(child.get()); 
            AnalyzeVariable(*variable);
        }
        else if(type == NodeType::Function)
        {
            FunctionNode* function = dynamic_cast<FunctionNode*>(child.get()); 
            AnalyzeFunction(*function);
        }
    }

    for (auto&& [name, signatures] : m_Context.FunctionCalls)
    {
        auto it = m_Context.Functions.find(name);

        if (it == m_Context.Functions.end())
        {
            for (FunctionCallSignature& signature : signatures)
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Error,
                    signature.Location,
                    "Function '" + name + "' not defined."
                );
            }
            continue;
        }

        FunctionSignature functionSignature = it->second;
        std::size_t numberOfParams = functionSignature.ParameterTypes.size();

        for (FunctionCallSignature& signature : signatures)
        {
            std::size_t numberOfParamsReceived = signature.ParameterTypes.size();

            if (numberOfParams != numberOfParamsReceived)
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Error,
                    signature.Location,
                    "Function '" + 
                    name + "' expects " + std::to_string(numberOfParams) +
                    ", but received " + std::to_string(numberOfParamsReceived)
                );
                break;
            }

            for (size_t i = 0; i < numberOfParams; ++i)
            {
                Type& left = signature.ParameterTypes[i];
                Type& right = functionSignature.ParameterTypes[i];

                if (!left.IsValid() || !right.IsValid()) continue;

                if (!right.CanAssignFrom(left))
                {
                    m_Context.Diagnostics.Add(
                        DiagnosticLevel::Error,
                        signature.Location,
                        "Impossible conversion of " +
                        left.ToString() + " to " +
                        right.ToString() +
                        " in function call '"+
                        it->first +
                        "'."
                    );
                    break;
                }

            }
        }
    }

    auto it = m_Context.Scopes.find("main");
    if (it == m_Context.Scopes.end())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Fatal,
            {},
            "Section 'Main' not found! This is the entry point."
        );
    }

    if (!m_Context.Functions.contains("main::Start") && !m_Context.Functions.contains("main::_Start"))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Fatal,
            {},
            "Function 'Main::Start' or 'Main::_Start' not found! This is the entry point."
        );
    }
}

void SemanticAnalyzer::AnalyzeVariable(
    VariableNode& variable
)
{
    std::string name = variable.Name.Text;

    if(m_Context.Variables.contains(name))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Name.Location,
            "Variable '" + name + "' already defined."
        );
        return;
    }

    auto type = TypeParser::Parse(variable.Type.Text);

    if(!type.has_value() || !type->IsValid())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Type.Location,
            "Unknown or invalid type '" + variable.Type.Text + "'."
        );
        variable.EvaluatedType = Type::MakeInvalid();
        m_Context.Variables.emplace(name, variable.EvaluatedType);
        return;
    }

    variable.EvaluatedType = *type;
    m_Context.Variables.emplace(name, *type);
}

Type SemanticAnalyzer::AnalyzeVariableCall(
    VariableCallNode& variable
)
{
    std::string name = variable.Name.Text;

    auto it = m_Context.Variables.find(name);
    if(it == m_Context.Variables.end())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Name.Location,
            "Variable '" + name + "' not defined."
        );
        variable.EvaluatedType = Type::MakeInvalid();
        return variable.EvaluatedType;
    }

    variable.EvaluatedType = it->second;
    return variable.EvaluatedType;
}

void SemanticAnalyzer::AnalyzeFunction(
    FunctionNode& function
)
{
    std::string name = function.Name.Text;
    if(m_Context.Functions.contains(name))
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            function.Name.Location,
            "Function '" + name + "' already defined."
        );
    }

    auto rtype = TypeParser::Parse(function.ReturnType.Text);
    Type funcRetType = Type::MakeVoid();

    if(!rtype.has_value() || !rtype->IsValid())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            function.ReturnType.Location,
            "Unknown return type '" + function.ReturnType.Text + "' in function '" + name + "'."
        );
    }
    else
    {
        funcRetType = *rtype;
    }

    function.EvaluatedType = funcRetType;

    FunctionSignature signature{
        .ReturnType = funcRetType,
        .ParameterTypes = {},
        .Location = function.Name.Location
    };

    std::unordered_set<std::string_view> params;
    std::vector<std::pair<std::string, std::optional<Type>>> parameterVariables;

    auto scopeEnd = name.rfind("::");
    std::string scopePrefix = (scopeEnd == std::string::npos) ? name : name.substr(0, scopeEnd);

    for (const auto& param : function.Parameters)
    {
        const std::string& pname = param.Name.Text;

        if(params.contains(pname))
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                param.Name.Location,
                "Parameter '" + pname + "' in function '" + name + "' already defined."
            );
            continue;
        }

        params.insert(pname);

        auto ptype = TypeParser::Parse(param.Type.Text);

        if(!ptype.has_value() || !ptype->IsValid())
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                param.Type.Location,
                "Unknown parameter type '" + param.Type.Text + "' in function '" + name + "'."
            );
            signature.ParameterTypes.push_back(Type::MakeInvalid());
        }
        else
        {
            signature.ParameterTypes.push_back(*ptype);

            std::string key = scopePrefix + "::" + pname;
            auto existing = m_Context.Variables.find(key);
            parameterVariables.emplace_back(
                key,
                existing != m_Context.Variables.end()
                    ? std::optional<Type>(existing->second)
                    : std::nullopt
            );
            m_Context.Variables.insert_or_assign(key, *ptype);
        }
    }

    m_Context.Functions.insert_or_assign(name, signature);

    for (auto& child : function.Children)
    {
        if (child) AnalyzeExpression(*child);
    }

    for (auto it = parameterVariables.rbegin(); it != parameterVariables.rend(); ++it)
    {
        if (it->second.has_value())
        {
            m_Context.Variables.insert_or_assign(it->first, *it->second);
        }
        else
        {
            m_Context.Variables.erase(it->first);
        }
    }
}

Type SemanticAnalyzer::AnalyzeFunctionCall(
    FunctionCallNode& function
)
{
    std::string name = function.Name.Text;

    FunctionCallSignature signature;
    signature.Location = function.GetLocation();

    for (auto& child : function.Children)
    {
        Type type = AnalyzeExpression(*child);

        if (!type.IsValid())
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                signature.Location,
                "Invalid type in parameter of function call '"+
                name +
                "'."
            );
            break;
        }

        if (type.IsVoid())
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Error,
                signature.Location,
                "Never use void as parameter in function '"+
                name +
                "'."
            );
            continue;
        }

        signature.ParameterTypes.push_back(type);
    }

    m_Context.FunctionCalls[name].emplace_back(std::move(signature));

    auto it = m_Context.Functions.find(name);
    if (it != m_Context.Functions.end())
    {
        function.EvaluatedType = it->second.ReturnType;
        return function.EvaluatedType;
    }

    function.EvaluatedType = Type::MakeSigned(32);
    return function.EvaluatedType;
}

void SemanticAnalyzer::AnalyzeAssignment(
    AssignmentNode& node
)
{
    std::string name = node.Name.Text;
    Type targetType = Type::MakeInvalid();

    auto it = m_Context.Variables.find(name);
    if (it == m_Context.Variables.end())
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            node.Name.Location,
            "Variable '" + name + "' not defined."
        );
    }
    else
    {
        targetType = it->second;
    }

    if (node.Children.empty() || !node.Children[0])
    {
        m_Context.Diagnostics.Add(
            DiagnosticLevel::Error,
            node.Name.Location,
            "Assignment expression is empty."
        );
        return;
    }

    Type exprType = AnalyzeExpression(*node.Children[0]);

    if (targetType.IsValid() && exprType.IsValid())
    {
        if (!targetType.CanAssignFrom(exprType))
        {
            m_Context.Diagnostics.Add(
                DiagnosticLevel::Warning,
                node.Name.Location,
                "Implicit conversion from '" + exprType.ToString() + "' to '" + targetType.ToString() + "' in assignment to '" + name + "'."
            );
        }
    }

    node.EvaluatedType = targetType;
}

Type SemanticAnalyzer::AnalyzeUnary(
    UnaryNode& node
)
{
    if (node.Children.empty() || !node.Children[0])
    {
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    Type operandType = AnalyzeExpression(*node.Children[0]);

    if (!operandType.IsValid())
    {
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    switch (node.Operation)
    {
        case UnaryOperation::Neg:
            if (!operandType.IsNumeric())
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Unary operator '-' requires numeric operand, but got '" + operandType.ToString() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = operandType;
            return node.EvaluatedType;

        case UnaryOperation::Not:
            if (!operandType.IsInteger())
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Unary operator '~' requires integer operand, but got '" + operandType.ToString() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = operandType;
            return node.EvaluatedType;

        case UnaryOperation::LogicalNot:
            if (!operandType.IsInteger())
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Unary operator '!' requires integer operand, but got '" + operandType.ToString() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = Type::MakeUnsigned(8);
            return node.EvaluatedType;
    }

    node.EvaluatedType = operandType;
    return node.EvaluatedType;
}

Type SemanticAnalyzer::AnalyzeBinary(
    BinaryNode& node
)
{
    if (node.Children.size() < 2 || !node.Children[0] || !node.Children[1])
    {
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    Type leftType = AnalyzeExpression(*node.Children[0]);
    Type rightType = AnalyzeExpression(*node.Children[1]);

    if (!leftType.IsValid() || !rightType.IsValid())
    {
        node.EvaluatedType = Type::MakeInvalid();
        return node.EvaluatedType;
    }

    switch (node.Operation)
    {
        case BinaryOperation::Add:
        case BinaryOperation::Sub:
        case BinaryOperation::Mul:
        case BinaryOperation::Div:
            if (!leftType.IsNumeric() || !rightType.IsNumeric())
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Arithmetic operator requires numeric operands, but got '" + leftType.ToString() + "' and '" + rightType.ToString() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = leftType.GetCommonType(rightType);
            return node.EvaluatedType;

        case BinaryOperation::And:
        case BinaryOperation::Or:
        case BinaryOperation::Xor:
        case BinaryOperation::Lshift:
        case BinaryOperation::Rshift:
            if (!leftType.IsInteger() || !rightType.IsInteger())
            {
                m_Context.Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Bitwise operator requires integer operands, but got '" + leftType.ToString() + "' and '" + rightType.ToString() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = leftType.GetCommonType(rightType);
            return node.EvaluatedType;
    }

    node.EvaluatedType = leftType.GetCommonType(rightType);
    return node.EvaluatedType;
}

Type SemanticAnalyzer::AnalyzeExpression(
    Node& node
)
{
    const NodeType type = node.GetType();

    switch (type)
    {
        case NodeType::Literal:
        {
            LiteralNode& lit = static_cast<LiteralNode&>(node);
            lit.EvaluatedType = Type::MakeSigned(32);
            return lit.EvaluatedType;
        }

        case NodeType::VariableCall:
        {
            VariableCallNode& varCall = static_cast<VariableCallNode&>(node);
            return AnalyzeVariableCall(varCall);
        }

        case NodeType::FunctionCall:
        {
            FunctionCallNode& funcCall = static_cast<FunctionCallNode&>(node);
            return AnalyzeFunctionCall(funcCall);
        }

        case NodeType::Assign:
        {
            AssignmentNode& assign = static_cast<AssignmentNode&>(node);
            AnalyzeAssignment(assign);
            return assign.EvaluatedType;
        }

        case NodeType::Unary:
        {
            UnaryNode& unary = static_cast<UnaryNode&>(node);
            return AnalyzeUnary(unary);
        }

        case NodeType::Binary:
        {
            BinaryNode& binary = static_cast<BinaryNode&>(node);
            return AnalyzeBinary(binary);
        }

        case NodeType::Return:
        {
            ReturnNode& ret = static_cast<ReturnNode&>(node);
            if (!ret.Children.empty() && ret.Children[0])
            {
                AnalyzeExpression(*ret.Children[0]);
            }
            return Type::MakeVoid();
        }

        default:
            return Type::MakeVoid();
    }
}

bool SemanticAnalyzer::IsVisited(std::string_view name)
{
    if (const auto it = m_VisitStates.find(name); it != m_VisitStates.cend())
    {
        if (it->second == VisitState::Visited){
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::IsVisiting(std::string_view name)
{
    if (const auto it = m_VisitStates.find(name); it != m_VisitStates.cend())
    {
        if (it->second == VisitState::Visiting){
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::IsVisitedOrVisiting(std::string_view name)
{
    if (const auto it = m_VisitStates.find(name); it != m_VisitStates.cend())
    {
        if (it->second != VisitState::NotVisited){
            return true;
        }
    }
    return false;
}

}
