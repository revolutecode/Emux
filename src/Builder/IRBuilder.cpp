#include "Emux/Builder/IRBuilder.hpp"
#include "Emux/Builder/IRExpressionBuilder.hpp"
#include "Emux/Builder/IROptimizer.hpp"
#include "Emux/Compiler/AST/AssignmentNode.hpp"
#include "Emux/Compiler/AST/ControlFlowNodes.hpp"
#include "Emux/Compiler/AST/EnumNode.hpp"
#include "Emux/Compiler/AST/FunctionNode.hpp"
#include "Emux/Compiler/AST/IRNode.hpp"
#include "Emux/Compiler/AST/LiteralNode.hpp"
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
#include "Emux/Utilities/BigInt.hpp"
#include "Emux/Utilities/NumericLiteral.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
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
    if (parameter.EvaluatedType.IsValid())
    {
        return parameter.EvaluatedType;
    }

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

// Bytes iniciais de uma declaracao [[const]]: literal de string num bloco b
// (com NUL e padding a zero) ou literal inteiro little-endian num escalar.
// 'folded' substitui o literal quando o valor ja foi calculado pela analise
// semantica, como no auto-incremento de um enum.
std::vector<std::uint8_t> ConstInitialBytes(
    const Node* init,
    const Type& type,
    const std::optional<BigInt>& folded = std::nullopt
)
{
    std::vector<std::uint8_t> bytes(type.GetByteSize(), 0);

    if (type.IsBuffer())
    {
        if (!init || init->GetType() != NodeType::Literal)
        {
            return bytes;
        }

        const auto& literal = static_cast<const LiteralNode&>(*init);

        if (literal.Value.Type != TokenType::String)
        {
            return bytes;
        }

        const std::size_t count = std::min<std::size_t>(
            bytes.size(),
            literal.Value.Text.size()
        );

        for (std::size_t i = 0; i < count; ++i)
        {
            bytes[i] = static_cast<std::uint8_t>(literal.Value.Text[i]);
        }

        return bytes;
    }

    std::optional<BigInt> value = folded;

    if (!value.has_value())
    {
        if (!init || init->GetType() != NodeType::Literal)
        {
            return bytes;
        }

        const auto& literal = static_cast<const LiteralNode&>(*init);

        if (literal.Value.Type != TokenType::Number)
        {
            return bytes;
        }

        const bool isHex =
            literal.Value.Text.size() > 2 &&
            literal.Value.Text[0] == '0' &&
            (literal.Value.Text[1] == 'x' || literal.Value.Text[1] == 'X');

        value = BigInt::Parse(literal.Value.Text, isHex ? 16 : 10);
    }

    if (!value.has_value())
    {
        return bytes;
    }

    if (type.IsFixed())
    {
        return NumericLiteral::FixedStorageValue(*value, type)
            .ToLittleEndianBytes(bytes.size());
    }

    if (type.IsFloat())
    {
        return NumericLiteral::FloatStorageBits(*value, type)
            .ToLittleEndianBytes(bytes.size());
    }

    return value->ToLittleEndianBytes(bytes.size());
}

void CollectStringLiterals(
    const Node& node,
    IRContext& ctx
)
{
    // O inicializador de uma declaracao [[const]] e emitido como bytes do
    // proprio global, nao como bloco 'data' partilhado.
    if (node.GetType() == NodeType::Variable)
    {
        return;
    }

    if (node.GetType() == NodeType::Literal)
    {
        const auto& literal = static_cast<const LiteralNode&>(node);
        if (literal.Value.Type == TokenType::String)
        {
            ctx.InternString(literal.Value.Text);
        }
    }

    for (const auto& child : node.Children)
    {
        if (child)
        {
            CollectStringLiterals(*child, ctx);
        }
    }
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
    else if (node.GetType() == NodeType::Enum)
    {
        // Os valores dos elementos vivem em Elements, nao em Children.
        for (const EnumElement& element : static_cast<const EnumNode&>(node).Elements)
        {
            if (element.Value)
            {
                CollectVariableUses(*element.Value, uses);
            }
        }
    }

    for (const auto& child : node.Children)
    {
        if (child)
        {
            CollectVariableUses(*child, uses);
        }
    }
}

