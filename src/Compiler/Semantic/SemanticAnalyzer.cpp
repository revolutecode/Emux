#include <Emux/Compiler/Semantic/SemanticAnalyzer.hpp>
#include <Emux/Compiler/AST/LiteralNode.hpp>
#include <Emux/Compiler/AST/ReturnNode.hpp>
#include <Emux/Compiler/Type/TypeParser.hpp>

#include <cstddef>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Emux
{

namespace
{

std::string RawFunctionName(std::string_view scoped)
{
    const auto separator = scoped.rfind("::");
    if (separator == std::string_view::npos)
    {
        return std::string(scoped);
    }

    return std::string(scoped.substr(separator + 2));
}

bool IsValidSymbolName(std::string_view name)
{
    if (name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_'))
    {
        return false;
    }

    for (char c : name)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
        {
            return false;
        }
    }

    return true;
}

Abi AttributeAbi(const AttrSet& attrs)
{
    if (attrs.Has(AttrId::Stdcall)) return Abi::Stdcall;
    if (attrs.Has(AttrId::Fastcall)) return Abi::Fastcall;
    return Abi::Cdecl;
}

int AttributeAbiCount(const AttrSet& attrs)
{
    int count = 0;
    count += attrs.Has(AttrId::Stdcall) ? 1 : 0;
    count += attrs.Has(AttrId::Cdecl) ? 1 : 0;
    count += attrs.Has(AttrId::Fastcall) ? 1 : 0;
    return count;
}

bool IsValidLibraryName(std::string_view name)
{
    if (name.empty())
    {
        return true;
    }

    for (char c : name)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.')
        {
            return false;
        }
    }

    return true;
}

}

SemanticAnalyzer::SemanticAnalyzer(
    Program& program,
    SemanticContext& semantic,
    Diagnostics& diagnostics,
    std::uint32_t pointerBits
):
    m_Program(program),
    m_Semantic(semantic),
    m_Diagnostics(diagnostics),
    m_PointerBits(pointerBits)
{
}

void SemanticAnalyzer::RegisterDeclarations()
{
    // Variáveis globais primeiro: funções podem usá-las em qualquer ordem.
    for (auto& child : m_Program.Children)
    {
        if (!child || child->GetType() != NodeType::Variable) continue;
        AnalyzeVariable(*static_cast<VariableNode*>(child.get()));
    }

    for (auto& child : m_Program.Children)
    {
        if (!child || child->GetType() != NodeType::Function) continue;
        DeclareFunction(*static_cast<FunctionNode*>(child.get()));
    }
}

void SemanticAnalyzer::AnalyzeBodies()
{
    for (auto& child : m_Program.Children)
    {
        if (!child || child->GetType() != NodeType::Function) continue;
        AnalyzeFunctionBody(*static_cast<FunctionNode*>(child.get()));
    }
}

void SemanticAnalyzer::AnalyzeVariable(
    VariableNode& variable
)
{
    std::string name = variable.Name.Text;

    if(m_Semantic.Variables.contains(name))
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Name.Location,
            "Variable '" + name + "' already defined."
        );
        return;
    }

    auto type = TypeParser::Parse(variable.Type.Text, m_PointerBits);

    if(!type.has_value() || !type->IsValid())
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            variable.Type.Location,
            "Unknown or invalid type '" + variable.Type.Text + "'."
        );
        variable.EvaluatedType = Type::MakeInvalid();
        m_Semantic.Variables.emplace(name, variable.EvaluatedType);
        return;
    }

    variable.EvaluatedType = *type;
    m_Semantic.Variables.emplace(name, *type);

    if (variable.Attrs.Has(AttrId::Export))
    {
        std::string symbol = variable.Attrs.Value(AttrId::Export).empty()
            ? RawFunctionName(name)
            : std::string(variable.Attrs.Value(AttrId::Export));

        if (!IsValidSymbolName(symbol))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                variable.Name.Location,
                "Invalid export symbol '" + symbol + "' for variable '" + name + "'."
            );
        }
    }
}

