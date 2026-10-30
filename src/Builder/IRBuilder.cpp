#include "Emux/Builder/IRBuilder.hpp"
#include "Emux/Builder/IRExpressionBuilder.hpp"
#include "Emux/Builder/IROptimizer.hpp"
#include "Emux/Compiler/AST/AssignmentNode.hpp"
#include "Emux/Compiler/AST/FunctionNode.hpp"
#include "Emux/Compiler/AST/IRNode.hpp"
#include "Emux/Compiler/AST/NodeType.hpp"
#include "Emux/Compiler/AST/Program.hpp"
#include "Emux/Compiler/AST/ReturnNode.hpp"
#include "Emux/Compiler/AST/VariableCallNode.hpp"
#include "Emux/Compiler/AST/VariableNode.hpp"
#include "Emux/Compiler/AttrSet.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Compiler/Type/TypeParser.hpp"
#include "Emux/Core/Abi.hpp"
#include "Emux/Core/Logger.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Emux
{

namespace
{

std::string RawName(std::string_view scoped)
{
    const auto separator = scoped.rfind("::");
    if (separator == std::string_view::npos)
    {
        return std::string(scoped);
    }

    return std::string(scoped.substr(separator + 2));
}

Type ParameterType(const FunctionParameter& parameter, std::uint32_t pointerBits)
{
    auto type = TypeParser::Parse(parameter.Type.Text, pointerBits);
    if (!type.has_value() || !type->IsValid())
    {
        throw std::runtime_error(
            "Tipo invalido '" + parameter.Type.Text +
            "' no parametro '" + parameter.Name.Text + "'");
    }

    return *type;
}

std::size_t ParameterStackBytes(
    const std::vector<FunctionParameter>& parameters,
    std::uint32_t pointerBits,
    Abi abi
)
{
    std::vector<std::uint32_t> byteSizes;
    for (const auto& parameter : parameters)
    {
        byteSizes.push_back(ParameterType(parameter, pointerBits).GetByteSize());
    }

    return AbiStackBytes(
        abi,
        byteSizes,
        pointerBits == 32 ? Architecture::X86 : Architecture::X86_64
    );
}

Abi FunctionAbi(const FunctionNode& function)
{
    if (function.Attrs.Has(AttrId::Stdcall))
    {
        return Abi::Stdcall;
    }

    if (function.Attrs.Has(AttrId::Fastcall))
    {
        return Abi::Fastcall;
    }

    return Abi::Cdecl;
}

void CollectVariableUses(
    const Node& node,
    std::unordered_set<std::string>& uses
)
{
    if (node.GetType() == NodeType::VariableCall)
    {
        uses.insert(static_cast<const VariableCallNode&>(node).Name.Text);
    }
    else if (node.GetType() == NodeType::Assign)
    {
        uses.insert(static_cast<const AssignmentNode&>(node).Name.Text);
    }

    for (const auto& child : node.Children)
    {
        if (child)
        {
            CollectVariableUses(*child, uses);
        }
    }
}

}

IRBuilder::IRBuilder()
    : m_Context(std::make_unique<IRContext>())
{
}

IRBuilder::~IRBuilder() = default;

std::string IRBuilder::Build(const Program& program)
{
    return Build(program, nullptr);
}

std::string IRBuilder::Build(
    const Program& program,
    const std::unordered_map<std::string, Type>* globalVariables,
    const std::unordered_map<std::string, ImportInfo>* imports,
    const std::unordered_map<std::string, FunctionSignature>* functions,
    std::uint32_t pointerBits
)
{
    m_Context = std::make_unique<IRContext>();
    m_Context->pointerBits = pointerBits;
    m_Context->globalTypes = globalVariables;

    // Assinaturas globais: permitem tipar os argumentos e o retorno das
    // chamadas, inclusive as que atravessam módulos.
    if (functions)
    {
        for (const auto& [name, signature] : *functions)
        {
            m_Context->functionSignatures[IRContext::ReplaceDoublePoints(name)] = &signature;
        }
    }

    // Imports declarados neste módulo: nome IR + símbolo nativo + ABI.
    std::unordered_set<std::string> emittedImports;
    for (const auto& ptr : program.Children)
    {
        if (!ptr || ptr->GetType() != NodeType::Function)
        {
            continue;
        }

        const FunctionNode& function = static_cast<const FunctionNode&>(*ptr);
        if (!function.Attrs.Has(AttrId::Import))
        {
            continue;
        }

        const std::string irName = IRContext::ReplaceDoublePoints(function.Name.Text);
        const Abi abi = FunctionAbi(function);
        const std::size_t stackBytes = ParameterStackBytes(
            function.Parameters,
            m_Context->pointerBits,
            abi
        );

        m_Context->ir << "import " << irName << ' ' << RawName(function.Name.Text)
            << ' ' << AbiName(abi) << ' ' << stackBytes << '\n';
        emittedImports.insert(irName);
    }

    // Imports declarados por outros módulos e usados neste.
    if (imports)
    {
        for (const auto& [name, info] : *imports)
        {
            const std::string irName = IRContext::ReplaceDoublePoints(name);
            if (emittedImports.contains(irName))
            {
                continue;
            }

            m_Context->ir << "import " << irName << ' ' << info.Symbol
                << ' ' << AbiName(info.CallingConvention) << ' ' << info.StackSize << '\n';
            emittedImports.insert(irName);
        }
    }

    // Globais definidos neste módulo.
    for (const auto& ptr : program.Children)
    {
        if (!ptr || ptr->GetType() != NodeType::Variable)
        {
            continue;
        }

        BuildVariable(static_cast<const VariableNode&>(*ptr));
    }

    // Globais de outros módulos usadas neste: o backend precisa do tipo para
    // dimensionar o acesso e declarar o símbolo externo.
    if (globalVariables && !globalVariables->empty())
    {
        std::unordered_set<std::string> locals;
        for (const auto& ptr : program.Children)
        {
            if (ptr && ptr->GetType() == NodeType::Variable)
            {
                locals.insert(static_cast<const VariableNode&>(*ptr).Name.Text);
            }
        }

        std::unordered_set<std::string> uses;
        for (const auto& ptr : program.Children)
        {
            if (!ptr)
            {
                continue;
            }

            if (ptr->GetType() == NodeType::Function)
            {
                const FunctionNode& function = static_cast<const FunctionNode&>(*ptr);
                const auto separator = function.Name.Text.rfind("::");
                const std::string scopePrefix = (separator == std::string::npos)
                    ? function.Name.Text
                    : function.Name.Text.substr(0, separator);

                std::unordered_set<std::string> parameters;
                for (const auto& parameter : function.Parameters)
                {
                    parameters.insert(scopePrefix + "::" + parameter.Name.Text);
                }

                std::unordered_set<std::string> functionUses;
                for (const auto& child : function.Children)
                {
                    if (child)
                    {
                        CollectVariableUses(*child, functionUses);
                    }
                }

                for (const std::string& use : functionUses)
                {
                    if (!parameters.contains(use))
                    {
                        uses.insert(use);
                    }
                }
            }
            else if (ptr->GetType() != NodeType::Variable)
            {
                CollectVariableUses(*ptr, uses);
            }
        }

        std::vector<std::pair<std::string, Type>> externals;
        for (const std::string& use : uses)
        {
            if (locals.contains(use))
            {
                continue;
            }

            auto it = globalVariables->find(use);
            if (it == globalVariables->end())
            {
                continue;
            }

            externals.emplace_back(IRContext::ReplaceDoublePoints(use), it->second);
        }

        std::sort(
            externals.begin(),
            externals.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; }
        );

        for (const auto& [name, type] : externals)
        {
            m_Context->ir << "extern " << name << " : " << type.ToString() << '\n';
        }
    }

    // Símbolos exportados (funções e variáveis).
    for (const auto& ptr : program.Children)
    {
        if (!ptr)
        {
            continue;
        }

        if (ptr->GetType() == NodeType::Variable || ptr->GetType() == NodeType::Function)
        {
            BuildExport(
                *ptr,
                IRContext::ReplaceDoublePoints(ptr->Name.Text),
                RawName(ptr->Name.Text)
            );
        }
    }

    for (const auto& ptr : program.Children)
    {
        if (!ptr)
        {
            continue;
        }

        switch (ptr->GetType())
        {
            case NodeType::Variable:
                break;
            case NodeType::Function:
                BuildFunction(static_cast<FunctionNode&>(*ptr));
                break;
            default:
                BuildExpression(*ptr);
                break;
        }
    }

    return m_Context->ir.str();
}