// Recolhe os nomes IR de todas as funcoes chamadas no modulo (corpos de funcao
// e instrucoes de topo). Usado para podar imports nunca chamados: o nome de uma
// chamada ja vem escopado da analise semantica, tal como o do import.
void CollectFunctionCalls(
    const Node& node,
    std::unordered_set<std::string>& calls
)
{
    if (node.GetType() == NodeType::FunctionCall)
    {
        calls.insert(IRContext::ReplaceDoublePoints(node.GetName().Text));
    }

    // Os nos de controlo de fluxo guardam condicao e blocos em membros proprios
    // (nao em Children), por isso o percurso tem de os visitar explicitamente.
    // Sem isto, uma chamada dentro de um if/while/for ficaria invisivel e o
    // respetivo import seria podado em -O1+.
    switch (node.GetType())
    {
        case NodeType::If:
        {
            const IfNode& flow = static_cast<const IfNode&>(node);
            if (flow.Condition)
            {
                CollectFunctionCalls(*flow.Condition, calls);
            }
            for (const NodePtr& child : flow.ThenBody)
            {
                if (child) CollectFunctionCalls(*child, calls);
            }
            for (const NodePtr& child : flow.ElseBody)
            {
                if (child) CollectFunctionCalls(*child, calls);
            }
            break;
        }
        case NodeType::While:
        {
            const WhileNode& flow = static_cast<const WhileNode&>(node);
            if (flow.Condition)
            {
                CollectFunctionCalls(*flow.Condition, calls);
            }
            for (const NodePtr& child : flow.Body)
            {
                if (child) CollectFunctionCalls(*child, calls);
            }
            break;
        }
        case NodeType::For:
        {
            const ForNode& flow = static_cast<const ForNode&>(node);
            const NodePtr* sections[] = {
                &flow.Init, &flow.Condition, &flow.Step
            };
            for (const NodePtr* section : sections)
            {
                if (*section) CollectFunctionCalls(**section, calls);
            }
            for (const NodePtr& bodyChild : flow.Body)
            {
                if (bodyChild) CollectFunctionCalls(*bodyChild, calls);
            }
            break;
        }
        default:
            break;
    }

    for (const auto& child : node.Children)
    {
        if (child)
        {
            CollectFunctionCalls(*child, calls);
        }
    }
}

// Uma expressao e livre de efeitos se a sua subarvore nao contem chamadas,
// atribuicoes, asm inline ([[IR]]) nem acessos volatile. Uma instrucao-cast
// descartada e livre de efeitos (ex: static_cast<i32>(g)) produz apenas um
// load/cast morto, que em -O1+ nem e emitido.
bool IsSideEffectFree(const Node& node)
{
    if (node.Attrs.Has(AttrId::Volatile))
    {
        return false;
    }

    switch (node.GetType())
    {
        case NodeType::FunctionCall:
        case NodeType::Assign:
        case NodeType::IR:
            return false;
        default:
            break;
    }

    for (const auto& child : node.Children)
    {
        if (child && !IsSideEffectFree(*child))
        {
            return false;
        }
    }

    return true;
}

