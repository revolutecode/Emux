#include "Emux/Builder/IRExpressionBuilder.hpp"
#include "Emux/Builder/IROptimizer.hpp"
#include "Emux/Compiler/AST/BinaryNode.hpp"
#include "Emux/Compiler/AST/CastNode.hpp"
#include "Emux/Compiler/AST/FunctionCallNode.hpp"
#include "Emux/Compiler/AST/IndexNode.hpp"
#include "Emux/Compiler/AST/LiteralNode.hpp"
#include "Emux/Compiler/AST/NodeType.hpp"
#include "Emux/Compiler/AST/UnaryNode.hpp"
#include "Emux/Compiler/AST/VariableCallNode.hpp"
#include "Emux/Core/Logger.hpp"
#include "Emux/Utilities/BigInt.hpp"
#include "Emux/Utilities/NumericLiteral.hpp"
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace Emux
{

namespace IRExpressionBuilder
{

namespace
{

ExprResult MakeLiteral(const std::string& text, const Type& type)
{
    return ExprResult{ text, type, true };
}

bool IsWideInteger(const Type& type)
{
    return type.IsInteger() && type.Bits > 64;
}

// O slot de um inteiro tem sempre uma largura de registo: 8, 16, 32 ou 64
// bits ate 64, e_bytes acima disso. Qualquer outra largura pode deixar lixo
// nos bits acima de Bits, por isso precisa de normalizacao explicita.
bool NeedsWidthMask(const Type& type)
{
    const Type repr = type.Representation();

    // O ponto fixo tem a mesma representacao de inteiro com sinal; os floats
    // tem sempre largura multipla de 8 e nao precisam de mascara.
    if (!(repr.IsInteger() || repr.IsFixed()) || repr.Bits == 0)
    {
        return false;
    }

    if (repr.Bits > 64)
    {
        return (repr.Bits % 8) != 0;
    }

    return repr.Bits != 8 && repr.Bits != 16 &&
        repr.Bits != 32 && repr.Bits != 64;
}

ExprResult Normalize(const ExprResult& value, const Type& type, IRContext& ctx)
{
    if (value.Literal || value.Text.empty() || !NeedsWidthMask(type))
    {
        return value;
    }

    const std::string dst = ctx.NewTemp();
    ctx.ir << dst << " = trunc " << type.Representation().ToString()
        << ' ' << value.Text << '\n';

    return ExprResult{ dst, type, false };
}

// Reduz o texto de um literal à representação canónica do tipo alvo. Assim
// os literal >64 bits chegam ao backend como decimal já truncado à largura,
// sem depender de stoll nem de imediatos de 64 bits.
std::string CanonicalLiteral(const std::string& text, const Type& type)
{
    const Type repr = type.Representation();

    if (!repr.IsInteger() && !repr.IsPointer() &&
        !repr.IsFixed() && !repr.IsFloat())
    {
        return text;
    }

    // Literais decimais ("1.5") sao convertidos a partir da fracao num/den.
    if (NumericLiteral::IsDecimalText(text))
    {
        auto decimal = NumericLiteral::ParseDecimal(text);
        if (!decimal.has_value() || repr.Bits == 0)
        {
            return text;
        }

        const auto& [numerator, denominator] = *decimal;

        if (repr.IsFixed())
        {
            return NumericLiteral::FixedStorageValue(
                numerator, denominator, repr
            ).ToString();
        }

        if (repr.IsFloat())
        {
            return NumericLiteral::FloatStorageBits(
                numerator, denominator, repr
            ).ToString();
        }

        // Decimal para inteiro/ponteiro nao e suportado.
        return text;
    }

    const bool isHex =
        text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X');

    auto value = BigInt::Parse(text, isHex ? 16 : 10);
    if (!value.has_value() || repr.Bits == 0)
    {
        return text;
    }

    // O literal da linguagem e o valor real; a instrucao transporta a
    // representacao de armazenamento do tipo alvo.
    if (repr.IsFixed())
    {
        return NumericLiteral::FixedStorageValue(*value, repr).ToString();
    }

    if (repr.IsFloat())
    {
        return NumericLiteral::FloatStorageBits(*value, repr).ToString();
    }

    return value->WrappedTo(repr.Bits, repr.IsSigned()).ToString();
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
        case BinaryOperation::Mod:    return "rem";
        case BinaryOperation::Xor:    return "xor";
        case BinaryOperation::And:    return "and";
        case BinaryOperation::Or:     return "or";
        case BinaryOperation::Lshift: return "shl";
        case BinaryOperation::Rshift: return type.IsSigned() ? "sar" : "shr";
        case BinaryOperation::Eq:     return "eq";
        case BinaryOperation::Ne:     return "ne";
        case BinaryOperation::Lt:     return "lt";
        case BinaryOperation::Gt:     return "gt";
        case BinaryOperation::Le:     return "le";
        case BinaryOperation::Ge:     return "ge";
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

    // '&&' e '||' nao sao instrucoes binarias: o lado direito só existe num
    // ramo, por isso o resultado nasce de saltos e não de um 'cmp'. Cada lado
    // é testado como um condicional (qualquer valor nao-zero e verdadeiro) e
    // escreve 0/1 no mesmo temporario. Os rotulos sao os mesmos dos 'if'/'while',
    // locais à função, e o jump final junta os dois ramos.
    if (IsShortCircuitOperation(node.Operation))
    {
        const std::string dst = ctx.NewTemp();
        const std::string lSkip = ctx.NewLabel();
        const std::string lEnd = ctx.NewLabel();

        const bool isAnd = node.Operation == BinaryOperation::LogicalAnd;

        const ExprResult left = GetValue(*node.Children[0], ctx);

        // Em '&&' um lado falso decide o 0; em '||' um lado verdadeiro decide
        // o 1. Em ambos os casos o salto vai para o ramo que escreve o valor
        // já conhecido e contorna a avaliação do lado direito.
        ctx.ir << (isAnd ? "branchnot " : "branch ") << left.Ty.ToString()
            << ' ' << left.Text << ", " << lSkip << '\n';

        const ExprResult right = GetValue(*node.Children[1], ctx);

        ctx.ir << (isAnd ? "branchnot " : "branch ") << right.Ty.ToString()
            << ' ' << right.Text << ", " << lSkip << '\n';

        ctx.ir << dst << " = const u8 " << (isAnd ? "1" : "0") << '\n';
        ctx.ir << "jump " << lEnd << '\n';
        ctx.ir << "label " << lSkip << '\n';
        ctx.ir << dst << " = const u8 " << (isAnd ? "0" : "1") << '\n';
        ctx.ir << "label " << lEnd << '\n';

        return ExprResult{ dst, Type::MakeUnsigned(8), false };
    }

    // Comparações: os operandos mantêm o seu tipo comum (ou o tipo ponteiro),
    // mas o resultado é 0/1 em u8. Por isso o texto da Exir carry o tipo dos
    // operandos e o gerador guarda o destino como u8.
    if (IsComparisonOperation(node.Operation))
    {
        Node& cmpLeft = *node.Children[0];
        Node& cmpRight = *node.Children[1];

        Type operandType;
        if (cmpLeft.EvaluatedType.IsPointer() && cmpRight.EvaluatedType.IsPointer())
        {
            operandType = cmpLeft.EvaluatedType;
        }
        else
        {
            operandType = cmpLeft.EvaluatedType.GetCommonType(cmpRight.EvaluatedType);
        }

        if (!operandType.IsValid())
        {
            operandType = Type::MakeUnsigned(32);
        }

        ExprResult cmpLhs = Coerce(GetValue(cmpLeft, ctx), operandType, ctx);
        ExprResult cmpRhs = Coerce(GetValue(cmpRight, ctx), operandType, ctx);

        const std::string cmpDst = ctx.NewTemp();
        ctx.ir << cmpDst << " = " << BinaryOperationName(node.Operation, operandType)
            << ' ' << operandType.ToString() << ' ' << cmpLhs.Text << ", " << cmpRhs.Text << '\n';

        return ExprResult{ cmpDst, resultType, false };
    }

    Node& leftNode = *node.Children[0];
    Node& rightNode = *node.Children[1];

    ExprResult left = GetValue(leftNode, ctx);
    ExprResult right = GetValue(rightNode, ctx);

    // Slice de buffer: `b + K` (K bytes, K constante). GetValue decai a base
    // para o endereco u8* (addr <simbolo>); somamos K bytes e devolvemos o
    // ponteiro deslocado. A categoria Buffer (com o tamanho restante) vive so
    // no analisador, para o bounds-checking; no IR o slice e sempre u8*.
    if (node.Operation == BinaryOperation::Add &&
        leftNode.EvaluatedType.IsBuffer() &&
        rightNode.EvaluatedType.IsInteger())
    {
        const Type addrType = Type::MakeUnsigned(ctx.pointerBits);
        ExprResult byteOffset = Coerce(right, addrType, ctx);

        const std::string dst = ctx.NewTemp();
        ctx.ir << dst << " = add " << addrType.ToString() << ' '
            << left.Text << ", " << byteOffset.Text << '\n';

        return ExprResult{
            dst,
            Type::MakePointer(Type::MakeUnsigned(8), ctx.pointerBits),
            false
        };
    }

    // Distancia entre ponteiros: a diferenca em bytes dividida por sizeof(T).
    if (node.Operation == BinaryOperation::Sub &&
        leftNode.EvaluatedType.IsPointer() &&
        rightNode.EvaluatedType.IsPointer())
    {
        left = Coerce(left, resultType, ctx);
        right = Coerce(right, resultType, ctx);

        std::string distance = ctx.NewTemp();
        ctx.ir << distance << " = " << BinaryInstructionName(node.Operation, resultType)
            << ' ' << resultType.ToString() << ' ' << left.Text << ", " << right.Text << '\n';

        const auto pointee = leftNode.EvaluatedType.Pointee();
        const std::uint32_t elementSize =
            (pointee && !pointee->IsVoid()) ? pointee->GetByteSize() : 1;

        if (elementSize > 1)
        {
            const std::string elements = ctx.NewTemp();
            ctx.ir << elements << " = "
                << BinaryInstructionName(BinaryOperation::Div, resultType)
                << ' ' << resultType.ToString() << ' ' << distance
                << ", " << elementSize << '\n';
            distance = elements;
        }

        return Normalize(ExprResult{ distance, resultType, false }, resultType, ctx);
    }

    // p + n e p - n avançam elementos, nao bytes.
    if ((node.Operation == BinaryOperation::Add ||
         node.Operation == BinaryOperation::Sub) &&
        resultType.IsPointer())
    {
        const bool leftIsPointer = leftNode.EvaluatedType.IsPointer();
        ExprResult& offset = leftIsPointer ? right : left;
        offset = ScalePointerOffset(offset, resultType, ctx);
    }

    left = Coerce(left, resultType, ctx);
    right = Coerce(right, resultType, ctx);

    const std::string dst = ctx.NewTemp();
    ctx.ir << dst << " = " << BinaryOperationName(node.Operation, resultType)
        << ' ' << resultType.ToString() << ' ' << left.Text << ", " << right.Text << '\n';

    return Normalize(ExprResult{ dst, resultType, false }, resultType, ctx);
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

        Type pointerType;
        if (resultType.IsPointer())
        {
            // `&b` de um bloco ja vem como u8* da analise semantica.
            pointerType = resultType;
        }
        else
        {
            pointerType = Type::MakePointer(operandNode.EvaluatedType, ctx.pointerBits);
        }
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
        return Normalize(ExprResult{ dst, valueType, false }, valueType, ctx);
    }

    // '!x' num inteiro largo testa todos os bytes; o resultado é 0/1 mas é
    // guardado no tipo largo para o backend poder percorrer o valor.
    if (node.Operation == UnaryOperation::LogicalNot &&
        IsWideInteger(operandNode.EvaluatedType))
    {
        const Type wideType = operandNode.EvaluatedType;
        ExprResult operand = Coerce(GetValue(operandNode, ctx), wideType, ctx);

        const std::string dst = ctx.NewTemp();
        ctx.ir << dst << " = lnot " << wideType.ToString() << ' ' << operand.Text << '\n';
        return Normalize(ExprResult{ dst, wideType, false }, wideType, ctx);
    }

    ExprResult operand = Coerce(GetValue(operandNode, ctx), resultType, ctx);

    const std::string dst = ctx.NewTemp();
    ctx.ir << dst << " = " << UnaryOperationName(node.Operation)
        << ' ' << resultType.ToString() << ' ' << operand.Text << '\n';

    return Normalize(ExprResult{ dst, resultType, false }, resultType, ctx);
}

// endereco = base + indice * sizeof(elemento); num bloco b o elemento tem
// 1 byte, num ponteiro T* escala por sizeof(T).
ExprResult BuildIndex(IndexNode& node, IRContext& ctx)
{
    if (node.Children.size() < 2 || !node.Children[0] || !node.Children[1])
    {
        Logger::Fatal("Invalid Index node");
        return MakeLiteral("0", Type::MakeInvalid());
    }

    ExprResult base = GetValue(*node.Children[0], ctx);
    ExprResult index = GetValue(*node.Children[1], ctx);

    Type elementType = Type::MakeUnsigned(8);
    std::uint32_t elementSize = 1;

    if (node.Children[0]->EvaluatedType.IsPointer())
    {
        const auto pointee = node.Children[0]->EvaluatedType.Pointee();
        if (pointee && !pointee->IsVoid())
        {
            elementType = *pointee;
            elementSize = elementType.GetByteSize();
            if (elementSize == 0)
            {
                elementSize = 1;
            }
        }
    }

    const Type addrType = Type::MakeUnsigned(ctx.pointerBits);
    index = Coerce(index, addrType, ctx);

    std::string offset = index.Text;

    if (elementSize > 1)
    {
        offset = ctx.NewTemp();
        ctx.ir << offset << " = mul u" << ctx.pointerBits << ' '
            << index.Text << ", " << elementSize << '\n';
    }

    const std::string address = ctx.NewTemp();
    ctx.ir << address << " = add u" << ctx.pointerBits << ' '
        << base.Text << ", " << offset << '\n';

    const std::string dst = ctx.NewTemp();
    ctx.ir << dst << " = load " << elementType.ToString() << ' '
        << address << '\n';

    return ExprResult{ dst, elementType, false };
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

std::string BinaryInstructionName(BinaryOperation operation, const Type& type)
{
    return BinaryOperationName(operation, type);
}

ExprResult Coerce(const ExprResult& value, const Type& target, IRContext& ctx)
{
    if (!target.IsValid() || target.IsVoid())
    {
        return value;
    }

    // Um literal não tem representação própria na Exir: o operando é
    // interpretado conforme o tipo da instrução que o consome. Trunca-se o
    // texto à largura do alvo e, se o alvo for largo, materializa-se num
    // temporário: operações multi-limb trabalham sobre memória.
    if (value.Literal || !value.Ty.IsValid())
    {
        const Type sourceRepr = value.Ty.IsValid()
            ? value.Ty.Representation()
            : Type::MakeInvalid();

        // Um literal de ponto fixo/float chega com o texto ja na representacao
        // de armazenamento (ver CanonicalLiteral): re-canonicaliza-lo o
        // interpretaria como valor e voltaria a aplicar escala/float.
        if (value.Literal && (sourceRepr.IsFixed() || sourceRepr.IsFloat()))
        {
            const bool isDecimal = NumericLiteral::IsDecimalText(value.Text);

            // Decimal para o MESMO tipo de origem: basta canonizar o texto
            // para a representacao de armazenamento e emi-lo como literal.
            if (isDecimal && sourceRepr == target.Representation())
            {
                const std::string canonical =
                    CanonicalLiteral(value.Text, sourceRepr);

                if (target.IsNumeric() && target.Bits > 64)
                {
                    const std::string dst = ctx.NewTemp();
                    ctx.ir << dst << " = const " << target.ToString()
                        << ' ' << canonical << '\n';
                    return ExprResult{ dst, target, false };
                }

                return ExprResult{ canonical, target, true };
            }

            if (!isDecimal && sourceRepr == target.Representation())
            {
                return ExprResult{ value.Text, target, true };
            }

            // Literais decimais sao canonizados no tipo de origem; o cast
            // seguinte trata a conversao para o tipo alvo.
            const std::string canonical = isDecimal
                ? CanonicalLiteral(value.Text, sourceRepr)
                : value.Text;

            const std::string src = ctx.NewTemp();
            ctx.ir << src << " = const " << sourceRepr.ToString()
                << ' ' << canonical << '\n';

            const std::string dst = ctx.NewTemp();
            ctx.ir << dst << " = cast " << target.ToString() << ' '
                << src << ", " << sourceRepr.ToString() << '\n';
            return Normalize(ExprResult{ dst, target, false }, target, ctx);
        }

        const std::string text = CanonicalLiteral(value.Text, target);

        // Operacoes multi-limb trabalham sobre memoria: qualquer valor com
        // mais de 64 bits (inteiro, ponto fixo ou float) e materializado.
        if (target.IsNumeric() && target.Bits > 64)
        {
            const std::string dst = ctx.NewTemp();
            ctx.ir << dst << " = const " << target.ToString() << ' ' << text << '\n';
            return ExprResult{ dst, target, false };
        }

        return ExprResult{ text, target, value.Literal };
    }

    if (value.Ty.Representation() == target.Representation())
    {
        return Normalize(value, target, ctx);
    }

    if (value.Ty.Bits == target.Bits && IsIntegerLike(value.Ty) && IsIntegerLike(target))
    {
        return Normalize(ExprResult{ value.Text, target, false }, target, ctx);
    }

    const std::string dst = ctx.NewTemp();
    ctx.ir << dst << " = cast " << target.ToString() << ' '
        << value.Text << ", " << value.Ty.ToString() << '\n';

    return Normalize(ExprResult{ dst, target, false }, target, ctx);
}

ExprResult ScalePointerOffset(
    const ExprResult& offset,
    const Type& pointerType,
    IRContext& ctx
)
{
    const auto pointee = pointerType.Pointee();
    const std::uint32_t elementSize =
        (pointee && !pointee->IsVoid()) ? pointee->GetByteSize() : 1;

    const Type addressType = Type::MakeUnsigned(ctx.pointerBits);
    ExprResult scaled = Coerce(offset, addressType, ctx);

    if (elementSize > 1)
    {
        const std::string dst = ctx.NewTemp();
        ctx.ir << dst << " = mul " << addressType.ToString() << ' '
            << scaled.Text << ", " << elementSize << '\n';
        scaled = ExprResult{ dst, addressType, false };
    }

    return scaled;
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

        case NodeType::Index:
            return BuildIndex(static_cast<IndexNode&>(node), ctx);

        case NodeType::Cast:
        {
            CastNode& cast = static_cast<CastNode&>(node);

            if (nodeType.IsVoid())
            {
                if (!cast.Children.empty() && cast.Children[0] &&
                    !cast.Children[0]->FoldedValue.has_value())
                {
                    (void)GetValue(*cast.Children[0], ctx);
                }
                return ExprResult{ "", Type::MakeVoid(), false };
            }

            if (cast.Children.empty() || !cast.Children[0])
            {
                throw std::runtime_error("Cast sem operando");
            }

            ExprResult source = GetValue(*cast.Children[0], ctx);

            // Leitura de bytes de um bloco/view: o operando ja decaiu para o
            // endereco (u8*), por isso o cast numerico vira um 'load' da
            // largura fisica do alvo. Os limites foram verificados na analise
            // semantica contra o tamanho restante da view.
            if (cast.Kind == CastKind::Static &&
                cast.Children[0]->EvaluatedType.IsBuffer() &&
                nodeType.IsNumeric())
            {
                const std::string loaded = ctx.NewTemp();
                ctx.ir << loaded << " = load " << nodeType.ToString() << ' '
                    << source.Text << '\n';
                return Normalize(
                    ExprResult{ loaded, nodeType, false }, nodeType, ctx);
            }

            if (cast.Kind == CastKind::Reinterpret &&
                nodeType.IsValid() && source.Ty.IsValid())
            {
                const Type sourceType = source.Ty.Representation();

                if (sourceType == nodeType.Representation())
                {
                    return Normalize(ExprResult{ source.Text, nodeType, false }, nodeType, ctx);
                }

                ExprResult operand = Coerce(source, sourceType, ctx);
                if (operand.Literal)
                {
                    const std::string materialized = ctx.NewTemp();
                    ctx.ir << materialized << " = const " << sourceType.ToString()
                        << ' ' << operand.Text << '\n';
                    operand = ExprResult{ materialized, sourceType, false };
                }

                const std::string dst = ctx.NewTemp();
                ctx.ir << dst << " = bitcast " << nodeType.ToString() << ' '
                    << operand.Text << ", " << sourceType.ToString() << '\n';
                return Normalize(ExprResult{ dst, nodeType, false }, nodeType, ctx);
            }

            return Coerce(source, nodeType, ctx);
        }

        case NodeType::Literal:
        {
            LiteralNode& literal = static_cast<LiteralNode&>(node);

            if (literal.Value.Type == TokenType::String)
            {
                const auto it = ctx.stringLabels.find(literal.Value.Text);
                if (it == ctx.stringLabels.end())
                {
                    throw std::runtime_error("String literal sem bloco de dados associado");
                }

                const std::string dst = ctx.NewTemp();
                ctx.ir << dst << " = addr " << it->second << '\n';
                return ExprResult{ dst, nodeType, false };
            }

            if (literal.Value.Text.empty())
            {
                return MakeLiteral("0", nodeType);
            }

            // Literais largos ficam como texto até alguém os coercer; Coerce
            // trunca/canonicaliza e materializa um 'const' multi-limb.
            return MakeLiteral(literal.Value.Text, nodeType);
        }

        case NodeType::VariableCall:
        {
            const std::string symbol = ResolveName(ctx, node.Name.Text);
            const auto declared = ctx.LookupType(node.Name.Text);

            // Um bloco b decai para o endereco do primeiro byte em contexto de
            // valor, tal como um array em C++.
            if (declared.has_value() && declared->IsBuffer())
            {
                const std::string address = ctx.NewTemp();
                ctx.ir << address << " = addr " << symbol << '\n';
                return ExprResult{
                    address,
                    Type::MakePointer(Type::MakeUnsigned(8), ctx.pointerBits),
                    false
                };
            }

            Type valueType = nodeType;
            if (!valueType.IsValid() || valueType.IsVoid())
            {
                valueType = declared.value_or(Type::MakeUnsigned(32));
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