void IRBuilder::BuildExport(
    const Node& node,
    std::string_view irName,
    std::string_view rawName
)
{
    if (!node.Attrs.Has(AttrId::Export))
    {
        return;
    }

    const std::string_view alias = node.Attrs.Value(AttrId::Export);
    const std::string symbol = alias.empty() ? std::string(rawName) : std::string(alias);

    m_Context->ir << "export " << irName << ' ' << symbol << '\n';
}

void IRBuilder::BuildFunction(const FunctionNode& function)
{
    if (function.Attrs.Has(AttrId::Import))
    {
        return;
    }

    m_Context->ResetFunction();

    // Otimização com base em AttrSet: noexcept / inline
    if (function.Attrs.Has(AttrId::NoExcept))
    {
        m_Context->ir << "; [attr: noexcept]\n";
    }
    if (function.Attrs.Has(AttrId::Inline) || function.Attrs.Has(AttrId::AlwaysInline))
    {
        m_Context->ir << "; [attr: inline]\n";
    }

    const std::string irName = IRContext::ReplaceDoublePoints(function.Name.Text);
    const Abi abi = FunctionAbi(function);
    const std::size_t stackBytes = ParameterStackBytes(
        function.Parameters,
        m_Context->pointerBits,
        abi
    );

    m_Context->funcName = irName;
    m_Context->funcRet = function.EvaluatedType.IsValid()
        ? function.EvaluatedType
        : Type::MakeVoid();

    m_Context->ir << "func " << irName << " : " << m_Context->funcRet.ToString()
        << ' ' << AbiName(abi) << ' ' << stackBytes << '\n';

    std::string scopedName = function.Name.Text;
    const auto scopeSeparator = scopedName.rfind("::");
    const std::string scopePrefix = (scopeSeparator == std::string::npos)
        ? scopedName
        : scopedName.substr(0, scopeSeparator);

    for (const auto& parameter : function.Parameters)
    {
        m_Context->ir << "param " << parameter.Name.Text << " : "
            << ParameterType(parameter, m_Context->pointerBits).ToString() << '\n';
        m_Context->localAliases[scopePrefix + "::" + parameter.Name.Text] = parameter.Name.Text;
    }

    bool endsWithReturn = false;

    for (const auto& ptr : function.Children)
    {
        if (!ptr)
        {
            continue;
        }

        if (ptr->GetType() == NodeType::Return)
        {
            BuildReturn(static_cast<ReturnNode&>(*ptr));
            endsWithReturn = true;
        }
        else
        {
            BuildExpression(*ptr);
            endsWithReturn = false;
        }
    }

    if (!endsWithReturn)
    {
        m_Context->ir << "ret\n";
    }

    m_Context->ir << "end\n";
}