// Aplica 'x op= y' ao valor atual do alvo. Em ponteiros, += e -= escalam o
// lado direito por sizeof(pointee), como a aritmetica p + n.
IRExpressionBuilder::ExprResult ApplyCompound(
    const IRExpressionBuilder::ExprResult& current,
    const IRExpressionBuilder::ExprResult& rhs,
    BinaryOperation operation,
    const Type& type,
    IRContext& ctx
)
{
    IRExpressionBuilder::ExprResult value = rhs;

    if ((operation == BinaryOperation::Add ||
         operation == BinaryOperation::Sub) &&
        type.IsPointer())
    {
        value = IRExpressionBuilder::ScalePointerOffset(value, type, ctx);
    }

    const IRExpressionBuilder::ExprResult left =
        IRExpressionBuilder::Coerce(current, type, ctx);
    value = IRExpressionBuilder::Coerce(value, type, ctx);

    const std::string dst = ctx.NewTemp();
    ctx.ir << dst << " = "
        << IRExpressionBuilder::BinaryInstructionName(operation, type)
        << ' ' << type.ToString() << ' ' << left.Text << ", " << value.Text << '\n';

    return IRExpressionBuilder::ExprResult{ dst, type, false };
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
    std::uint32_t pointerBits,
    bool optimize
)
{
    m_Context = std::make_unique<IRContext>();
    m_Context->pointerBits = pointerBits;
    m_Context->globalTypes = globalVariables;
    m_Context->optimize = optimize;

    // Os blocos de dados têm de ser emitidos antes das funções no texto Exir.
    CollectStringLiterals(program, *m_Context);
    for (const auto& literal : m_Context->stringLiterals)
    {
        m_Context->ir << "data " << literal.Label << ' ' << literal.Bytes << '\n';
    }

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
    // Em -O1+ um import nunca chamado neste módulo é podado aqui, antes de
    // virar Exir (o gerador já o ignoraria no 'extern', mas o texto ficaria
    // poluído). O nome de uma chamada usa o mesmo ReplaceDoublePoints que
    // BuildFunctionCall emprega para resolver o alvo, logo o casamento é exato.
    std::unordered_set<std::string> calledFunctions;
    if (optimize)
    {
        for (const auto& ptr : program.Children)
        {
            if (ptr)
            {
                CollectFunctionCalls(*ptr, calledFunctions);
            }
        }
    }

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

        if (optimize && !calledFunctions.contains(irName))
        {
            continue;
        }

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

            if (optimize && !calledFunctions.contains(irName))
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
        if (!ptr)
        {
            continue;
        }

        if (ptr->GetType() == NodeType::Variable)
        {
            BuildVariable(static_cast<const VariableNode&>(*ptr));
        }
        else if (ptr->GetType() == NodeType::Enum)
        {
            BuildEnum(static_cast<const EnumNode&>(*ptr));
        }
    }

    // Globais de outros módulos usadas neste: o backend precisa do tipo para
    // dimensionar o acesso e declarar o símbolo externo.
    if (globalVariables && !globalVariables->empty())
    {
        std::unordered_set<std::string> locals;
        for (const auto& ptr : program.Children)
        {
            if (!ptr)
            {
                continue;
            }

            if (ptr->GetType() == NodeType::Variable)
            {
                locals.insert(static_cast<const VariableNode&>(*ptr).Name.Text);
            }
            else if (ptr->GetType() == NodeType::Enum)
            {
                for (const EnumElement& element : static_cast<const EnumNode&>(*ptr).Elements)
                {
                    locals.insert(element.Key);
                }
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
            case NodeType::Using:
            case NodeType::Variable:
            case NodeType::Enum:
                // Já tratados nas passagens de imports e globais.
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

    if (!node.Attrs.Has(AttrId::Const) || !node.EvaluatedType.IsValid())
    {
        m_Context->ir << "global " << irName << " : " << typeText << '\n';
        return;
    }

    const std::vector<std::uint8_t> bytes = ConstInitialBytes(
        node.Children.empty() ? nullptr : node.Children[0].get(),
        node.EvaluatedType
    );

    m_Context->ir << "global " << irName << " : " << typeText << " const";

    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        m_Context->ir << (i == 0 ? ' ' : ',') << static_cast<unsigned>(bytes[i]);
    }

    m_Context->ir << '\n';
}

// Um enum nao gera codigo. So os elementos de bloco b precisam de memoria, e
// por isso viram globais const em .rdata; os escalares existem apenas na tabela
// de constantes, para que as referencias dobrem para um literal.
void IRBuilder::BuildEnum(const EnumNode& node)
{
    for (const EnumElement& element : node.Elements)
    {
        if (element.Key.empty() || !element.EvaluatedType.IsValid())
        {
            continue;
        }

        const std::string irName = IRContext::ReplaceDoublePoints(element.Key);

        if (element.EvaluatedType.IsBuffer())
        {
            const std::vector<std::uint8_t> bytes = ConstInitialBytes(
                element.Value.get(),
                element.EvaluatedType,
                element.FoldedValue
            );

            m_Context->ir << "global " << irName << " : "
                << element.EvaluatedType.ToString() << " const";

            for (std::size_t i = 0; i < bytes.size(); ++i)
            {
                m_Context->ir << (i == 0 ? ' ' : ',') << static_cast<unsigned>(bytes[i]);
            }

            m_Context->ir << '\n';
            continue;
        }

        if (!element.FoldedValue.has_value())
        {
            continue;
        }

        if (element.FoldedValue->FitsInt64())
        {
            m_Context->constValues[irName] = element.FoldedValue->ToInt64();
        }
        else if (element.FoldedValue->FitsUInt64())
        {
            m_Context->constValues[irName] =
                static_cast<std::int64_t>(element.FoldedValue->ToUInt64());
        }
    }
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

        if (node.CompoundOp.has_value())
        {
            const std::string current = m_Context->NewTemp();
            m_Context->ir << current << " = load " << valueType.ToString()
                << ' ' << pointer.Text << '\n';

            value = ApplyCompound(
                IRExpressionBuilder::ExprResult{ current, valueType, false },
                value,
                *node.CompoundOp,
                valueType,
                *m_Context
            );
        }
        else
        {
            value = IRExpressionBuilder::Coerce(value, valueType, *m_Context);
        }

        m_Context->ir << "store " << valueType.ToString() << ' '
            << pointer.Text << ", " << value.Text << '\n';
        return;
    }

    if (node.IsIndexed)
    {
        if (!node.Target || node.Target->Children.size() < 2 ||
            !node.Target->Children[0] || !node.Target->Children[1])
        {
            Logger::Fatal("Indexed assignment without target");
            return;
        }

        IRExpressionBuilder::ExprResult value =
            IRExpressionBuilder::GetValue(valueNode, *m_Context);

        if (value.Ty.IsVoid())
        {
            return;
        }

        Type valueType = node.EvaluatedType.IsValid()
            ? node.EvaluatedType
            : Type::MakeUnsigned(8);

        if (!valueType.IsValid() || valueType.IsVoid())
        {
            valueType = Type::MakeUnsigned(8);
        }

        if (!node.CompoundOp.has_value())
        {
            value = IRExpressionBuilder::Coerce(value, valueType, *m_Context);
        }

        IRExpressionBuilder::ExprResult base =
            IRExpressionBuilder::GetValue(*node.Target->Children[0], *m_Context);
        IRExpressionBuilder::ExprResult index =
            IRExpressionBuilder::GetValue(*node.Target->Children[1], *m_Context);

        const Type addrType = Type::MakeUnsigned(m_Context->pointerBits);
        index = IRExpressionBuilder::Coerce(index, addrType, *m_Context);

        std::uint32_t elementSize = valueType.GetByteSize();
        if (elementSize == 0)
        {
            elementSize = 1;
        }

        std::string offset = index.Text;

        if (elementSize > 1)
        {
            offset = m_Context->NewTemp();
            m_Context->ir << offset << " = mul u" << m_Context->pointerBits << ' '
                << index.Text << ", " << elementSize << '\n';
        }

        const std::string address = m_Context->NewTemp();
        m_Context->ir << address << " = add u" << m_Context->pointerBits << ' '
            << base.Text << ", " << offset << '\n';

        if (node.CompoundOp.has_value())
        {
            const std::string current = m_Context->NewTemp();
            m_Context->ir << current << " = load " << valueType.ToString()
                << ' ' << address << '\n';

            value = ApplyCompound(
                IRExpressionBuilder::ExprResult{ current, valueType, false },
                value,
                *node.CompoundOp,
                valueType,
                *m_Context
            );
        }

        m_Context->ir << "store " << valueType.ToString() << ' '
            << address << ", " << value.Text << '\n';
        return;
    }

    const std::string irName = IRContext::ReplaceDoublePoints(node.Name.Text);
    std::string varName = irName;
    if (auto alias = m_Context->localAliases.find(node.Name.Text);
        alias != m_Context->localAliases.end())
    {
        varName = alias->second;
    }

    // Blocos b sao regioes estaticas: a atribuicao copia bytes em vez de
    // armazenar um valor. O tamanho vem do proprio tipo de destino.
    if (node.EvaluatedType.IsValid() && node.EvaluatedType.IsBuffer())
    {
        const std::uint32_t capacity = node.EvaluatedType.GetByteSize();
        std::string source;
        std::size_t bytes = capacity;

        if (valueNode.GetType() == NodeType::Literal)
        {
            const auto& literal = static_cast<const LiteralNode&>(valueNode);
            if (literal.Value.Type != TokenType::String)
            {
                // Diagnostico ja emitido na analise semantica.
                return;
            }

            auto label = m_Context->stringLabels.find(literal.Value.Text);
            if (label == m_Context->stringLabels.end())
            {
                throw std::runtime_error("String literal sem bloco de dados associado");
            }

            source = label->second;
            bytes = literal.Value.Text.size() + 1;
        }
        else if (valueNode.GetType() == NodeType::VariableCall)
        {
            const auto& variable = static_cast<const VariableCallNode&>(valueNode);
            source = IRContext::ReplaceDoublePoints(variable.Name.Text);

            if (auto type = m_Context->LookupType(variable.Name.Text);
                type.has_value() && type->IsBuffer())
            {
                bytes = type->GetByteSize();
            }
        }
        else
        {
            // Diagnostico ja emitido na analise semantica.
            return;
        }

        m_Context->ir << "copy " << varName << ", " << source << ", "
            << bytes << '\n';
        return;
    }

    IRExpressionBuilder::ExprResult value = IRExpressionBuilder::GetValue(valueNode, *m_Context);

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

    if (node.CompoundOp.has_value())
    {
        const std::string current = m_Context->NewTemp();
        m_Context->ir << current << " = load " << targetType.ToString()
            << ' ' << varName << '\n';

        value = ApplyCompound(
            IRExpressionBuilder::ExprResult{ current, targetType, false },
            value,
            *node.CompoundOp,
            targetType,
            *m_Context
        );
    }
    else
    {
        value = IRExpressionBuilder::Coerce(value, targetType, *m_Context);
    }

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
            // Uma chamada como instrução tem sempre efeitos colaterais: emite-se.
            (void)IRExpressionBuilder::GetValue(node, *m_Context);
            break;
        case NodeType::Cast:
            // Um cast descartado e livre de efeitos (ex: static_cast<i32>(g))
            // só produziria um load/cast morto. Em -O1+ nem é emitido; se o
            // cast envolver uma chamada, IsSideEffectFree devolve falso e o
            // valor continua a ser construído (a chamada tem de acontecer).
            if (m_Context->optimize && IsSideEffectFree(node))
            {
                break;
            }
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
        case NodeType::If:
            BuildIf(static_cast<IfNode&>(node));
            break;
        case NodeType::While:
            BuildWhile(static_cast<WhileNode&>(node));
            break;
        case NodeType::For:
            BuildFor(static_cast<ForNode&>(node));
            break;
        case NodeType::Using:
            // using so existe na analise semantica.
            break;
        case NodeType::Enum:
            // Os elementos ja foram emitidos como globais const.
            break;
        default:
            Logger::Fatal("Node not solved");
            std::exit(-1);
            break;
    }
}

