#include "Emux/Builder/IRExpressionBuilder.hpp"
#include "Emux/Builder/IROptimizer.hpp"
#include "Emux/Compiler/AST/LiteralNode.hpp"
#include "Emux/Compiler/AST/VariableCallNode.hpp"
#include "Emux/Compiler/AST/NodeType.hpp"
#include "Emux/Core/Logger.hpp"
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

ExprResult MakeLiteralResult(const std::string& text, const Type& type)
{
    return ExprResult{ text, type, IRContext::IsWide(type) };
}

bool ContainsCall(const Node& node)
{
    if (node.GetType() == NodeType::FunctionCall)
    {
        return true;
    }

    for (const auto& child : node.Children)
    {
        if (child && ContainsCall(*child))
        {
            return true;
        }
    }

    return false;
}

bool IsRegisterText(const std::string& text)
{
    return IRContext::LooksLikeReg(text) || text == "rr0" || text == "rr1";
}

// Materializa o operando esquerdo em memoria para sobreviver a chamadas
// avaliadas no lado direito.
ExprResult SpillAcrossCalls(const ExprResult& value, const Type& fallbackType, IRContext& ctx)
{
    if (value.Wide || value.Text.empty() || IsImmediateText(value.Text) || !IsRegisterText(value.Text))
    {
        return value;
    }

    const Type spillType = value.ValueType.IsValid() ? value.ValueType : fallbackType;
    const std::string temp = ctx.NewTemp(spillType);
    ctx.ir << "mov " << temp << ',' << value.Text << '\n';
    ctx.ReleaseReg(value.Text);
    return ExprResult{ temp, spillType, false };
}

// Promove um valor estreito para um temporário de 64 bits, preservando a
// extensão (sinal/zero) conforme o tipo de origem.
ExprResult CoerceWide(const ExprResult& value, const Type& targetType, IRContext& ctx)
{
    if (value.Wide || IsImmediateText(value.Text))
    {
        return value;
    }

    const Type sourceType = value.ValueType.IsValid() ? value.ValueType : targetType;
    const Type wideType = sourceType.IsSigned() ? Type::MakeSigned(64) : Type::MakeUnsigned(64);
    const std::string temp = ctx.NewTemp(wideType);
    ctx.ir << "mov64 " << temp << ',' << value.Text << ',' << sourceType.ToString() << '\n';
    return ExprResult{ temp, wideType, true };
}

std::string WideOperation(BinaryOperation operation, const Type& resultType)
{
    switch (operation)
    {
        case BinaryOperation::Add:    return "add64";
        case BinaryOperation::Sub:    return "sub64";
        case BinaryOperation::Mul:    return "mul64";
        case BinaryOperation::Div:    return resultType.IsSigned() ? "div64" : "divu64";
        case BinaryOperation::Xor:    return "xor64";
        case BinaryOperation::And:    return "and64";
        case BinaryOperation::Or:     return "or64";
        case BinaryOperation::Lshift: return "shl64";
        case BinaryOperation::Rshift: return resultType.IsSigned() ? "sar64" : "shr64";
        default:
            throw std::runtime_error("Operacao binaria de 64 bits nao suportada");
    }
}

}

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

