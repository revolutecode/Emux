#include "Emux/Builder/IRExpressionBuilder.hpp"
#include "Emux/Builder/IROptimizer.hpp"
#include "Emux/Compiler/AST/LiteralNode.hpp"
#include "Emux/Compiler/AST/VariableCallNode.hpp"
#include "Emux/Compiler/AST/NodeType.hpp"
#include "Emux/Core/Logger.hpp"
#include <cstdlib>

namespace Emux
{

namespace IRExpressionBuilder
{

std::string HelperBinaryOperand(const std::string& str, IRContext& ctx)
{
    std::string left(str);
    if (!IRContext::LooksLikeReg(str))
    {
        left = ctx.GetFreeReg().value_or(str);
        if (left != str)
        {
            ctx.ir << "mov " << left << ',' << str << '\n';
        }
    }
    return left;
}

void BuildBinary(const BinaryNode& node, IRContext& ctx)
{
    // Otimização: Constant Folding com base no AttrSet ou operandos literais
    if (auto folded = IROptimizer::TryFoldBinary(node, ctx))
    {
        ctx.lastBinary = std::to_string(*folded);
        return;
    }

    if (node.Children.size() < 2 || !node.Children[0] || !node.Children[1])
    {
        Logger::Fatal("Invalid Binary node");
        return;
    }

    Node& a = *node.Children[0];
    Node& b = *node.Children[1];

    std::string textA = GetValue(a, ctx);
    std::string textB = GetValue(b, ctx);

    // Otimização algébrica: x + 0 = x, x - 0 = x, x * 1 = x
    if (textB == "0")
    {
        if (node.Operation == BinaryOperation::Add || node.Operation == BinaryOperation::Sub || node.Operation == BinaryOperation::Or || node.Operation == BinaryOperation::Xor)
        {
            ctx.lastBinary = textA;
            return;
        }
        if (node.Operation == BinaryOperation::Mul || node.Operation == BinaryOperation::And)
        {
            ctx.lastBinary = "0";
            return;
        }
    }
    if (textB == "1" && (node.Operation == BinaryOperation::Mul || node.Operation == BinaryOperation::Div))
    {
        ctx.lastBinary = textA;
        return;
    }

    std::string left = HelperBinaryOperand(textA, ctx);
    std::string right = (!textB.empty() && (std::isdigit(static_cast<unsigned char>(textB[0])) || textB[0] == '-'))
        ? textB
        : HelperBinaryOperand(textB, ctx);

    switch (node.Operation)
    {
        case BinaryOperation::Add:    ctx.ir << "add " << left << ',' << right << '\n'; break;
        case BinaryOperation::Sub:    ctx.ir << "sub " << left << ',' << right << '\n'; break;
        case BinaryOperation::Mul:    ctx.ir << "mul " << left << ',' << right << '\n'; break;
        case BinaryOperation::Div:    ctx.ir << "div " << left << ',' << right << '\n'; break;
        case BinaryOperation::Xor:    ctx.ir << "xor " << left << ',' << right << '\n'; break;
        case BinaryOperation::And:    ctx.ir << "and " << left << ',' << right << '\n'; break;
        case BinaryOperation::Or:     ctx.ir << "or "  << left << ',' << right << '\n'; break;
        case BinaryOperation::Lshift: ctx.ir << "shl " << left << ',' << right << '\n'; break;
        case BinaryOperation::Rshift: ctx.ir << "shr " << left << ',' << right << '\n'; break;
        default:
            Logger::Fatal("This operation not supported");
            break;
    }

    ctx.lastBinary = left;
    if (IRContext::LooksLikeReg(right)) ctx.ReleaseReg(right);
}

void BuildUnary(const UnaryNode& node, IRContext& ctx)
{
    // Otimização: Constant Folding com base no AttrSet ou operandos literais
    if (auto folded = IROptimizer::TryFoldUnary(node, ctx))
    {
        ctx.lastBinary = std::to_string(*folded);
        return;
    }

    if (node.Children.empty() || !node.Children[0])
    {
        Logger::Fatal("Invalid Unary node");
        return;
    }

    Node& operand = *node.Children[0];
    std::string text = GetValue(operand, ctx);
    std::string target = HelperBinaryOperand(text, ctx);

    switch (node.Operation)
    {
        case UnaryOperation::Neg: ctx.ir << "neg " << target << '\n'; break;
        case UnaryOperation::Not: ctx.ir << "not " << target << '\n'; break;
    }

    ctx.lastBinary = target;
}

void BuildFunctionCall(const FunctionCallNode& functionCall, IRContext& ctx)
{
    std::ostringstream sstr;
    sstr << "call " << IRContext::ReplaceDoublePoints(functionCall.GetName().Text);

    for (auto& child : functionCall.Children)
    {
        sstr << ',' << GetValue(*child, ctx);
    }
    sstr << '\n';

    ctx.ir << sstr.str();
}

std::string GetValue(Node& node, IRContext& ctx)
{
    // Otimização: se o nó é constante conhecido, devolve direto o valor
    if (auto constVal = IROptimizer::TryEvaluateConst(node, ctx))
    {
        return std::to_string(*constVal);
    }

    switch (node.GetType())
    {
        case NodeType::FunctionCall:
            BuildFunctionCall(static_cast<FunctionCallNode&>(node), ctx);
            return "rr0";
        case NodeType::Binary:
            BuildBinary(static_cast<BinaryNode&>(node), ctx);
            return ctx.lastBinary;
        case NodeType::Unary:
            BuildUnary(static_cast<UnaryNode&>(node), ctx);
            return ctx.lastBinary;
        case NodeType::Literal:
            return static_cast<LiteralNode&>(node).Value.Text;
        case NodeType::VariableCall:
            return IRContext::ReplaceDoublePoints(node.Name.Text);
        default:
            Logger::Fatal("This node can't devolve value");
            std::exit(-1);
            return {};
    }
}

} // namespace IRExpressionBuilder

} // namespace Emux