void IRBuilder::BuildStatements(const std::vector<NodePtr>& statements)
{
    for (const auto& ptr : statements)
    {
        if (!ptr)
        {
            continue;
        }

        if (ptr->GetType() == NodeType::Return)
        {
            BuildReturn(static_cast<ReturnNode&>(*ptr));
        }
        else
        {
            BuildExpression(*ptr);
        }
    }
}

// 'if' sem 'else': branchnot salta o bloco quando a condicao e zero.
// Com 'else': branchnot para o ramo falso, jump ao fim apos o ramo verdadeiro.
// O tipo da condicao (u8 de comparacao ou inteiro) segue no 'branch' para o
// gerador dimensionar o test (rax vs eax).
void IRBuilder::BuildIf(const IfNode& node)
{
    const IRExpressionBuilder::ExprResult cond =
        IRExpressionBuilder::GetValue(*node.Condition, *m_Context);

    if (node.HasElse)
    {
        const std::string lElse = m_Context->NewLabel();
        const std::string lEnd = m_Context->NewLabel();

        m_Context->ir << "branchnot " << cond.Ty.ToString() << ' '
            << cond.Text << ", " << lElse << '\n';
        BuildStatements(node.ThenBody);
        m_Context->ir << "jump " << lEnd << '\n';
        m_Context->ir << "label " << lElse << '\n';
        BuildStatements(node.ElseBody);
        m_Context->ir << "label " << lEnd << '\n';
        return;
    }

    const std::string lEnd = m_Context->NewLabel();
    m_Context->ir << "branchnot " << cond.Ty.ToString() << ' '
        << cond.Text << ", " << lEnd << '\n';
    BuildStatements(node.ThenBody);
    m_Context->ir << "label " << lEnd << '\n';
}