void IRBuilder::BuildReturn(const ReturnNode& ret)
{
    if (!ret.Children.empty() && ret.Children[0] &&
        m_Context->funcRet.IsValid() && !m_Context->funcRet.IsVoid())
    {
        IRExpressionBuilder::ExprResult value = IRExpressionBuilder::GetValue(*ret.Children[0], *m_Context);
        value = IRExpressionBuilder::Coerce(value, m_Context->funcRet, *m_Context);

        m_Context->ir << "ret " << m_Context->funcRet.ToString() << ' ' << value.Text << '\n';
        return;
    }

    m_Context->ir << "ret\n";
}

void IRBuilder::BuildVariable(const VariableNode& node)
{
    const std::string irName = IRContext::ReplaceDoublePoints(node.Name.Text);
    const std::string typeText = node.EvaluatedType.IsValid()
        ? node.EvaluatedType.ToString()
        : node.Type.Text;

    m_Context->ir << "global " << irName << " : " << typeText << '\n';
}

void IRBuilder::BuildAssignment(const AssignmentNode& node)
{
    if (node.Children.empty() || !node.Children[0])
    {
        Logger::Fatal("AssignmentNode null, empty or corrupted");
        return;
    }

    Node& valueNode = *node.Children[0];

    if (node.IsDereference)
    {
        if (!node.Target)
        {
            Logger::Fatal("Dereference assignment without target pointer");
            return;
        }

        IRExpressionBuilder::ExprResult pointer = IRExpressionBuilder::GetValue(*node.Target, *m_Context);
        IRExpressionBuilder::ExprResult value = IRExpressionBuilder::GetValue(valueNode, *m_Context);

        if (value.Ty.IsVoid())
        {
            return;
        }

        Type valueType = node.EvaluatedType.IsValid()
            ? node.EvaluatedType
            : (value.Ty.IsValid() ? value.Ty : Type::MakeUnsigned(8));

        if (valueType.IsVoid())
        {
            valueType = Type::MakeUnsigned(8);
        }

        value = IRExpressionBuilder::Coerce(value, valueType, *m_Context);

        m_Context->ir << "store " << valueType.ToString() << ' '
            << pointer.Text << ", " << value.Text << '\n';
        return;
    }

    IRExpressionBuilder::ExprResult value = IRExpressionBuilder::GetValue(valueNode, *m_Context);

    const std::string irName = IRContext::ReplaceDoublePoints(node.Name.Text);
    std::string varName = irName;
    if (auto alias = m_Context->localAliases.find(node.Name.Text);
        alias != m_Context->localAliases.end())
    {
        varName = alias->second;
    }

    Type targetType = node.EvaluatedType.IsValid()
        ? node.EvaluatedType
        : value.Ty;

    if (!targetType.IsValid() || targetType.IsVoid())
    {
        targetType = Type::MakeUnsigned(32);
    }

    if (value.Ty.IsVoid())
    {
        // O diagnóstico já foi emitido na análise semântica; não gerar
        // instruções Exir malformadas.
        return;
    }

    value = IRExpressionBuilder::Coerce(value, targetType, *m_Context);

    // Otimização: se é const, registar na tabela para propagação.
    m_Context->constValues.erase(irName);
    if (node.Attrs.Has(AttrId::Const))
    {
        if (auto constValue = IROptimizer::TryEvaluateConst(valueNode, *m_Context))
        {
            m_Context->constValues[irName] = *constValue;
        }
    }

    m_Context->ir << "storev " << targetType.ToString() << ' '
        << varName << ", " << value.Text << '\n';
}

void IRBuilder::BuildExpression(Node& node)
{
    switch (node.GetType())
    {
        case NodeType::Assign:
            BuildAssignment(static_cast<AssignmentNode&>(node));
            break;
        case NodeType::FunctionCall:
            (void)IRExpressionBuilder::GetValue(node, *m_Context);
            break;
        case NodeType::IR:
        {
            auto& irNode = static_cast<IRNode&>(node);
            std::string code = (!irNode.Code.Text.empty() && irNode.Code.Text.front() == '\n')
                ? irNode.Code.Text.substr(1)
                : irNode.Code.Text;

            m_Context->ir << code;
            if (!code.empty() && code.back() != '\n')
            {
                m_Context->ir << '\n';
            }
            break;
        }
        case NodeType::Variable:
            BuildVariable(static_cast<VariableNode&>(node));
            break;
        default:
            Logger::Fatal("Node not solved");
            std::exit(-1);
            break;
    }
}

} // namespace Emux
