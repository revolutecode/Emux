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

    // Funções nativas declaradas neste módulo: as chamadas usam o nome Emux
    // escopado, mas precisam de emitir o símbolo nativo (ex: GetStdHandle).
    std::unordered_set<std::string> localImports;
    for (const auto& ptr : program.Children)
    {
        if (!ptr || ptr->GetType() != NodeType::Function) continue;

        const FunctionNode& function = static_cast<const FunctionNode&>(*ptr);
        if (!function.Attrs.Has(AttrId::Import)) continue;

        const std::string irName = IRContext::ReplaceDoublePoints(function.Name.Text);
        localImports.insert(irName);
        m_Context->importAliases[irName] = RawName(function.Name.Text);
    }

    // Imports declarados por outros módulos: o alias de chamada e a ABI são
    // globais; o backend só precisa do 'extern' correspondente.
    if (imports)
    {
        for (const auto& [name, info] : *imports)
        {
            const std::string irName = IRContext::ReplaceDoublePoints(name);
            m_Context->importAliases[irName] = info.Symbol;

            if (!localImports.contains(irName))
            {
                m_Context->ir << "import " << info.Symbol << ','
                    << AbiName(info.CallingConvention) << ','
                    << info.StackSize << '\n';
            }
        }
    }

    // Convenções não-cdecl de funções conhecidas (inclusive de outros
    // módulos): o backend precisa disto ao emitir chamadas na arquitetura x86.
    if (functions)
    {
        for (const auto& [name, signature] : *functions)
        {
            m_Context->functionSignatures[IRContext::ReplaceDoublePoints(name)] = &signature;

            if (signature.CallingConvention == Abi::Cdecl)
            {
                continue;
            }

            std::vector<std::uint32_t> parameterBytes;
            for (const Type& parameter : signature.ParameterTypes)
            {
                parameterBytes.push_back(parameter.GetByteSize());
            }

            const std::size_t stackSize = AbiStackBytes(
                signature.CallingConvention,
                parameterBytes,
                m_Context->pointerBits == 32 ? Architecture::X86 : Architecture::X86_64
            );

            m_Context->ir << "abi " << IRContext::ReplaceDoublePoints(name) << ','
                << AbiName(signature.CallingConvention) << ','
                << stackSize << '\n';
        }
    }

    // Globais de outros módulos usadas neste: o backend precisa do tipo
    // para dimensionar o operando de memória e declarar o símbolo externo.
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
            m_Context->ir << "extvars " << name << ':' << type.ToString() << '\n';
        }
    }

    // Declarações de variáveis primeiro: com múltiplos arquivos elas podem
    // aparecer depois das funções que as usam.
    for (const auto& ptr : program.Children)
    {
        if (!ptr || ptr->GetType() != NodeType::Variable) continue;
        BuildVariable(static_cast<const VariableNode&>(*ptr));
    }

    for (const auto& ptr : program.Children)
    {
        if (!ptr) continue;

        Node& node = *ptr;

        switch (node.GetType())
        {
            case NodeType::Variable:
                break;
            case NodeType::Function:
                BuildFunction(static_cast<FunctionNode&>(node));
                break;
            default:
                BuildExpression(node);
                break;
        }
    }

    return m_Context->ir.str();
}

void IRBuilder::BuildImport(const FunctionNode& function)
{
    std::vector<std::uint32_t> parameterBytes;
    for (const auto& parameter : function.Parameters)
    {
        auto type = TypeParser::Parse(parameter.Type.Text, m_Context->pointerBits);
        if (!type.has_value())
        {
            continue;
        }

        parameterBytes.push_back(type->GetByteSize());
    }

    const Abi abi = function.Attrs.Has(AttrId::Stdcall)
        ? Abi::Stdcall
        : (function.Attrs.Has(AttrId::Fastcall) ? Abi::Fastcall : Abi::Cdecl);

    const std::size_t stackSize = AbiStackBytes(
        abi,
        parameterBytes,
        m_Context->pointerBits == 32 ? Architecture::X86 : Architecture::X86_64
    );

    m_Context->ir << "import " << RawName(function.Name.Text) << ','
        << AbiName(abi) << ','
        << stackSize << '\n';
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

    m_Context->ir << "export " << irName << ',' << symbol << '\n';
}