void IRBuilder::BuildWhile(const WhileNode& node)
{
    const std::string lTop = m_Context->NewLabel();
    const std::string lEnd = m_Context->NewLabel();

    m_Context->ir << "label " << lTop << '\n';
    const IRExpressionBuilder::ExprResult cond =
        IRExpressionBuilder::GetValue(*node.Condition, *m_Context);
    m_Context->ir << "branchnot " << cond.Ty.ToString() << ' '
        << cond.Text << ", " << lEnd << '\n';
    BuildStatements(node.Body);
    m_Context->ir << "jump " << lTop << '\n';
    m_Context->ir << "label " << lEnd << '\n';
}

// 'for (init; cond; step) body'. Secoes vazias sao omitidas; sem 'cond' o
// ciclo e infinito (so sai por 'return'). O 'step' corre antes do jump de
// regresso ao topo, apos o corpo.
void IRBuilder::BuildFor(const ForNode& node)
{
    if (node.Init)
    {
        BuildExpression(*node.Init);
    }

    const std::string lTop = m_Context->NewLabel();
    const std::string lEnd = m_Context->NewLabel();

    m_Context->ir << "label " << lTop << '\n';
    if (node.Condition)
    {
        const IRExpressionBuilder::ExprResult cond =
            IRExpressionBuilder::GetValue(*node.Condition, *m_Context);
        m_Context->ir << "branchnot " << cond.Ty.ToString() << ' '
            << cond.Text << ", " << lEnd << '\n';
    }

    BuildStatements(node.Body);

    if (node.Step)
    {
        BuildExpression(*node.Step);
    }

    m_Context->ir << "jump " << lTop << '\n';
    m_Context->ir << "label " << lEnd << '\n';
}

} // namespace Emux