ExprResult BuildBinary(const BinaryNode& node, IRContext& ctx)
{
    const Type resultType = node.EvaluatedType;

    // Otimização: Constant Folding com base no AttrSet ou operandos literais
    if (auto folded = IROptimizer::TryFoldBinary(node, ctx))
    {
        ctx.lastBinary = std::to_string(*folded);
        return MakeLiteralResult(ctx.lastBinary, resultType);
    }

    if (node.Children.size() < 2 || !node.Children[0] || !node.Children[1])
    {
        Logger::Fatal("Invalid Binary node");
        return ExprResult{ "0", resultType, false };
    }

    Node& a = *node.Children[0];
    Node& b = *node.Children[1];

    ExprResult leftVal = GetValue(a, ctx);

    if (ContainsCall(b))
    {
        leftVal = SpillAcrossCalls(leftVal, resultType, ctx);
    }

    ExprResult rightVal = GetValue(b, ctx);

    // Caminho de 64 bits: o resultado vive numa variável temporária oculta,
    // e o backend usa pares de 32 bits (x86) ou registradores nativos (x64).
    if (IRContext::IsWide(resultType) && (resultType.IsInteger() || resultType.IsPointer()))
    {
        const std::string operation = WideOperation(node.Operation, resultType);

        if ((node.Operation == BinaryOperation::Lshift ||
             node.Operation == BinaryOperation::Rshift) &&
            (rightVal.Text.empty() ||
             !(std::isdigit(static_cast<unsigned char>(rightVal.Text[0])) || rightVal.Text[0] == '-')))
        {
            throw std::runtime_error(
                "Deslocamento de 64 bits requer uma constante como contagem");
        }

        ExprResult wideLeft = CoerceWide(leftVal, resultType, ctx);
        ExprResult wideRight = (node.Operation == BinaryOperation::Lshift ||
                                node.Operation == BinaryOperation::Rshift)
            ? rightVal
            : CoerceWide(rightVal, resultType, ctx);

        std::string dst = ctx.NewTemp(resultType);
        ctx.ir << operation << ' ' << dst << ',' << wideLeft.Text << ',' << wideRight.Text << '\n';
        return ExprResult{ dst, resultType, true };
    }

    // Caminho estreito (ate 32 bits): registradores virtuais.
    std::string textA = leftVal.Text;
    std::string textB = rightVal.Text;

    // Otimização algébrica: x + 0 = x, x - 0 = x, x * 1 = x
    if (textB == "0")
    {
        if (node.Operation == BinaryOperation::Add || node.Operation == BinaryOperation::Sub || node.Operation == BinaryOperation::Or || node.Operation == BinaryOperation::Xor)
        {
            ctx.lastBinary = textA;
            return ExprResult{ textA, resultType, false };
        }
        if (node.Operation == BinaryOperation::Mul || node.Operation == BinaryOperation::And)
        {
            ctx.lastBinary = "0";
            return MakeLiteralResult("0", resultType);
        }
    }
    if (textB == "1" && (node.Operation == BinaryOperation::Mul || node.Operation == BinaryOperation::Div))
    {
        ctx.lastBinary = textA;
        return ExprResult{ textA, resultType, false };
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

    return ExprResult{ left, resultType, false };
}

ExprResult BuildUnary(const UnaryNode& node, IRContext& ctx)
{
    const Type resultType = node.EvaluatedType;

    // Otimização: Constant Folding com base no AttrSet ou operandos literais
    if (node.Operation != UnaryOperation::AddressOf && node.Operation != UnaryOperation::Deref)
    {
        if (auto folded = IROptimizer::TryFoldUnary(node, ctx))
        {
            ctx.lastBinary = std::to_string(*folded);
            return MakeLiteralResult(ctx.lastBinary, resultType);
        }
    }

    if (node.Children.empty() || !node.Children[0])
    {
        Logger::Fatal("Invalid Unary node");
        return ExprResult{ "0", resultType, false };
    }

    Node& operandNode = *node.Children[0];

    if (node.Operation == UnaryOperation::AddressOf)
    {
        const std::string scoped = operandNode.Name.Text;
        const std::string symbol = IRContext::ReplaceDoublePoints(scoped);

        if (IRContext::IsWide(resultType))
        {
            std::string dst = ctx.NewTemp(resultType);
            ctx.ir << "addr " << dst << ',' << symbol << '\n';
            return ExprResult{ dst, resultType, true };
        }

        auto reg = ctx.GetFreeReg();
        if (!reg.has_value())
        {
            throw std::runtime_error("Sem registrador virtual livre para '&'");
        }

        ctx.ir << "addr " << *reg << ',' << symbol << '\n';
        return ExprResult{ *reg, resultType, false };
    }

    if (node.Operation == UnaryOperation::Deref)
    {
        ExprResult pointer = GetValue(operandNode, ctx);
        const Type valueType = resultType.IsVoid() ? Type::MakeUnsigned(8) : resultType;

        if (IRContext::IsWide(valueType))
        {
            std::string dst = ctx.NewTemp(valueType);
            ctx.ir << "dload " << dst << ',' << pointer.Text << ',' << valueType.ToString() << '\n';
            return ExprResult{ dst, valueType, true };
        }

        auto reg = ctx.GetFreeReg();
        if (!reg.has_value())
        {
            throw std::runtime_error("Sem registrador virtual livre para '*'");
        }

        ctx.ir << "dload " << *reg << ',' << pointer.Text << ',' << valueType.ToString() << '\n';
        return ExprResult{ *reg, valueType, false };
    }

    ExprResult operand = GetValue(operandNode, ctx);

    if (node.Operation == UnaryOperation::LogicalNot && operand.Wide)
    {
        auto reg = ctx.GetFreeReg();
        if (!reg.has_value())
        {
            throw std::runtime_error("Sem registrador virtual livre para '!'");
        }

        ctx.ir << "lnot64 " << *reg << ',' << operand.Text << '\n';
        return ExprResult{ *reg, resultType, false };
    }

    if (IRContext::IsWide(resultType) && resultType.IsInteger())
    {
        ExprResult wideOperand = CoerceWide(operand, resultType, ctx);
        std::string dst = ctx.NewTemp(resultType);
        ctx.ir << (node.Operation == UnaryOperation::Neg ? "neg64 " : "not64 ")
            << dst << ',' << wideOperand.Text << '\n';
        return ExprResult{ dst, resultType, true };
    }

    std::string target = HelperBinaryOperand(operand.Text, ctx);

    switch (node.Operation)
    {
        case UnaryOperation::Neg: ctx.ir << "neg " << target << '\n'; break;
        case UnaryOperation::Not: ctx.ir << "not " << target << '\n'; break;
        case UnaryOperation::LogicalNot: ctx.ir << "lnot " << target << '\n'; break;
        default: break;
    }

    ctx.lastBinary = target;
    return ExprResult{ target, resultType, false };
}

ExprResult BuildFunctionCall(const FunctionCallNode& functionCall, IRContext& ctx)
{
    const std::string irName = IRContext::ReplaceDoublePoints(functionCall.GetName().Text);
    std::string target = irName;
    if (auto alias = ctx.importAliases.find(target); alias != ctx.importAliases.end())
    {
        target = alias->second;
    }

    const FunctionSignature* signature = nullptr;
    if (auto it = ctx.functionSignatures.find(irName); it != ctx.functionSignatures.end())
    {
        signature = it->second;
    }

    std::ostringstream sstr;
    sstr << "call " << target;

    std::size_t index = 0;
    for (auto& child : functionCall.Children)
    {
        ExprResult argument = GetValue(*child, ctx);

        if (signature && index < signature->ParameterTypes.size())
        {
            const Type& parameter = signature->ParameterTypes[index];

            if (IRContext::IsWide(parameter))
            {
                const bool immediate = IsImmediateText(argument.Text);

                // No x64 um imediato cabe direto no registrador de 64 bits.
                // No x86 (e para valores estreitos) precisa de temporario tipado
                // para o backend empilhar o par hi/lo correto.
                const bool needsTemp = ctx.pointerBits > 32
                    ? (!argument.Wide && !immediate)
                    : (!argument.Wide || immediate);

                if (needsTemp)
                {
                    const Type source = argument.ValueType.IsValid() ? argument.ValueType : parameter;
                    const std::string temp = ctx.NewTemp(parameter);
                    ctx.ir << "mov64 " << temp << ',' << argument.Text << ',' << source.ToString() << '\n';
                    argument = ExprResult{ temp, parameter, true };
                }
            }
        }

        sstr << ',' << argument.Text;
        ++index;
    }
    sstr << '\n';

    ctx.ir << sstr.str();

    const Type returnType = functionCall.EvaluatedType;

    if (returnType.IsVoid())
    {
        return ExprResult{ "", returnType, false };
    }

    if (IRContext::IsWide(returnType))
    {
        std::string dst = ctx.NewTemp(returnType);
        ctx.ir << "mov64 " << dst << ",rr0," << returnType.ToString() << '\n';
        return ExprResult{ dst, returnType, true };
    }

    // Chamadas seguintes reutilizam rr0/r0-r3; preserva o resultado.
    std::string dst = ctx.NewTemp(returnType);
    ctx.ir << "mov " << dst << ",rr0\n";
    return ExprResult{ dst, returnType, false };
}

ExprResult GetValue(Node& node, IRContext& ctx)
{
    const Type nodeType = node.EvaluatedType;

    // Otimização: se o nó é constante conhecido, devolve direto o valor
    if (auto constVal = IROptimizer::TryEvaluateConst(node, ctx))
    {
        return MakeLiteralResult(std::to_string(*constVal), nodeType);
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
                return ExprResult{ "0", nodeType, IRContext::IsWide(nodeType) };
            }

            return MakeLiteralResult(literal.Value.Text, nodeType);
        }

        case NodeType::VariableCall:
        {
            std::string text;

            if (auto it = ctx.localAliases.find(node.Name.Text); it != ctx.localAliases.end())
            {
                text = it->second;
            }
            else
            {
                text = IRContext::ReplaceDoublePoints(node.Name.Text);
            }

            return ExprResult{ text, nodeType, IRContext::IsWide(nodeType) };
        }

        default:
            Logger::Fatal("This node can't devolve value");
            std::exit(-1);
            return ExprResult{ "0", Type::MakeInvalid(), false };
    }
}

} // namespace IRExpressionBuilder

} // namespace Emux
