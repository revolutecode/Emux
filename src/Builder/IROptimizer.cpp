#include "Emux/Builder/IROptimizer.hpp"
#include "Emux/Builder/IRContext.hpp"
#include "Emux/Compiler/AST/LiteralNode.hpp"
#include "Emux/Compiler/AST/VariableCallNode.hpp"
#include "Emux/Compiler/TokenType.hpp"
#include <cstdint>
#include <limits>
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

namespace
{

bool IsFoldableType(const Type& type)
{
    return !type.IsValid() || type.IsInteger();
}

int64_t WrapToType(int64_t value, const Type& type)
{
    if (!type.IsValid() || type.Bits == 0 || type.Bits >= 64)
    {
        return value;
    }

    const std::uint64_t mask = (1ull << type.Bits) - 1;
    std::uint64_t raw = static_cast<std::uint64_t>(value) & mask;

    if (type.IsSigned() && ((raw >> (type.Bits - 1)) & 1ull) != 0)
    {
        raw |= ~mask;
    }

    return static_cast<int64_t>(raw);
}

std::optional<int64_t> FoldDivision(int64_t a, int64_t b, const Type& type)
{
    if (b == 0)
    {
        return std::nullopt;
    }

    if (type.IsUnsigned())
    {
        std::uint64_t ua = static_cast<std::uint64_t>(a);
        std::uint64_t ub = static_cast<std::uint64_t>(b);

        if (type.Bits > 0 && type.Bits < 64)
        {
            const std::uint64_t mask = (1ull << type.Bits) - 1;
            ua &= mask;
            ub &= mask;
        }

        return WrapToType(static_cast<int64_t>(ua / ub), type);
    }

    if (a == std::numeric_limits<int64_t>::min() && b == -1)
    {
        return std::nullopt;
    }

    return WrapToType(a / b, type);
}

std::optional<int64_t> FoldShift(int64_t value, int64_t count, bool left, const Type& type)
{
    if (count < 0 || count >= 64)
    {
        return std::nullopt;
    }

    if (left)
    {
        return WrapToType(
            static_cast<int64_t>(static_cast<std::uint64_t>(value) << count),
            type
        );
    }

    if (type.IsUnsigned())
    {
        std::uint64_t raw = static_cast<std::uint64_t>(value);

        if (type.Bits > 0 && type.Bits < 64)
        {
            raw &= (1ull << type.Bits) - 1;
        }

        return WrapToType(static_cast<int64_t>(raw >> count), type);
    }

    // Shift aritmético explícito (o comportamento de >> em negativos é
    // definido pela implementação em C++).
    const std::uint64_t raw = static_cast<std::uint64_t>(value);
    const std::int64_t shifted = value >= 0
        ? static_cast<std::int64_t>(raw >> count)
        : static_cast<std::int64_t>(~(~raw >> count));

    return WrapToType(shifted, type);
}

}

std::optional<int64_t> TryFoldBinary(const BinaryNode& node, const IRContext& ctx)
{
    if (IsVolatile(node)) return std::nullopt;
    if (!IsFoldableType(node.EvaluatedType)) return std::nullopt;
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
    const Type& type = node.EvaluatedType;

    switch (node.Operation)
    {
        case BinaryOperation::Add:
            return WrapToType(
                static_cast<int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b)),
                type
            );
        case BinaryOperation::Sub:
            return WrapToType(
                static_cast<int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b)),
                type
            );
        case BinaryOperation::Mul:
            return WrapToType(
                static_cast<int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b)),
                type
            );
        case BinaryOperation::Div:    return FoldDivision(a, b, type);
        case BinaryOperation::Xor:    return WrapToType(a ^ b, type);
        case BinaryOperation::And:    return WrapToType(a & b, type);
        case BinaryOperation::Or:     return WrapToType(a | b, type);
        case BinaryOperation::Lshift: return FoldShift(a, b, true, type);
        case BinaryOperation::Rshift: return FoldShift(a, b, false, type);
        default:                      return std::nullopt;
    }
}

std::optional<int64_t> TryFoldUnary(const UnaryNode& node, const IRContext& ctx)
{
    if (IsVolatile(node)) return std::nullopt;
    if (!IsFoldableType(node.EvaluatedType)) return std::nullopt;
    if (node.Children.empty() || !node.Children[0])
    {
        return std::nullopt;
    }

    auto val = TryEvaluateConst(*node.Children[0], ctx);
    if (!val) return std::nullopt;

    switch (node.Operation)
    {
        case UnaryOperation::Neg:
            return WrapToType(
                static_cast<int64_t>(0ull - static_cast<std::uint64_t>(*val)),
                node.EvaluatedType
            );
        case UnaryOperation::Not:        return ~(*val);
        case UnaryOperation::LogicalNot: return (*val) == 0 ? 1 : 0;
        default:                         return std::nullopt;
    }
}

} // namespace IROptimizer

} // namespace Emux