Type SemanticAnalyzer::AnalyzeVariableCall(
    VariableCallNode& variable
)
{
    std::string name = variable.Name.Text;

    auto it = m_Semantic.Variables.find(name);
    if(it == m_Semantic.Variables.end())
    {
        m_Diagnostics.Add(
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

void SemanticAnalyzer::DeclareFunction(
    FunctionNode& function
)
{
    std::string name = function.Name.Text;
    if(m_Semantic.Functions.contains(name))
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            function.Name.Location,
            "Function '" + name + "' already defined."
        );
    }

    auto rtype = TypeParser::Parse(function.ReturnType.Text, m_PointerBits);
    Type funcRetType = Type::MakeVoid();

    if(!rtype.has_value() || !rtype->IsValid())
    {
        m_Diagnostics.Add(
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

    const int abiCount = AttributeAbiCount(function.Attrs);
    if (abiCount > 1)
    {
        m_Diagnostics.Add(
            DiagnosticLevel::Error,
            function.Name.Location,
            "Function '" + name + "' has more than one calling convention attribute."
        );
    }

    FunctionSignature signature{
        .ReturnType = funcRetType,
        .ParameterTypes = {},
        .CallingConvention = AttributeAbi(function.Attrs),
        .Location = function.Name.Location
    };

    std::unordered_set<std::string_view> params;

    for (const auto& param : function.Parameters)
    {
        const std::string& pname = param.Name.Text;

        if(params.contains(pname))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                param.Name.Location,
                "Parameter '" + pname + "' in function '" + name + "' already defined."
            );
            continue;
        }

        params.insert(pname);

        auto ptype = TypeParser::Parse(param.Type.Text, m_PointerBits);

        if(!ptype.has_value() || !ptype->IsValid())
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                param.Type.Location,
                "Unknown parameter type '" + param.Type.Text + "' in function '" + name + "'."
            );
            signature.ParameterTypes.push_back(Type::MakeInvalid());
        }
        else
        {
            signature.ParameterTypes.push_back(*ptype);
        }
    }

    m_Semantic.Functions.insert_or_assign(name, signature);

    if (function.Attrs.Has(AttrId::Import))
    {
        if (function.Attrs.Has(AttrId::Export))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                function.Name.Location,
                "Function '" + name + "' cannot be both imported and exported."
            );
        }

        ImportInfo info;
        info.Symbol = RawFunctionName(name);
        info.Library = std::string(function.Attrs.Value(AttrId::Import));
        info.CallingConvention = signature.CallingConvention;

        if (!IsValidSymbolName(info.Symbol))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                function.Name.Location,
                "Invalid native symbol name '" + info.Symbol + "'."
            );
        }

        if (!IsValidLibraryName(info.Library))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                function.Name.Location,
                "Invalid library name '" + info.Library + "' in [[import]]."
            );
        }

        std::vector<std::uint32_t> parameterBytes;
        for (const Type& parameter : signature.ParameterTypes)
        {
            parameterBytes.push_back(parameter.GetByteSize());
        }

        info.StackSize = AbiStackBytes(
            info.CallingConvention,
            parameterBytes,
            m_PointerBits == 32 ? Architecture::X86 : Architecture::X86_64
        );

        m_Semantic.Imports.insert_or_assign(name, std::move(info));
    }
    else if (function.Attrs.Has(AttrId::Export))
    {
        std::string symbol = function.Attrs.Value(AttrId::Export).empty()
            ? RawFunctionName(name)
            : std::string(function.Attrs.Value(AttrId::Export));

        if (!IsValidSymbolName(symbol))
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                function.Name.Location,
                "Invalid export symbol '" + symbol + "' for function '" + name + "'."
            );
        }
    }
}

