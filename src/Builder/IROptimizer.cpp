#include "Emux/Builder/IROptimizer.hpp"
#include "Emux/Builder/IRContext.hpp"
#include "Emux/Compiler/AST/LiteralNode.hpp"
#include "Emux/Compiler/AST/VariableCallNode.hpp"
#include <string>

namespace Emux
{

namespace IROptimizer
{

bool IsConst(const Node& node, const IRContext& ctx)
{
    if (node.Attrs.Has(AttrId::Const)) return true;
    if (node.GetType() == NodeType::Literal) return true;

    if (node.GetType() == NodeType::VariableCall)
    {
        const auto& varCall = static_cast<const VariableCallNode&>(node);
        std::string name = IRContext::ReplaceDoublePoints(varCall.Name.Text);
        return ctx.constValues.contains(name);
    }

    return false;
}

bool IsVolatile(const Node& node)
{
    return node.Attrs.Has(AttrId::Volatile);
}

std::optional<int64_t> TryEvaluateConst(const Node& node, const IRContext& ctx)
{
    if (IsVolatile(node)) return std::nullopt;

    if (node.GetType() == NodeType::Literal)
    {
        const auto& lit = static_cast<const LiteralNode&>(node);
        try
        {
            if (lit.Value.Text.starts_with("0x") || lit.Value.Text.starts_with("0X"))
            {
                return std::stoll(lit.Value.Text, nullptr, 16);
            }
            return std::stoll(lit.Value.Text, nullptr, 10);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    if (node.GetType() == NodeType::VariableCall)
    {
        const auto& varCall = static_cast<const VariableCallNode&>(node);
        std::string name = IRContext::ReplaceDoublePoints(varCall.Name.Text);
        if (auto it = ctx.constValues.find(name); it != ctx.constValues.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    if (node.GetType() == NodeType::Binary)
    {
        return TryFoldBinary(static_cast<const BinaryNode&>(node), ctx);
    }

    if (node.GetType() == NodeType::Unary)
    {
        return TryFoldUnary(static_cast<const UnaryNode&>(node), ctx);
    }

    return std::nullopt;
}

std::optional<int64_t> TryFoldBinary(const BinaryNode& node, const IRContext& ctx)
{
    if (IsVolatile(node)) return std::nullopt;
    if (node.Children.size() < 2 || !node.Children[0] || !node.Children[1])
    {
        return std::nullopt;
    }

    auto leftVal = TryEvaluateConst(*node.Children[0], ctx);
    auto rightVal = TryEvaluateConst(*node.Children[1], ctx);

    if (!leftVal || !rightVal)
    {
        return std::nullopt;
    }

    int64_t a = *leftVal;
    int64_t b = *rightVal;

    switch (node.Operation)
    {
        case BinaryOperation::Add:    return a + b;
        case BinaryOperation::Sub:    return a - b;
        case BinaryOperation::Mul:    return a * b;
        case BinaryOperation::Div:    return (b != 0) ? std::optional<int64_t>(a / b) : std::nullopt;
        case BinaryOperation::Xor:    return a ^ b;
        case BinaryOperation::And:    return a & b;
        case BinaryOperation::Or:     return a | b;
        case BinaryOperation::Lshift: return a << b;
        case BinaryOperation::Rshift: return a >> b;
        default:                      return std::nullopt;
    }
}

std::optional<int64_t> TryFoldUnary(const UnaryNode& node, const IRContext& ctx)
{
    if (IsVolatile(node)) return std::nullopt;
    if (node.Children.empty() || !node.Children[0])
    {
        return std::nullopt;
    }

    auto val = TryEvaluateConst(*node.Children[0], ctx);
    if (!val) return std::nullopt;

    switch (node.Operation)
    {
        case UnaryOperation::Neg: return -(*val);
        case UnaryOperation::Not: return ~(*val);
        default:                  return std::nullopt;
    }
}

} // namespace IROptimizer

} // namespace Emux