void IRBuilder::BuildFunction(const FunctionNode& function)
{
    if (function.Attrs.Has(AttrId::Import))
    {
        BuildImport(function);
        return;
    }

    m_Context->ResetFunction();

    const std::string irName = IRContext::ReplaceDoublePoints(function.Name.Text);
    BuildExport(function, irName, RawName(function.Name.Text));

    // Otimização com base em AttrSet: noexcept / inline
    if (function.Attrs.Has(AttrId::NoExcept))
    {
        m_Context->ir << "; [attr: noexcept]\n";
    }
    if (function.Attrs.Has(AttrId::Inline) || function.Attrs.Has(AttrId::AlwaysInline))
    {
        m_Context->ir << "; [attr: inline]\n";
    }

    m_Context->funcName = irName;
    m_Context->ir << "func " << irName << '\n';
    m_Context->ir << "regs r0:u32,r1:u32,r2:u32,r3:u32\n";

    m_Context->funcRetType = function.ReturnType.Text;
    m_Context->funcRet = function.EvaluatedType;

    const Abi abi = function.Attrs.Has(AttrId::Stdcall)
        ? Abi::Stdcall
        : (function.Attrs.Has(AttrId::Fastcall) ? Abi::Fastcall : Abi::Cdecl);

    if (abi != Abi::Cdecl)
    {
        std::vector<std::uint32_t> parameterBytes;
        for (const auto& parameter : function.Parameters)
        {
            auto type = TypeParser::Parse(parameter.Type.Text, m_Context->pointerBits);
            if (!type.has_value())
            {
                continue;
            }

            parameterBytes.push_back(type->GetByteSize());
        }

        const std::size_t stackSize = AbiStackBytes(
            abi,
            parameterBytes,
            m_Context->pointerBits == 32 ? Architecture::X86 : Architecture::X86_64
        );

        m_Context->ir << "funcabi " << irName << ',' << AbiName(abi) << ',' << stackSize << '\n';
    }

    if (!function.Parameters.empty())
    {
        std::string rawFuncName = function.Name.Text;
        auto scopeSep = rawFuncName.rfind("::");
        std::string scopePrefix = (scopeSep == std::string::npos)
            ? std::string()
            : rawFuncName.substr(0, scopeSep);

        m_Context->ir << "args ";
        for (size_t i = 0; i < function.Parameters.size(); i++)
        {
            auto& param = function.Parameters[i];
            m_Context->ir << param.Name.Text << ':' << param.Type.Text;

            if (auto type = TypeParser::Parse(param.Type.Text, m_Context->pointerBits);
                type.has_value())
            {
                m_Context->localTypes[param.Name.Text] = *type;
            }

            if (!scopePrefix.empty())
            {
                m_Context->localAliases[scopePrefix + "::" + param.Name.Text] = param.Name.Text;
            }

            if (i < function.Parameters.size() - 1)
            {
                m_Context->ir << ',';
            }
        }
        m_Context->ir << '\n';
    }

    bool endsWithReturn = false;

    for (const auto& ptr : function.Children)
    {
        if (!ptr) continue;
        Node& node = *ptr;

        switch (node.GetType())
        {
            case NodeType::Return:
                BuildReturn(static_cast<ReturnNode&>(node));
                endsWithReturn = true;
                break;
            default:
                BuildExpression(node);
                endsWithReturn = false;
                break;
        }
    }

    // Funções void (ou sem return explícito) precisam de um ret final.
    if (!endsWithReturn)
    {
        m_Context->ir << "ret\n";
    }
}

void IRBuilder::BuildReturn(const ReturnNode& ret)
{
    if (!ret.Children.empty() && ret.Children[0])
    {
        Node& node = *ret.Children[0];
        const auto value = IRExpressionBuilder::GetValue(node, *m_Context);

        if (IRContext::IsWide(m_Context->funcRet))
        {
            m_Context->ir << "ret64 " << value.Text << ',' << m_Context->funcRet.ToString() << '\n';
            return;
        }

        m_Context->ir << "mov rr0," << value.Text << '\n';
        if (IRContext::LooksLikeReg(value.Text))
        {
            m_Context->ReleaseReg(value.Text);
        }
    }

    m_Context->ir << "ret\n";
}

void IRBuilder::BuildVariable(const VariableNode& node)
{
    const std::string irName = IRContext::ReplaceDoublePoints(node.Name.Text);
    BuildExport(node, irName, RawName(node.Name.Text));
    m_Context->ir << "vars " << irName << ':' << node.Type.Text << '\n';
}

void IRBuilder::BuildAssignment(const AssignmentNode& node)
{
    if (node.Children.empty() || !node.Children[0])
    {
        Logger::Fatal("AssignmentNode null, empty or corrupted");
        return;
    }

    Node& a = *node.Children[0];

    if (node.IsDereference)
    {
        if (!node.Target)
        {
            Logger::Fatal("Dereference assignment without target pointer");
            return;
        }

        const auto pointer = IRExpressionBuilder::GetValue(*node.Target, *m_Context);
        const auto value = IRExpressionBuilder::GetValue(a, *m_Context);

        Type valueType = node.EvaluatedType.IsValid()
            ? node.EvaluatedType
            : value.ValueType;

        if (!valueType.IsValid() || valueType.IsVoid())
        {
            valueType = Type::MakeUnsigned(8);
        }

        m_Context->ir << "dstore " << pointer.Text << ',' << value.Text
            << ',' << valueType.ToString() << '\n';

        if (IRContext::LooksLikeReg(value.Text))
        {
            m_Context->ReleaseReg(value.Text);
        }
        return;
    }

    const auto result = IRExpressionBuilder::GetValue(a, *m_Context);
    std::string varName = IRContext::ReplaceDoublePoints(node.Name.Text);

    if (auto alias = m_Context->localAliases.find(node.Name.Text); alias != m_Context->localAliases.end())
    {
        varName = alias->second;
    }

    const Type targetType = node.EvaluatedType;

    // Otimização: se é const, registrar na tabela para constante propagação
    if (a.Attrs.Has(AttrId::Const) && !IRContext::IsWide(targetType))
    {
        if (auto constVal = IROptimizer::TryEvaluateConst(a, *m_Context))
        {
            m_Context->constValues[varName] = *constVal;
        }
    }

    if (IRContext::IsWide(targetType))
    {
        const std::string sourceType = result.ValueType.IsValid()
            ? result.ValueType.ToString()
            : targetType.ToString();
        m_Context->ir << "mov64 " << varName << ',' << result.Text << ',' << sourceType << '\n';
    }
    else
    {
        m_Context->ir << "mov " << varName << ',' << result.Text << '\n';
    }

    if (IRContext::LooksLikeReg(result.Text))
    {
        m_Context->ReleaseReg(result.Text);
    }
}

void IRBuilder::BuildExpression(Node& node)
{
    switch (node.GetType())
    {
        case NodeType::Assign:
            BuildAssignment(static_cast<AssignmentNode&>(node));
            break;
        case NodeType::FunctionCall:
            (void)IRExpressionBuilder::BuildFunctionCall(static_cast<FunctionCallNode&>(node), *m_Context);
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