void SemanticAnalyzer::AnalyzeFunctionBody(
    FunctionNode& function
)
{
    std::string name = function.Name.Text;
    if (!m_Semantic.Functions.contains(name))
    {
        return;
    }

    auto scopeEnd = name.rfind("::");
    std::string scopePrefix = (scopeEnd == std::string::npos) ? name : name.substr(0, scopeEnd);

    std::unordered_set<std::string_view> params;
    std::vector<std::pair<std::string, std::optional<Type>>> parameterVariables;

    m_CurrentParameters.clear();

    for (const auto& param : function.Parameters)
    {
        const std::string& pname = param.Name.Text;

        if (!params.insert(pname).second)
        {
            continue;
        }

        auto ptype = TypeParser::Parse(param.Type.Text, m_PointerBits);
        if(!ptype.has_value() || !ptype->IsValid())
        {
            continue;
        }

        std::string key = scopePrefix + "::" + pname;
        m_CurrentParameters.insert(key);
        auto existing = m_Semantic.Variables.find(key);
        parameterVariables.emplace_back(
            key,
            existing != m_Semantic.Variables.end()
                ? std::optional<Type>(existing->second)
                : std::nullopt
        );
        m_Semantic.Variables.insert_or_assign(key, *ptype);
    }

    for (auto& child : function.Children)
    {
        if (child) AnalyzeExpression(*child);
    }

    for (auto it = parameterVariables.rbegin(); it != parameterVariables.rend(); ++it)
    {
        if (it->second.has_value())
        {
            m_Semantic.Variables.insert_or_assign(it->first, *it->second);
        }
        else
        {
            m_Semantic.Variables.erase(it->first);
        }
    }

    m_CurrentParameters.clear();
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
            m_Diagnostics.Add(
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
            m_Diagnostics.Add(
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

    m_Semantic.FunctionCalls[name].emplace_back(std::move(signature));

    auto it = m_Semantic.Functions.find(name);
    if (it != m_Semantic.Functions.end())
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

    if (node.IsDereference)
    {
        if (node.Target)
        {
            Type pointerType = AnalyzeExpression(*node.Target);

            if (!pointerType.IsPointer())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Dereference assignment requires a pointer, but got '" + pointerType.ToString() + "'."
                );
            }
            else
            {
                targetType = pointerType.SubType ? *pointerType.SubType : Type::MakeUnsigned(8);
                if (targetType.IsVoid())
                {
                    targetType = Type::MakeUnsigned(8);
                }
            }
        }
    }
    else
    {
        auto it = m_Semantic.Variables.find(name);
        if (it == m_Semantic.Variables.end())
        {
            m_Diagnostics.Add(
                DiagnosticLevel::Error,
                node.Name.Location,
                "Variable '" + name + "' not defined."
            );
        }
        else
        {
            targetType = it->second;
        }
    }

    if (node.Children.empty() || !node.Children[0])
    {
        m_Diagnostics.Add(
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
            m_Diagnostics.Add(
                DiagnosticLevel::Warning,
                node.Name.Location,
                "Implicit conversion from '" + exprType.ToString() + "' to '" + targetType.ToString() + "' in assignment to '" + (node.IsDereference ? "*" + name : name) + "'."
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
                m_Diagnostics.Add(
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
                m_Diagnostics.Add(
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
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Unary operator '!' requires integer operand, but got '" + operandType.ToString() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = Type::MakeUnsigned(8);
            return node.EvaluatedType;

        case UnaryOperation::AddressOf:
        {
            Node& operand = *node.Children[0];

            if (operand.GetType() != NodeType::VariableCall)
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "The '&' operator requires a variable."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            const std::string& name = operand.Name.Text;

            if (m_CurrentParameters.contains(name))
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Taking the address of parameter '" + name + "' is not supported; use a section variable."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            node.EvaluatedType = Type::MakePointer(operandType, m_PointerBits);
            return node.EvaluatedType;
        }

        case UnaryOperation::Deref:
        {
            if (!operandType.IsPointer())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "The '*' operator requires a pointer, but got '" + operandType.ToString() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }

            if (!operandType.SubType || operandType.SubType->IsVoid())
            {
                node.EvaluatedType = Type::MakeUnsigned(8);
                return node.EvaluatedType;
            }

            node.EvaluatedType = *operandType.SubType;
            return node.EvaluatedType;
        }
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
            // Aritmética de ponteiro em bytes: p + n / p - n.
            if (leftType.IsPointer() && rightType.IsInteger())
            {
                node.EvaluatedType = leftType;
                return node.EvaluatedType;
            }

            if (leftType.IsInteger() && rightType.IsPointer() &&
                node.Operation == BinaryOperation::Add)
            {
                node.EvaluatedType = rightType;
                return node.EvaluatedType;
            }

            if (!leftType.IsNumeric() || !rightType.IsNumeric())
            {
                m_Diagnostics.Add(
                    DiagnosticLevel::Error,
                    node.GetLocation(),
                    "Arithmetic operator requires numeric operands, but got '" + leftType.ToString() + "' and '" + rightType.ToString() + "'."
                );
                node.EvaluatedType = Type::MakeInvalid();
                return node.EvaluatedType;
            }
            node.EvaluatedType = leftType.GetCommonType(rightType);
            return node.EvaluatedType;

        case BinaryOperation::Mul:
        case BinaryOperation::Div:
            if (!leftType.IsNumeric() || !rightType.IsNumeric())
            {
                m_Diagnostics.Add(
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
                m_Diagnostics.Add(
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
            auto parsed = TypeParser::Parse(lit.Type.Text, m_PointerBits);
            lit.EvaluatedType = (parsed.has_value() && parsed->IsValid())
                ? *parsed
                : Type::MakeSigned(32);
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

void ValidateSemantic(
    const SemanticContext& semantic,
    Diagnostics& diagnostics
)
{
    for (const auto& [name, signatures] : semantic.FunctionCalls)
    {
        auto it = semantic.Functions.find(name);

        if (it == semantic.Functions.end())
        {
            for (const FunctionCallSignature& signature : signatures)
            {
                diagnostics.Add(
                    DiagnosticLevel::Error,
                    signature.Location,
                    "Function '" + name + "' not defined."
                );
            }
            continue;
        }

        const FunctionSignature& functionSignature = it->second;
        std::size_t numberOfParams = functionSignature.ParameterTypes.size();

        for (const FunctionCallSignature& signature : signatures)
        {
            std::size_t numberOfParamsReceived = signature.ParameterTypes.size();

            if (numberOfParams != numberOfParamsReceived)
            {
                diagnostics.Add(
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
                const Type& left = signature.ParameterTypes[i];
                const Type& right = functionSignature.ParameterTypes[i];

                if (!left.IsValid() || !right.IsValid()) continue;

                if (!right.CanAssignFrom(left))
                {
                    diagnostics.Add(
                        DiagnosticLevel::Error,
                        signature.Location,
                        "Impossible conversion of " +
                        left.ToString() + " to " +
                        right.ToString() +
                        " in function call '"+
                        name +
                        "'."
                    );
                    break;
                }
            }
        }
    }

    auto it = semantic.Scopes.find("main");
    if (it == semantic.Scopes.end())
    {
        diagnostics.Add(
            DiagnosticLevel::Fatal,
            {},
            "Section 'Main' not found! This is the entry point."
        );
    }

    if (!semantic.Functions.contains("main::Start") && !semantic.Functions.contains("main::_Start"))
    {
        diagnostics.Add(
            DiagnosticLevel::Fatal,
            {},
            "Function 'Main::Start' or 'Main::_Start' not found! This is the entry point."
        );
    }

    for (const char* entry : { "main::Start", "main::_Start" })
    {
        if (semantic.Imports.contains(entry))
        {
            diagnostics.Add(
                DiagnosticLevel::Fatal,
                {},
                "Entry point function '" + std::string(entry) + "' cannot be imported."
            );
        }
    }
}

}
