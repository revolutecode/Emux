#include "Emux/Builder/IRExpressionBuilder.hpp"
#include "Emux/Builder/IROptimizer.hpp"
#include "Emux/Compiler/AST/BinaryNode.hpp"
#include "Emux/Compiler/AST/FunctionCallNode.hpp"
#include "Emux/Compiler/AST/LiteralNode.hpp"
#include "Emux/Compiler/AST/NodeType.hpp"
#include "Emux/Compiler/AST/UnaryNode.hpp"
#include "Emux/Compiler/AST/VariableCallNode.hpp"
#include "Emux/Core/Logger.hpp"
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace Emux
{

namespace IRExpressionBuilder
{

namespace
{

bool IsImmediateText(const std::string& text)
{
    return !text.empty() &&
        (std::isdigit(static_cast<unsigned char>(text[0])) ||
         text[0] == '-' || text[0] == '+');
}

ExprResult MakeLiteral(const std::string& text, const Type& type)
{
    return ExprResult{ text, type, true };
}

// Traduz um nome escopado ("main::a") para o nome usado na Exir: alias de
// parâmetro ("a") ou símbolo mangled ("main_a").
std::string ResolveName(const IRContext& ctx, const std::string& scoped)
{
    if (auto it = ctx.localAliases.find(scoped); it != ctx.localAliases.end())
    {
        return it->second;
    }

    return IRContext::ReplaceDoublePoints(scoped);
}

bool IsIntegerLike(const Type& type)
{
    return type.IsInteger() || type.IsPointer();
}

std::string BinaryOperationName(BinaryOperation operation, const Type& type)
{
    switch (operation)
    {
        case BinaryOperation::Add:    return "add";
        case BinaryOperation::Sub:    return "sub";
        case BinaryOperation::Mul:    return "mul";
        case BinaryOperation::Div:    return "div";
        case BinaryOperation::Xor:    return "xor";
        case BinaryOperation::And:    return "and";
        case BinaryOperation::Or:     return "or";
        case BinaryOperation::Lshift: return "shl";
        case BinaryOperation::Rshift: return type.IsSigned() ? "sar" : "shr";
        default:
            throw std::runtime_error("Operacao binaria nao suportada pela Exir");
    }
}

std::string UnaryOperationName(UnaryOperation operation)
{
    switch (operation)
    {
        case UnaryOperation::Neg:        return "neg";
        case UnaryOperation::Not:        return "not";
        case UnaryOperation::LogicalNot: return "lnot";
        default:
            throw std::runtime_error("Operacao unaria nao suportada pela Exir");
    }
}

ExprResult BuildBinary(BinaryNode& node, IRContext& ctx)
{
    const Type resultType = node.EvaluatedType.IsValid()
        ? node.EvaluatedType
        : Type::MakeUnsigned(32);

    if (auto folded = IROptimizer::TryFoldBinary(node, ctx))
    {
        return MakeLiteral(std::to_string(*folded), resultType);
    }

    if (node.Children.size() < 2 || !node.Children[0] || !node.Children[1])
    {
        Logger::Fatal("Invalid Binary node");
        return MakeLiteral("0", resultType);
    }

    ExprResult left = GetValue(*node.Children[0], ctx);
    ExprResult right = GetValue(*node.Children[1], ctx);

    left = Coerce(left, resultType, ctx);
    right = Coerce(right, resultType, ctx);

    const std::string dst = ctx.NewTemp();
    ctx.ir << dst << " = " << BinaryOperationName(node.Operation, resultType)
        << ' ' << resultType.ToString() << ' ' << left.Text << ", " << right.Text << '\n';

    return ExprResult{ dst, resultType, false };
}

ExprResult BuildUnary(UnaryNode& node, IRContext& ctx)
{
    const Type resultType = node.EvaluatedType.IsValid()
        ? node.EvaluatedType
        : Type::MakeUnsigned(32);

    if (node.Operation != UnaryOperation::AddressOf &&
        node.Operation != UnaryOperation::Deref)
    {
        if (auto folded = IROptimizer::TryFoldUnary(node, ctx))
        {
            return MakeLiteral(std::to_string(*folded), resultType);
        }
    }

    if (node.Children.empty() || !node.Children[0])
    {
        Logger::Fatal("Invalid Unary node");
        return MakeLiteral("0", resultType);
    }

    Node& operandNode = *node.Children[0];

    if (node.Operation == UnaryOperation::AddressOf)
    {
        const std::string symbol = ResolveName(ctx, operandNode.Name.Text);
        const std::string dst = ctx.NewTemp();
        ctx.ir << dst << " = addr " << symbol << '\n';

        const Type pointerType = resultType.IsPointer()
            ? resultType
            : Type::MakePointer(operandNode.EvaluatedType, ctx.pointerBits);
        return ExprResult{ dst, pointerType, false };
    }

    if (node.Operation == UnaryOperation::Deref)
    {
        ExprResult pointer = GetValue(operandNode, ctx);

        Type valueType = resultType;
        if (!valueType.IsValid() || valueType.IsVoid())
        {
            valueType = Type::MakeUnsigned(8);
        }

        const std::string dst = ctx.NewTemp();
        ctx.ir << dst << " = load " << valueType.ToString() << ' ' << pointer.Text << '\n';
        return ExprResult{ dst, valueType, false };
    }

    ExprResult operand = Coerce(GetValue(operandNode, ctx), resultType, ctx);

    const std::string dst = ctx.NewTemp();
    ctx.ir << dst << " = " << UnaryOperationName(node.Operation)
        << ' ' << resultType.ToString() << ' ' << operand.Text << '\n';

    return ExprResult{ dst, resultType, false };
}

ExprResult BuildFunctionCall(FunctionCallNode& functionCall, IRContext& ctx)
{
    const std::string target = IRContext::ReplaceDoublePoints(functionCall.GetName().Text);

    const FunctionSignature* signature = nullptr;
    if (auto it = ctx.functionSignatures.find(target); it != ctx.functionSignatures.end())
    {
        signature = it->second;
    }

    std::string arguments;
    std::size_t index = 0;

    for (auto& child : functionCall.Children)
    {
        ExprResult argument = GetValue(*child, ctx);

        if (signature && index < signature->ParameterTypes.size())
        {
            argument = Coerce(argument, signature->ParameterTypes[index], ctx);
        }

        if (!argument.Text.empty())
        {
            if (!arguments.empty())
            {
                arguments += ", ";
            }

            const Type argumentType = argument.Ty.IsValid()
                ? argument.Ty
                : Type::MakeUnsigned(32);
            arguments += argumentType.ToString();
            arguments += ' ';
            arguments += argument.Text;
        }

        ++index;
    }

    const Type returnType = functionCall.EvaluatedType.IsValid()
        ? functionCall.EvaluatedType
        : (signature ? signature->ReturnType : Type::MakeSigned(32));
    const Abi abi = signature ? signature->CallingConvention : Abi::Cdecl;

    const std::string callText = "call " + returnType.ToString() + ' ' + target + ' '
        + std::string(AbiName(abi)) + " (" + arguments + ")";

    if (returnType.IsVoid())
    {
        ctx.ir << callText << '\n';
        return ExprResult{ "", returnType, false };
    }

    const std::string dst = ctx.NewTemp();
    ctx.ir << dst << " = " << callText << '\n';
    return ExprResult{ dst, returnType, false };
}

} // namespace

ExprResult Coerce(const ExprResult& value, const Type& target, IRContext& ctx)
{
    if (!target.IsValid() || target.IsVoid())
    {
        return value;
    }

    if (value.Ty.IsValid() && value.Ty == target)
    {
        return value;
    }

    // Um literal não tem representação própria na Exir: o operando é
    // interpretado conforme o tipo da instrução que o consome.
    if (value.Literal || !value.Ty.IsValid())
    {
        return ExprResult{ value.Text, target, value.Literal };
    }

    if (value.Ty.Bits == target.Bits && IsIntegerLike(value.Ty) && IsIntegerLike(target))
    {
        return ExprResult{ value.Text, target, false };
    }

    const std::string dst = ctx.NewTemp();
    ctx.ir << dst << " = cast " << target.ToString() << ' '
        << value.Text << ", " << value.Ty.ToString() << '\n';

    return ExprResult{ dst, target, false };
}

ExprResult GetValue(Node& node, IRContext& ctx)
{
    const Type nodeType = node.EvaluatedType;

    if (auto constValue = IROptimizer::TryEvaluateConst(node, ctx))
    {
        return MakeLiteral(
            std::to_string(*constValue),
            nodeType.IsValid() ? nodeType : Type::MakeSigned(32)
        );
    }

    switch (node.GetType())
    {
        case NodeType::FunctionCall:
            return BuildFunctionCall(static_cast<FunctionCallNode&>(node), ctx);

        case NodeType::Binary:
            return BuildBinary(static_cast<BinaryNode&>(node), ctx);

        case NodeType::Unary:
            return BuildUnary(static_cast<UnaryNode&>(node), ctx);

        case NodeType::Literal:
        {
            LiteralNode& literal = static_cast<LiteralNode&>(node);

            if (nodeType.IsValid() && nodeType.Bits > 64)
            {
                throw std::runtime_error(
                    "Literal '" + literal.Value.Text +
                    "' excede 64 bits; inteiros arbitrarios ainda nao sao suportados pelo backend");
            }

            if (literal.Value.Text.empty())
            {
                return MakeLiteral("0", nodeType);
            }

            return MakeLiteral(literal.Value.Text, nodeType);
        }

        case NodeType::VariableCall:
        {
            const std::string symbol = ResolveName(ctx, node.Name.Text);

            Type valueType = nodeType;
            if (!valueType.IsValid() || valueType.IsVoid())
            {
                valueType = ctx.LookupType(node.Name.Text).value_or(Type::MakeUnsigned(32));
            }

            const std::string dst = ctx.NewTemp();
            ctx.ir << dst << " = load " << valueType.ToString() << ' ' << symbol << '\n';
            return ExprResult{ dst, valueType, false };
        }

        default:
            Logger::Fatal("This node can't devolve value");
            std::exit(-1);
            return MakeLiteral("0", Type::MakeInvalid());
    }
}

} // namespace IRExpressionBuilder

} // namespace Emux
