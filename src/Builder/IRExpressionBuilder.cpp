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
#include "Emux/Utilities/BigInt.hpp"
#include "Emux/Utilities/NumericLiteral.hpp"

#include <stdexcept>

namespace Emux
{

namespace IRExpressionBuilder
{

namespace
{

// Ponto fixo partilha o comportamento inteiro com sinal; float e um padrao de
// bits que nao se estende.
bool IsSignedLike(const Type& type)
{
    const Type repr = type.Representation();
    return repr.IsSigned() || repr.IsFixed();
}

// Os slots de temporarios tem sempre largura de registo. Um tipo cuja
// precisao nao e 8, 16, 32 ou 64 bits (u24, i5, q12) pode deixar lixo acima
// de Bits depois de uma conta, e as leituras seguintes assumiriam que nao.
// O 'trunc' explicito devolve o valor a representacao canonica do tipo.
bool NeedsWidthMask(const Type& type)
{    const Type repr = type.Representation();

    if (repr.IsFloat() || repr.IsBuffer() || !repr.IsNumeric())
    {
        return false;
    }

    // O booleano ja so tem 0 e 1; uma mascara seria ruido no IR.
    if (repr.Bits <= 1)
    {
        return false;
    }

    if (repr.Bits > 64)
    {
        return (repr.Bits % 8) != 0;
    }

    return repr.Bits != 8 && repr.Bits != 16 && repr.Bits != 32 && repr.Bits != 64;
}

ExprResult Normalize(const ExprResult& value, const Type& type, IRContext& ctx)
{
    if (value.Val.Kind == Exir::ValueKind::Constant || !NeedsWidthMask(type))
    {
        return value;
    }

    const std::uint32_t dstId = ctx.NewTempId();
    Exir::Instruction inst;
    inst.Op = Exir::OpCode::Trunc;
    inst.ResultTy = type;
    inst.DstId = dstId;
    inst.Operands[0] = value.Val;
    ctx.Emit(std::move(inst));

    return ExprResult{ Exir::Value::MakeTemp(dstId, type), false };
}

// O imediato de um Value tem 64 bits: um tipo cuja representacao ocupa mais
// que isso nao se pode escrever como imediato no proprio operando.
bool NeedsWideStorage(const Type& type)
{
    const Type repr = type.Representation();
    return repr.IsNumeric() && !repr.IsBuffer() && repr.GetByteSize() > 8;
}

// Classe numerica de um tipo, para decidir entre conversao e reinterpretacao.
enum class NumericClass
{
    Other,
    Integer,
    Float,
    Fixed
};

NumericClass GetNumericClass(const Type& type)
{
    const Type repr = type.Representation();

    if (repr.IsFloat()) return NumericClass::Float;
    if (repr.IsFixed()) return NumericClass::Fixed;
    if (repr.IsInteger()) return NumericClass::Integer;

    return NumericClass::Other;
}

// Leitura do padrao de bits de um imediato ja canonico no tipo de origem.
BigInt NumericValueOf(std::uint64_t imm, const Type& type)
{
    return IsSignedLike(type)
        ? BigInt(static_cast<std::int64_t>(imm))
        : BigInt::FromUInt64(imm);
}

} // namespace

// Valor constante canonico de um tipo numerico. Abaixo de 64 bits e um
// imediato; acima, a representacao em bytes vive na .rdata e chega ao codigo
// por addr+load, porque nenhum operando guarda 128 bits.
static ExprResult MaterializeConstant(const BigInt& stored, const Type& type, IRContext& ctx)
{
    const Type repr = type.Representation();
    const bool signedLike = IsSignedLike(repr);
    const std::uint32_t bits = repr.Bits ? repr.Bits : 64;
    const BigInt wrapped = stored.WrappedTo(bits, signedLike);

    if (!NeedsWideStorage(repr))
    {
        // O padrao de bits do slot e de 64 bits: um negativo fica em
        // complemento de dois, nao na magnitude.
        const std::vector<std::uint8_t> pattern = wrapped.ToLittleEndianBytes(8);

        std::uint64_t imm = 0;
        for (std::size_t i = 0; i < pattern.size(); ++i)
        {
            imm |= std::uint64_t(pattern[i]) << (8 * i);
        }

        return ExprResult{ Exir::Value::MakeConstant(imm, type), false };
    }

    const std::vector<std::uint8_t> bytes =
        wrapped.ToLittleEndianBytes(repr.GetByteSize());

    const std::uint32_t globalId = ctx.InternWideConstant(bytes, type);
    const std::uint32_t addrId = ctx.NewTempId();

    Exir::Instruction addr;
    addr.Op = Exir::OpCode::Addr;
    addr.ResultTy = Type::MakePointer(type, ctx.pointerBits);
    addr.DstId = addrId;
    addr.Operands[0] = Exir::Value::MakeGlobal(globalId, type);
    ctx.Emit(std::move(addr));

    const std::uint32_t valId = ctx.NewTempId();
    Exir::Instruction load;
    load.Op = Exir::OpCode::Load;
    load.ResultTy = type;
    load.DstId = valId;
    load.Operands[0] = Exir::Value::MakeTemp(addrId, addr.ResultTy);
    ctx.Emit(std::move(load));

    return ExprResult{ Exir::Value::MakeTemp(valId, type), false };
}

ExprResult MakeNumericConstant(const BigInt& value, const Type& type, IRContext& ctx)
{
    const Type repr = type.Representation();

    // Um inteiro ja e o seu padrao de bits; float e fixed vivem noutro
    // codigo e o slot tem de guardar os bits, nao o numero.
    if (repr.IsFloat())
    {
        return MaterializeConstant(NumericLiteral::FloatStorageBits(value, type), type, ctx);
    }

    if (repr.IsFixed())
    {
        return MaterializeConstant(NumericLiteral::FixedStorageValue(value, type), type, ctx);
    }

    return MaterializeConstant(value, type, ctx);
}

ExprResult MakeNumericConstant(
    const BigInt& numerator,
    const BigInt& denominator,
    const Type& type,
    IRContext& ctx
)
{
    const Type repr = type.Representation();

    if (repr.IsFloat())
    {
        return MaterializeConstant(
            NumericLiteral::FloatStorageBits(numerator, denominator, type), type, ctx);
    }

    if (repr.IsFixed())
    {
        return MaterializeConstant(
            NumericLiteral::FixedStorageValue(numerator, denominator, type), type, ctx);
    }

    const auto divided = numerator.DivMod(denominator);
    return MaterializeConstant(divided ? divided->first : BigInt(0), type, ctx);
}

ExprResult EmitBinaryInst(Exir::OpCode op, const ExprResult& left, const ExprResult& right, const Type& resultTy, IRContext& ctx)
{
    const std::uint32_t dstId = ctx.NewTempId();
    Exir::Instruction inst;
    inst.Op = op;
    inst.ResultTy = resultTy;
    inst.DstId = dstId;
    inst.Operands[0] = left.Val;
    inst.Operands[1] = right.Val;
    ctx.Emit(std::move(inst));

    return Normalize(
        ExprResult{ Exir::Value::MakeTemp(dstId, resultTy), false }, resultTy, ctx);
}

namespace
{

ExprResult EmitUnaryInst(Exir::OpCode op, const ExprResult& operand, const Type& resultTy, IRContext& ctx)
{
    const std::uint32_t dstId = ctx.NewTempId();
    Exir::Instruction inst;
    inst.Op = op;
    inst.ResultTy = resultTy;
    inst.DstId = dstId;
    inst.Operands[0] = operand.Val;
    ctx.Emit(std::move(inst));

    return Normalize(
        ExprResult{ Exir::Value::MakeTemp(dstId, resultTy), false }, resultTy, ctx);
}

// O decaimento de um bloco e um unico 'addr' tipado como u8*, quer a regiao
// viva num slot da frame quer numa seccao de dados estatica.
Exir::Value DecayBuffer(const VariableCallNode& varCall, IRContext& ctx)
{
    const Type ptrTy = Type::MakePointer(Type::MakeUnsigned(8), ctx.pointerBits);
    const std::string name = varCall.Name.Text;

    Exir::Value target;
    if (auto slotOpt = ctx.FindLocalSlot(name); slotOpt.has_value())
    {
        target = Exir::Value::MakeLocal(*slotOpt, varCall.EvaluatedType);
    }
    else
    {
        target = Exir::Value::MakeGlobal(
            ctx.FindOrImportGlobal(name).value_or(0), varCall.EvaluatedType);
    }

    const std::uint32_t addrId = ctx.NewTempId();
    Exir::Instruction inst;
    inst.Op = Exir::OpCode::Addr;
    inst.ResultTy = ptrTy;
    inst.DstId = addrId;
    inst.Operands[0] = target;
    ctx.Emit(std::move(inst));

    return Exir::Value::MakeTemp(addrId, ptrTy);
}

} // namespace

Exir::OpCode MapBinaryOp(BinaryOperation operation, const Type& type)
{
    // Ponto fixo partilha o comportamento inteiro com sinal; float nao tem
    // divisao com sinal separada, mas usa a mesma rota do lado assinado.
    const bool signedLike = IsSignedLike(type);

    switch (operation)
    {
        case BinaryOperation::Add:    return Exir::OpCode::Add;
        case BinaryOperation::Sub:    return Exir::OpCode::Sub;
        case BinaryOperation::Mul:    return Exir::OpCode::Mul;
        case BinaryOperation::Div:    return signedLike ? Exir::OpCode::SDiv : Exir::OpCode::UDiv;
        case BinaryOperation::Mod:    return signedLike ? Exir::OpCode::SRem : Exir::OpCode::URem;
        case BinaryOperation::Xor:        return Exir::OpCode::Xor;
        case BinaryOperation::And:
        case BinaryOperation::LogicalAnd: return Exir::OpCode::And;
        case BinaryOperation::Or:
        case BinaryOperation::LogicalOr:  return Exir::OpCode::Or;
        case BinaryOperation::Lshift:     return Exir::OpCode::Shl;
        case BinaryOperation::Rshift: return signedLike ? Exir::OpCode::AShr : Exir::OpCode::LShr;
        case BinaryOperation::Eq:     return Exir::OpCode::CmpEq;
        case BinaryOperation::Ne:     return Exir::OpCode::CmpNe;
        case BinaryOperation::Lt:     return signedLike ? Exir::OpCode::CmpSlt : Exir::OpCode::CmpUlt;
        case BinaryOperation::Gt:     return signedLike ? Exir::OpCode::CmpSgt : Exir::OpCode::CmpUgt;
        case BinaryOperation::Le:     return signedLike ? Exir::OpCode::CmpSle : Exir::OpCode::CmpUle;
        case BinaryOperation::Ge:     return signedLike ? Exir::OpCode::CmpSge : Exir::OpCode::CmpUge;
    }
    return Exir::OpCode::Add;
}

ExprResult Coerce(const ExprResult& value, const Type& target, IRContext& ctx)
{
    if (value.Val.Ty == target)
    {
        return value;
    }

    if (value.Val.Kind == Exir::ValueKind::Constant)
    {
        const Type sourceRepr = value.Val.Ty.Representation();
        const Type targetRepr = target.Representation();
        const NumericClass sourceClass = GetNumericClass(value.Val.Ty);
        const NumericClass targetClass = GetNumericClass(target);

        // Entre inteiros (e para tudo o que nao e numero) o padrao de bits
        // nao muda. Ja mudar de classe, ou de escala/largura dentro de um
        // fixed ou de um float, e uma conversao a serio.
        const bool changesMeaning =
            sourceClass != NumericClass::Other && targetClass != NumericClass::Other &&
            (sourceClass != targetClass ||
             ((sourceClass == NumericClass::Fixed || sourceClass == NumericClass::Float) &&
              (sourceRepr.Bits != targetRepr.Bits || sourceRepr.Scale != targetRepr.Scale)));

        if (!changesMeaning)
        {
            if (!NeedsWideStorage(targetRepr))
            {
                return ExprResult{ Exir::Value::MakeConstant(value.Val.Imm, target), false };
            }

            // Um immediato nao guarda mais de 64 bits; o valor reescreve-se no
            // tipo alvo a partir da leitura, com sinal, do imediato actual.
            return MakeNumericConstant(NumericValueOf(value.Val.Imm, sourceRepr), target, ctx);
        }

        // Inteiro -> float/fixo: o imediato E o numero, basta codifica-lo.
        if (sourceClass == NumericClass::Integer)
        {
            return MakeNumericConstant(
                NumericValueOf(value.Val.Imm, sourceRepr), target, ctx);
        }
    }

    const std::uint32_t dstId = ctx.NewTempId();
    Exir::Instruction inst;

    inst.DstId = dstId;
    inst.ResultTy = target;
    inst.Operands[0] = value.Val;

    if (target.IsPointer() && value.Val.Ty.IsInteger())
    {
        inst.Op = Exir::OpCode::IntToPtr;
    }
    else if (target.IsInteger() && value.Val.Ty.IsPointer())
    {
        inst.Op = Exir::OpCode::PtrToInt;
    }
    else if (target.IsPointer() || value.Val.Ty.IsPointer() ||
             target.IsBuffer() || value.Val.Ty.IsBuffer())
    {
        // Enderecos e blocos sao padroes de bits: nao ha conversao numerica.
        inst.Op = Exir::OpCode::BitCast;
    }
    else if (target.IsInteger() && value.Val.Ty.IsInteger())
    {
        // Entre inteiros so muda a largura: truncar, zero ou sinal.
        if (target.Bits < value.Val.Ty.Bits)
        {
            inst.Op = Exir::OpCode::Trunc;
        }
        else if (value.Val.Ty.IsSigned())
        {
            inst.Op = Exir::OpCode::SExt;
        }
        else
        {
            inst.Op = Exir::OpCode::ZExt;
        }
    }
    else if (target.IsValid() && value.Val.Ty.IsValid() &&
             GetNumericClass(target) == GetNumericClass(value.Val.Ty))
    {
        // Mesmo padrao de bits, classe numerica diferente so por nome.
        inst.Op = Exir::OpCode::BitCast;
    }
    else
    {
        // Float, ponto fixo e escalas: o valor reinterpreta-se, nao os bits.
        inst.Op = Exir::OpCode::Conv;
    }

    ctx.Emit(std::move(inst));
    return ExprResult{ Exir::Value::MakeTemp(dstId, target), false };
}

ExprResult ScalePointerOffset(const ExprResult& offset, const Type& pointerType, IRContext& ctx)
{
    const auto pointee = pointerType.Pointee();
    const std::uint32_t elementSize = pointee ? pointee->GetByteSize() : 1;

    if (elementSize <= 1)
    {
        return offset;
    }

    const Type addrType = Type::MakeUnsigned(ctx.pointerBits);
    const ExprResult scaledOffset = Coerce(offset, addrType, ctx);
    const ExprResult elemSizeVal = ExprResult{ Exir::Value::MakeConstant(elementSize, addrType), false };

    return EmitBinaryInst(Exir::OpCode::Mul, scaledOffset, elemSizeVal, addrType, ctx);
}

namespace
{

// O resultado literal de um desempate de capacidades, quando o conteudo nao e
// preciso perguntar ao runtime.
bool TieSatisfies(BinaryOperation operation, int tie)
{
    switch (operation)
    {
        case BinaryOperation::Eq: return tie == 0;
        case BinaryOperation::Ne: return tie != 0;
        case BinaryOperation::Lt: return tie < 0;
        case BinaryOperation::Gt: return tie > 0;
        case BinaryOperation::Le: return tie <= 0;
        case BinaryOperation::Ge: return tie >= 0;
        default: return false;
    }
}

// Comparacao entre dois blocos. A capacidade e um facto de compilacao, o
// conteudo nao: capacidades diferentes decidem '==' e '!=' sem runtime, e nos
// restantes operadores os bytes comuns mandam e so um empate faz valer o
// desempate estatico. Como a memcmp so conhece os bytes comuns, o desempate
// entra por arimetica: sign(2*cmp + tie) nao altera o sinal de cmp.
ExprResult BuildBufferComparison(BinaryNode& node, IRContext& ctx)
{
    Node& lhs = *node.Children[0];
    Node& rhs = *node.Children[1];

    const std::uint32_t sizeA = lhs.EvaluatedType.GetByteSize();
    const std::uint32_t sizeB = rhs.EvaluatedType.GetByteSize();
    const std::uint32_t common = sizeA < sizeB ? sizeA : sizeB;
    const int tie = sizeA == sizeB ? 0 : (sizeA > sizeB ? 1 : -1);

    // Avaluar os dois lados primeiro: um slice pode esconder uma chamada no
    // indice e os seus efeitos colaterais tem de acontecer mesmo quando o
    // resultado se dobra.
    const ExprResult left = GetValue(lhs, ctx);
    const ExprResult right = GetValue(rhs, ctx);

    const Type boolTy = Type::MakeUnsigned(8);
    if (common == 0 ||
        (tie != 0 && (node.Operation == BinaryOperation::Eq ||
                      node.Operation == BinaryOperation::Ne)))
    {
        return ExprResult{
            Exir::Value::MakeConstant(TieSatisfies(node.Operation, tie) ? 1 : 0, boolTy),
            false
        };
    }

    const Type cmpTy = Type::MakeSigned(32);
    const std::uint32_t cmpId = ctx.NewTempId();
    Exir::Instruction cmpInst;
    cmpInst.Op = Exir::OpCode::Memcmp;
    cmpInst.ResultTy = cmpTy;
    cmpInst.DstId = cmpId;
    cmpInst.Operands[0] = left.Val;
    cmpInst.Operands[1] = right.Val;
    cmpInst.Operands[2] = Exir::Value::MakeConstant(
        common, Type::MakeUnsigned(ctx.pointerBits));
    ctx.Emit(std::move(cmpInst));

    ExprResult outcome{ Exir::Value::MakeTemp(cmpId, cmpTy), false };
    if (tie != 0)
    {
        outcome = EmitBinaryInst(
            Exir::OpCode::Mul, outcome,
            ExprResult{ Exir::Value::MakeConstant(2, cmpTy), false }, cmpTy, ctx);
        outcome = EmitBinaryInst(
            Exir::OpCode::Add, outcome,
            ExprResult{ Exir::Value::MakeConstant(
                static_cast<std::uint64_t>(static_cast<std::int64_t>(tie)), cmpTy), false },
            cmpTy, ctx);
    }

    return EmitBinaryInst(
        MapBinaryOp(node.Operation, cmpTy), outcome,
        ExprResult{ Exir::Value::MakeConstant(0, cmpTy), false }, boolTy, ctx);
}

bool IsBlockComparison(BinaryNode& node)
{
    if (node.Children.size() < 2 || !node.Children[0] || !node.Children[1])
    {
        return false;
    }

    switch (node.Operation)
    {
        case BinaryOperation::Eq:
        case BinaryOperation::Ne:
        case BinaryOperation::Lt:
        case BinaryOperation::Gt:
        case BinaryOperation::Le:
        case BinaryOperation::Ge:
            return node.Children[0]->EvaluatedType.IsBuffer() &&
                   node.Children[1]->EvaluatedType.IsBuffer();
        default:
            return false;
    }
}

} // namespace

ExprResult BuildBinary(BinaryNode& node, IRContext& ctx)
{
    Node& leftNode = *node.Children[0];
    Node& rightNode = *node.Children[1];

    // Curto-circuito para LogicalAnd (&&)
    if (node.Operation == BinaryOperation::LogicalAnd)
    {
        const Type resTy = node.EvaluatedType.IsValid() ? node.EvaluatedType : Type::MakeUnsigned(1);
        const std::uint32_t resSlot = ctx.localCounter++;
        Exir::LocalSlot slot;
        slot.Id = resSlot;
        slot.Name = "__sc_" + std::to_string(resSlot);
        slot.Ty = resTy;
        ctx.CurrentFunction->Locals.push_back(std::move(slot));

        Exir::BasicBlock* evalRightBb = ctx.CreateBlock("and.rhs");
        Exir::BasicBlock* falseBb = ctx.CreateBlock("and.false");
        Exir::BasicBlock* mergeBb = ctx.CreateBlock("and.end");

        ExprResult left = GetValue(leftNode, ctx);
        ExprResult zero = ExprResult{ Exir::Value::MakeConstant(0, left.Val.Ty), false };
        ExprResult cond = EmitBinaryInst(Exir::OpCode::CmpNe, left, zero, Type::MakeUnsigned(1), ctx);

        Exir::Instruction cbr;
        cbr.Op = Exir::OpCode::CBr;
        cbr.Operands[0] = cond.Val;
        cbr.Operands[1] = Exir::Value::MakeBlock(evalRightBb->Id);
        cbr.Operands[2] = Exir::Value::MakeBlock(falseBb->Id);
        ctx.Emit(std::move(cbr));

        // falseBb: grava 0
        ctx.SetInsertPoint(falseBb);
        {
            const std::uint32_t addrId = ctx.NewTempId();
            Exir::Instruction addrInst;
            addrInst.Op = Exir::OpCode::Addr;
            addrInst.ResultTy = Type::MakePointer(resTy, ctx.pointerBits);
            addrInst.DstId = addrId;
            addrInst.Operands[0] = Exir::Value::MakeLocal(resSlot, resTy);
            ctx.Emit(std::move(addrInst));

            Exir::Instruction storeInst;
            storeInst.Op = Exir::OpCode::Store;
            storeInst.ResultTy = resTy;
            storeInst.Operands[0] = Exir::Value::MakeConstant(0, resTy);
            storeInst.Operands[1] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
            ctx.Emit(std::move(storeInst));

            Exir::Instruction brMerge;
            brMerge.Op = Exir::OpCode::Br;
            brMerge.Operands[0] = Exir::Value::MakeBlock(mergeBb->Id);
            ctx.Emit(std::move(brMerge));
        }

        // evalRightBb: avalia rightNode, grava (right != 0)
        ctx.SetInsertPoint(evalRightBb);
        {
            ExprResult right = GetValue(rightNode, ctx);
            ExprResult zeroR = ExprResult{ Exir::Value::MakeConstant(0, right.Val.Ty), false };
            ExprResult rightBool = EmitBinaryInst(Exir::OpCode::CmpNe, right, zeroR, Type::MakeUnsigned(1), ctx);
            ExprResult valToStore = Coerce(rightBool, resTy, ctx);

            const std::uint32_t addrId = ctx.NewTempId();
            Exir::Instruction addrInst;
            addrInst.Op = Exir::OpCode::Addr;
            addrInst.ResultTy = Type::MakePointer(resTy, ctx.pointerBits);
            addrInst.DstId = addrId;
            addrInst.Operands[0] = Exir::Value::MakeLocal(resSlot, resTy);
            ctx.Emit(std::move(addrInst));

            Exir::Instruction storeInst;
            storeInst.Op = Exir::OpCode::Store;
            storeInst.ResultTy = resTy;
            storeInst.Operands[0] = valToStore.Val;
            storeInst.Operands[1] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
            ctx.Emit(std::move(storeInst));

            Exir::Instruction brMerge;
            brMerge.Op = Exir::OpCode::Br;
            brMerge.Operands[0] = Exir::Value::MakeBlock(mergeBb->Id);
            ctx.Emit(std::move(brMerge));
        }

        // mergeBb: recarrega resultado
        ctx.SetInsertPoint(mergeBb);
        const std::uint32_t addrId = ctx.NewTempId();
        Exir::Instruction addrInst;
        addrInst.Op = Exir::OpCode::Addr;
        addrInst.ResultTy = Type::MakePointer(resTy, ctx.pointerBits);
        addrInst.DstId = addrId;
        addrInst.Operands[0] = Exir::Value::MakeLocal(resSlot, resTy);
        ctx.Emit(std::move(addrInst));

        const std::uint32_t loadId = ctx.NewTempId();
        Exir::Instruction loadInst;
        loadInst.Op = Exir::OpCode::Load;
        loadInst.ResultTy = resTy;
        loadInst.DstId = loadId;
        loadInst.Operands[0] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
        ctx.Emit(std::move(loadInst));

        return ExprResult{ Exir::Value::MakeTemp(loadId, resTy), false };
    }

    // Curto-circuito para LogicalOr (||)
    if (node.Operation == BinaryOperation::LogicalOr)
    {
        const Type resTy = node.EvaluatedType.IsValid() ? node.EvaluatedType : Type::MakeUnsigned(1);
        const std::uint32_t resSlot = ctx.localCounter++;
        Exir::LocalSlot slot;
        slot.Id = resSlot;
        slot.Name = "__sc_" + std::to_string(resSlot);
        slot.Ty = resTy;
        ctx.CurrentFunction->Locals.push_back(std::move(slot));

        Exir::BasicBlock* evalRightBb = ctx.CreateBlock("or.rhs");
        Exir::BasicBlock* trueBb = ctx.CreateBlock("or.true");
        Exir::BasicBlock* mergeBb = ctx.CreateBlock("or.end");

        ExprResult left = GetValue(leftNode, ctx);
        ExprResult zero = ExprResult{ Exir::Value::MakeConstant(0, left.Val.Ty), false };
        ExprResult cond = EmitBinaryInst(Exir::OpCode::CmpNe, left, zero, Type::MakeUnsigned(1), ctx);

        Exir::Instruction cbr;
        cbr.Op = Exir::OpCode::CBr;
        cbr.Operands[0] = cond.Val;
        cbr.Operands[1] = Exir::Value::MakeBlock(trueBb->Id);
        cbr.Operands[2] = Exir::Value::MakeBlock(evalRightBb->Id);
        ctx.Emit(std::move(cbr));

        // trueBb: grava 1
        ctx.SetInsertPoint(trueBb);
        {
            const std::uint32_t addrId = ctx.NewTempId();
            Exir::Instruction addrInst;
            addrInst.Op = Exir::OpCode::Addr;
            addrInst.ResultTy = Type::MakePointer(resTy, ctx.pointerBits);
            addrInst.DstId = addrId;
            addrInst.Operands[0] = Exir::Value::MakeLocal(resSlot, resTy);
            ctx.Emit(std::move(addrInst));

            Exir::Instruction storeInst;
            storeInst.Op = Exir::OpCode::Store;
            storeInst.ResultTy = resTy;
            storeInst.Operands[0] = Exir::Value::MakeConstant(1, resTy);
            storeInst.Operands[1] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
            ctx.Emit(std::move(storeInst));

            Exir::Instruction brMerge;
            brMerge.Op = Exir::OpCode::Br;
            brMerge.Operands[0] = Exir::Value::MakeBlock(mergeBb->Id);
            ctx.Emit(std::move(brMerge));
        }

        // evalRightBb: avalia rightNode, grava (right != 0)
        ctx.SetInsertPoint(evalRightBb);
        {
            ExprResult right = GetValue(rightNode, ctx);
            ExprResult zeroR = ExprResult{ Exir::Value::MakeConstant(0, right.Val.Ty), false };
            ExprResult rightBool = EmitBinaryInst(Exir::OpCode::CmpNe, right, zeroR, Type::MakeUnsigned(1), ctx);
            ExprResult valToStore = Coerce(rightBool, resTy, ctx);

            const std::uint32_t addrId = ctx.NewTempId();
            Exir::Instruction addrInst;
            addrInst.Op = Exir::OpCode::Addr;
            addrInst.ResultTy = Type::MakePointer(resTy, ctx.pointerBits);
            addrInst.DstId = addrId;
            addrInst.Operands[0] = Exir::Value::MakeLocal(resSlot, resTy);
            ctx.Emit(std::move(addrInst));

            Exir::Instruction storeInst;
            storeInst.Op = Exir::OpCode::Store;
            storeInst.ResultTy = resTy;
            storeInst.Operands[0] = valToStore.Val;
            storeInst.Operands[1] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
            ctx.Emit(std::move(storeInst));

            Exir::Instruction brMerge;
            brMerge.Op = Exir::OpCode::Br;
            brMerge.Operands[0] = Exir::Value::MakeBlock(mergeBb->Id);
            ctx.Emit(std::move(brMerge));
        }

        // mergeBb: recarrega resultado
        ctx.SetInsertPoint(mergeBb);
        const std::uint32_t addrId = ctx.NewTempId();
        Exir::Instruction addrInst;
        addrInst.Op = Exir::OpCode::Addr;
        addrInst.ResultTy = Type::MakePointer(resTy, ctx.pointerBits);
        addrInst.DstId = addrId;
        addrInst.Operands[0] = Exir::Value::MakeLocal(resSlot, resTy);
        ctx.Emit(std::move(addrInst));

        const std::uint32_t loadId = ctx.NewTempId();
        Exir::Instruction loadInst;
        loadInst.Op = Exir::OpCode::Load;
        loadInst.ResultTy = resTy;
        loadInst.DstId = loadId;
        loadInst.Operands[0] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
        ctx.Emit(std::move(loadInst));

        return ExprResult{ Exir::Value::MakeTemp(loadId, resTy), false };
    }

    if (IsBlockComparison(node))
    {
        return BuildBufferComparison(node, ctx);
    }

    ExprResult left = GetValue(leftNode, ctx);
    ExprResult right = GetValue(rightNode, ctx);

    // Fatiamento de buffer (avanco ou truncamento de cauda)
    if (leftNode.EvaluatedType.IsBuffer() && rightNode.EvaluatedType.IsInteger())
    {
        if (node.Operation == BinaryOperation::Add)
        {
            const Type addrType = Type::MakeUnsigned(ctx.pointerBits);
            ExprResult offset = Coerce(right, addrType, ctx);
            return EmitBinaryInst(Exir::OpCode::Add, left, offset, Type::MakePointer(Type::MakeUnsigned(8), ctx.pointerBits), ctx);
        }
        if (node.Operation == BinaryOperation::Sub)
        {
            // Truncamento de cauda: endereco inalterado
            return left;
        }
    }

    // Aritmetica de ponteiros: exatamente um dos lados e ponteiro e o offset
    // do outro escala pelo tamanho do elemento.
    const bool leftIsPointer = left.Val.Ty.IsPointer();
    const bool rightIsPointer = right.Val.Ty.IsPointer();
    if ((node.Operation == BinaryOperation::Add ||
         node.Operation == BinaryOperation::Sub) &&
        leftIsPointer != rightIsPointer)
    {
        const ExprResult ptrOperand = leftIsPointer ? left : right;
        const Type ptrTy = ptrOperand.Val.Ty;
        const ExprResult scaled = ScalePointerOffset(
            leftIsPointer ? right : left, ptrTy, ctx);

        return EmitBinaryInst(
            node.Operation == BinaryOperation::Add ? Exir::OpCode::Add
                                                   : Exir::OpCode::Sub,
            ptrOperand, scaled, ptrTy, ctx);
    }

    // Aritmetica e logica
    const Type resultType = node.EvaluatedType.IsValid() ? node.EvaluatedType : left.Val.Ty;

    // O tipo da conta e o dos operandos, nao o do resultado: uma comparacao
    // devolve um booleano mas compara na largura dos lados.
    Type operandType = left.Val.Ty;
    if (right.Val.Ty.Representation().Bits > operandType.Representation().Bits)
    {
        operandType = right.Val.Ty;
    }

    const Exir::OpCode op = MapBinaryOp(node.Operation, operandType);

    left = Coerce(left, operandType, ctx);
    right = Coerce(right, operandType, ctx);

    return EmitBinaryInst(op, left, right, resultType, ctx);
}

ExprResult BuildUnary(UnaryNode& node, IRContext& ctx)
{
    Node& childNode = *node.Children[0];

    // '&b' de um bloco e o endereco do primeiro byte: o decaimento resolve
    // sem avaliar o bloco como valor.
    if (node.Operation == UnaryOperation::AddressOf &&
        childNode.GetType() == NodeType::VariableCall &&
        childNode.EvaluatedType.IsBuffer())
    {
        return ExprResult{
            DecayBuffer(static_cast<VariableCallNode&>(childNode), ctx), false
        };
    }

    ExprResult child = GetValue(childNode, ctx);

    if (node.Operation == UnaryOperation::LogicalNot)
    {
        // Negacao logica canonica: xor u1 %val, 1
        const Type u1Type = Type::MakeUnsigned(1);
        child = Coerce(child, u1Type, ctx);
        const ExprResult one = ExprResult{ Exir::Value::MakeConstant(1, u1Type), false };
        return EmitBinaryInst(Exir::OpCode::Xor, child, one, u1Type, ctx);
    }

    if (child.Val.Kind == Exir::ValueKind::Constant)
    {
        if (node.Operation == UnaryOperation::Neg)
        {
            return ExprResult{ Exir::Value::MakeConstant(static_cast<std::uint64_t>(-static_cast<std::int64_t>(child.Val.Imm)), child.Val.Ty), false };
        }
        if (node.Operation == UnaryOperation::Not)
        {
            return ExprResult{ Exir::Value::MakeConstant(~child.Val.Imm, child.Val.Ty), false };
        }
    }

    if (node.Operation == UnaryOperation::Deref)
    {
        Type valueType = node.EvaluatedType;
        if (!valueType.IsValid() || valueType.IsVoid())
        {
            valueType = Type::MakeUnsigned(8);
        }

        const std::uint32_t valId = ctx.NewTempId();
        Exir::Instruction loadInst;
        loadInst.Op = Exir::OpCode::Load;
        loadInst.ResultTy = valueType;
        loadInst.DstId = valId;
        loadInst.Operands[0] = child.Val;
        ctx.Emit(std::move(loadInst));
        return ExprResult{ Exir::Value::MakeTemp(valId, valueType), true };
    }

    if (node.Operation == UnaryOperation::AddressOf)
    {
        if (childNode.GetType() == NodeType::VariableCall)
        {
            auto& varCall = static_cast<VariableCallNode&>(childNode);
            const std::string name = varCall.Name.Text;

            if (auto slotOpt = ctx.FindLocalSlot(name); slotOpt.has_value())
            {
                const std::uint32_t slotId = *slotOpt;
                const Type ty = ctx.FindLocalType(name).IsValid() ? ctx.FindLocalType(name) : varCall.EvaluatedType;

                const std::uint32_t addrId = ctx.NewTempId();
                Exir::Instruction addrInst;
                addrInst.Op = Exir::OpCode::Addr;
                addrInst.ResultTy = Type::MakePointer(ty, ctx.pointerBits);
                addrInst.DstId = addrId;
                addrInst.Operands[0] = Exir::Value::MakeLocal(slotId, ty);
                ctx.Emit(std::move(addrInst));

                return ExprResult{ Exir::Value::MakeTemp(addrId, addrInst.ResultTy), false };
            }

            const std::uint32_t globalId = ctx.FindOrImportGlobal(name).value_or(0);
            const Type ty = varCall.EvaluatedType;

            const std::uint32_t addrId = ctx.NewTempId();
            Exir::Instruction addrInst;
            addrInst.Op = Exir::OpCode::Addr;
            addrInst.ResultTy = Type::MakePointer(ty, ctx.pointerBits);
            addrInst.DstId = addrId;
            addrInst.Operands[0] = Exir::Value::MakeGlobal(globalId, ty);
            ctx.Emit(std::move(addrInst));

            return ExprResult{ Exir::Value::MakeTemp(addrId, addrInst.ResultTy), false };
        }
    }

    const Exir::OpCode op = (node.Operation == UnaryOperation::Neg) ? Exir::OpCode::Neg : Exir::OpCode::Not;
    return EmitUnaryInst(op, child, child.Val.Ty, ctx);
}

ExprResult BuildFunctionCall(FunctionCallNode& node, IRContext& ctx)
{
    std::vector<Exir::Value> args;
    for (const auto& argNode : node.Children)
    {
        args.push_back(GetValue(*argNode, ctx).Val);
    }

    const std::uint32_t dstId = node.EvaluatedType.IsVoid() ? 0 : ctx.NewTempId();
    Exir::Instruction inst;
    inst.Op = Exir::OpCode::Call;
    inst.ResultTy = node.EvaluatedType;
    inst.DstId = dstId;

    std::string callee = IRContext::ReplaceDoublePoints(node.Name.Text);
    if (auto it = ctx.importSymbols.find(node.Name.Text); it != ctx.importSymbols.end())
    {
        callee = it->second;
    }
    else if (auto it2 = ctx.importSymbols.find(callee); it2 != ctx.importSymbols.end())
    {
        callee = it2->second;
    }

    inst.Extra = std::make_unique<Exir::InstructionExtra>();
    inst.Extra->Text = std::move(callee);
    inst.Extra->Arguments = std::move(args);

    ctx.Emit(std::move(inst));

    if (node.EvaluatedType.IsVoid())
    {
        return ExprResult{ Exir::Value::MakeNone(), false };
    }

    return ExprResult{ Exir::Value::MakeTemp(dstId, node.EvaluatedType), false };
}

ExprResult GetValue(Node& node, IRContext& ctx)
{
    // A analise ja dobrou esta expressao com precisao arbitraria (FoldConstant
    // e os inicializadores de [[const]]). Emitir o valor dispensa descer aos
    // filhos, preserve o sinal e aceita os bits para la de 64.
    if (node.FoldedValue.has_value() && !node.Attrs.Has(AttrId::Volatile) &&
        node.EvaluatedType.IsInteger() && !node.EvaluatedType.IsBuffer())
    {
        return MakeNumericConstant(*node.FoldedValue, node.EvaluatedType, ctx);
    }

    switch (node.GetType())
    {
        case NodeType::Literal:
        {
            auto& lit = static_cast<LiteralNode&>(node);
            if (lit.Value.Type == TokenType::String)
            {
                // Literais iguais partilham um bloco de dados: o internamento
                // da ao gerador um unico __emux_strN por conteudo.
                const std::uint32_t strGlobalId = ctx.InternString(lit.Value.Text);

                const std::uint32_t addrId = ctx.NewTempId();
                Exir::Instruction addrInst;
                addrInst.Op = Exir::OpCode::Addr;
                addrInst.ResultTy = Type::MakePointer(Type::MakeUnsigned(8), ctx.pointerBits);
                addrInst.DstId = addrId;
                addrInst.Operands[0] = Exir::Value::MakeGlobal(strGlobalId, addrInst.ResultTy);
                ctx.Emit(std::move(addrInst));

                return ExprResult{ Exir::Value::MakeTemp(addrId, addrInst.ResultTy), false };
            }

            // Detecta a base do literal numerico (hex 0x, bin 0b, dec)
            const std::string& litText = lit.Value.Text;

            if (auto fraction = NumericLiteral::ParseDecimal(litText))
            {
                return MakeNumericConstant(
                    fraction->first, fraction->second, lit.EvaluatedType, ctx);
            }

            int base = 10;
            std::string_view digits = litText;
            if (litText.size() > 2 && litText[0] == '0')
            {
                if (litText[1] == 'x' || litText[1] == 'X')
                {
                    base = 16;
                    digits = std::string_view(litText).substr(2);
                }
                else if (litText[1] == 'b' || litText[1] == 'B')
                {
                    base = 2;
                    digits = std::string_view(litText).substr(2);
                }
            }

            auto val = BigInt::Parse(std::string(digits), base).value_or(BigInt(0));
            return MakeNumericConstant(val, lit.EvaluatedType, ctx);
        }

        case NodeType::VariableCall:
        {
            auto& varCall = static_cast<VariableCallNode&>(node);
            const std::string name = varCall.Name.Text;

            // Elementos de enum e [[const]] escalares existem so na tabela de
            // constantes: nao ha memoria a ler, o valor e um literal.
            if (auto constValue = IROptimizer::TryEvaluateConst(node, ctx);
                constValue.has_value() && !varCall.EvaluatedType.IsBuffer())
            {
                return ExprResult{
                    Exir::Value::MakeConstant(
                        static_cast<std::uint64_t>(*constValue), varCall.EvaluatedType),
                    false
                };
            }

            // Um bloco e a propria regiao de armazenamento: em contexto de
            // valor decai para o endereco do primeiro byte, tal como um array.
            // Ler o bloco como se fosse um valor escalar leria lixo.
            if (varCall.EvaluatedType.IsBuffer())
            {
                return ExprResult{ DecayBuffer(varCall, ctx), false };
            }

            // Se for variavel local na stack ou parametro
            if (auto slotOpt = ctx.FindLocalSlot(name); slotOpt.has_value())
            {
                const std::uint32_t slotId = *slotOpt;
                const Type ty = ctx.FindLocalType(name).IsValid() ? ctx.FindLocalType(name) : varCall.EvaluatedType;

                // addr ptr, %local
                const std::uint32_t addrId = ctx.NewTempId();
                Exir::Instruction addrInst;
                addrInst.Op = Exir::OpCode::Addr;
                addrInst.ResultTy = Type::MakePointer(ty, ctx.pointerBits);
                addrInst.DstId = addrId;
                addrInst.Operands[0] = Exir::Value::MakeLocal(slotId, ty);
                ctx.Emit(std::move(addrInst));

                // load <T>, %addr
                const std::uint32_t valId = ctx.NewTempId();
                Exir::Instruction loadInst;
                loadInst.Op = Exir::OpCode::Load;
                loadInst.ResultTy = ty;
                loadInst.DstId = valId;
                loadInst.Operands[0] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
                ctx.Emit(std::move(loadInst));

                return ExprResult{ Exir::Value::MakeTemp(valId, ty), true };
            }

            // Global
            const std::uint32_t globalId = ctx.FindOrImportGlobal(name).value_or(0);
            const Type ty = varCall.EvaluatedType;

            const std::uint32_t addrId = ctx.NewTempId();
            Exir::Instruction addrInst;
            addrInst.Op = Exir::OpCode::Addr;
            addrInst.ResultTy = Type::MakePointer(ty, ctx.pointerBits);
            addrInst.DstId = addrId;
            addrInst.Operands[0] = Exir::Value::MakeGlobal(globalId, ty);
            ctx.Emit(std::move(addrInst));

            const std::uint32_t valId = ctx.NewTempId();
            Exir::Instruction loadInst;
            loadInst.Op = Exir::OpCode::Load;
            loadInst.ResultTy = ty;
            loadInst.DstId = valId;
            loadInst.Operands[0] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
            ctx.Emit(std::move(loadInst));

            return ExprResult{ Exir::Value::MakeTemp(valId, ty), true };
        }

        case NodeType::Binary:
            return BuildBinary(static_cast<BinaryNode&>(node), ctx);

        case NodeType::Unary:
            return BuildUnary(static_cast<UnaryNode&>(node), ctx);

        case NodeType::FunctionCall:
            return BuildFunctionCall(static_cast<FunctionCallNode&>(node), ctx);

        case NodeType::Cast:
        {
            auto& castNode = static_cast<CastNode&>(node);
            if (!castNode.Children.empty() && castNode.Children[0])
            {
                auto inner = GetValue(*castNode.Children[0], ctx);

                // Leitura numerica de um bloco ou view: o operando ja decaiu
                // para o endereco, por isso o cast vira um 'load' da largura
                // fisica do alvo. Os limites sao verificados na analise
                // semantica contra o tamanho restante da view.
                if (castNode.Children[0]->EvaluatedType.IsBuffer() &&
                    castNode.EvaluatedType.IsNumeric())
                {
                    const std::uint32_t loadId = ctx.NewTempId();
                    Exir::Instruction loadInst;
                    loadInst.Op = Exir::OpCode::Load;
                    loadInst.ResultTy = castNode.EvaluatedType;
                    loadInst.DstId = loadId;
                    loadInst.Operands[0] = inner.Val;
                    ctx.Emit(std::move(loadInst));

                    return ExprResult{ Exir::Value::MakeTemp(loadId, castNode.EvaluatedType), false };
                }

                // Um 'reinterpret_cast' nao converte valores: copia o padrao
                // de bits, mesmo entre classes numericas diferentes.
                if (castNode.Kind == CastKind::Reinterpret)
                {
                    if (inner.Val.Ty == castNode.EvaluatedType)
                    {
                        return inner;
                    }

                    const std::uint32_t castId = ctx.NewTempId();
                    Exir::Instruction castInst;
                    castInst.Op = Exir::OpCode::BitCast;
                    castInst.ResultTy = castNode.EvaluatedType;
                    castInst.DstId = castId;
                    castInst.Operands[0] = inner.Val;
                    ctx.Emit(std::move(castInst));

                    return ExprResult{
                        Exir::Value::MakeTemp(castId, castNode.EvaluatedType), false
                    };
                }

                return Coerce(inner, castNode.EvaluatedType, ctx);
            }
            return ExprResult{ Exir::Value::MakeNone(), false };
        }

        case NodeType::Index:
        {
            auto& indexNode = static_cast<IndexNode&>(node);

            // Um bloco ja decai para o endereco do primeiro byte em contexto
            // de valor; um ponteiro T* e lido e o indice escala por sizeof(T).
            ExprResult baseVal = GetValue(*indexNode.Children[0], ctx);
            auto idxVal = GetValue(*indexNode.Children[1], ctx);

            const Type addrType = Type::MakeUnsigned(ctx.pointerBits);
            idxVal = Coerce(idxVal, addrType, ctx);

            Type elemType = indexNode.EvaluatedType;
            if (!elemType.IsValid()) elemType = Type::MakeUnsigned(8);

            const uint32_t elemSize = elemType.GetByteSize();
            if (elemSize > 1)
            {
                ExprResult scale = ExprResult{ Exir::Value::MakeConstant(elemSize, addrType), false };
                idxVal = EmitBinaryInst(Exir::OpCode::Mul, idxVal, scale, addrType, ctx);
            }

            ExprResult effectiveAddr = EmitBinaryInst(Exir::OpCode::Add, baseVal, idxVal, Type::MakePointer(elemType, ctx.pointerBits), ctx);

            const std::uint32_t valId = ctx.NewTempId();
            Exir::Instruction loadInst;
            loadInst.Op = Exir::OpCode::Load;
            loadInst.ResultTy = elemType;
            loadInst.DstId = valId;
            loadInst.Operands[0] = effectiveAddr.Val;
            ctx.Emit(std::move(loadInst));

            return ExprResult{ Exir::Value::MakeTemp(valId, elemType), true };
        }

        default:
            return ExprResult{ Exir::Value::MakeNone(), false };
    }
}

ExprResult GetAddress(Node& node, IRContext& ctx)
{
    if (node.GetType() == NodeType::VariableCall)
    {
        auto& varCall = static_cast<VariableCallNode&>(node);
        const std::string name = varCall.Name.Text;

        // Local / parametro
        if (auto slotOpt = ctx.FindLocalSlot(name); slotOpt.has_value())
        {
            const std::uint32_t slotId = *slotOpt;
            const Type ty = ctx.FindLocalType(name).IsValid() ? ctx.FindLocalType(name) : varCall.EvaluatedType;

            const std::uint32_t addrId = ctx.NewTempId();
            Exir::Instruction addrInst;
            addrInst.Op = Exir::OpCode::Addr;
            addrInst.ResultTy = Type::MakePointer(ty, ctx.pointerBits);
            addrInst.DstId = addrId;
            addrInst.Operands[0] = Exir::Value::MakeLocal(slotId, ty);
            ctx.Emit(std::move(addrInst));

            return ExprResult{ Exir::Value::MakeTemp(addrId, Type::MakePointer(ty, ctx.pointerBits)), false };
        }

        // Global
        const std::uint32_t globalId = ctx.FindOrImportGlobal(name).value_or(0);
        const Type ty = varCall.EvaluatedType;

        const std::uint32_t addrId = ctx.NewTempId();
        Exir::Instruction addrInst;
        addrInst.Op = Exir::OpCode::Addr;
        addrInst.ResultTy = Type::MakePointer(ty, ctx.pointerBits);
        addrInst.DstId = addrId;
        addrInst.Operands[0] = Exir::Value::MakeGlobal(globalId, ty);
        ctx.Emit(std::move(addrInst));

        return ExprResult{ Exir::Value::MakeTemp(addrId, Type::MakePointer(ty, ctx.pointerBits)), false };
    }

    // Fallback: avalia a expressao normalmente
    return GetValue(node, ctx);
}

} // namespace IRExpressionBuilder

} // namespace Emux
