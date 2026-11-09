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

    return ExprResult{ Exir::Value::MakeTemp(dstId, resultTy), false };
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

    return ExprResult{ Exir::Value::MakeTemp(dstId, resultTy), false };
}

} // namespace

Exir::OpCode MapBinaryOp(BinaryOperation operation, const Type& type)
{
    switch (operation)
    {
        case BinaryOperation::Add:    return Exir::OpCode::Add;
        case BinaryOperation::Sub:    return Exir::OpCode::Sub;
        case BinaryOperation::Mul:    return Exir::OpCode::Mul;
        case BinaryOperation::Div:    return type.IsSigned() ? Exir::OpCode::SDiv : Exir::OpCode::UDiv;
        case BinaryOperation::Mod:        return type.IsSigned() ? Exir::OpCode::SRem : Exir::OpCode::URem;
        case BinaryOperation::Xor:        return Exir::OpCode::Xor;
        case BinaryOperation::And:
        case BinaryOperation::LogicalAnd: return Exir::OpCode::And;
        case BinaryOperation::Or:
        case BinaryOperation::LogicalOr:  return Exir::OpCode::Or;
        case BinaryOperation::Lshift:     return Exir::OpCode::Shl;
        case BinaryOperation::Rshift: return type.IsSigned() ? Exir::OpCode::AShr : Exir::OpCode::LShr;
        case BinaryOperation::Eq:     return Exir::OpCode::CmpEq;
        case BinaryOperation::Ne:     return Exir::OpCode::CmpNe;
        case BinaryOperation::Lt:     return type.IsSigned() ? Exir::OpCode::CmpSlt : Exir::OpCode::CmpUlt;
        case BinaryOperation::Gt:     return type.IsSigned() ? Exir::OpCode::CmpSgt : Exir::OpCode::CmpUgt;
        case BinaryOperation::Le:     return type.IsSigned() ? Exir::OpCode::CmpSle : Exir::OpCode::CmpUle;
        case BinaryOperation::Ge:     return type.IsSigned() ? Exir::OpCode::CmpSge : Exir::OpCode::CmpUge;
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
        return ExprResult{ Exir::Value::MakeConstant(value.Val.Imm, target), false };
    }

    const std::uint32_t dstId = ctx.NewTempId();
    Exir::Instruction inst;
    inst.DstId = dstId;
    inst.ResultTy = target;
    inst.Operands[0] = value.Val;

    if (target.IsInteger() && value.Val.Ty.IsInteger())
    {
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
    else if (target.IsPointer() && value.Val.Ty.IsInteger())
    {
        inst.Op = Exir::OpCode::IntToPtr;
    }
    else if (target.IsInteger() && value.Val.Ty.IsPointer())
    {
        inst.Op = Exir::OpCode::PtrToInt;
    }
    else
    {
        inst.Op = Exir::OpCode::BitCast;
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

ExprResult BuildBinary(BinaryNode& node, IRContext& ctx)
{
    Node& leftNode = *node.Children[0];
    Node& rightNode = *node.Children[1];

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

    // Aritmetica e logica
    const Type resultType = node.EvaluatedType.IsValid() ? node.EvaluatedType : left.Val.Ty;
    const Exir::OpCode op = MapBinaryOp(node.Operation, left.Val.Ty);

    left = Coerce(left, left.Val.Ty, ctx);
    right = Coerce(right, left.Val.Ty, ctx);

    return EmitBinaryInst(op, left, right, resultType, ctx);
}

ExprResult BuildUnary(UnaryNode& node, IRContext& ctx)
{
    Node& childNode = *node.Children[0];
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
        const std::uint32_t valId = ctx.NewTempId();
        Exir::Instruction loadInst;
        loadInst.Op = Exir::OpCode::Load;
        loadInst.ResultTy = node.EvaluatedType;
        loadInst.DstId = valId;
        loadInst.Operands[0] = child.Val;
        ctx.Emit(std::move(loadInst));
        return ExprResult{ Exir::Value::MakeTemp(valId, node.EvaluatedType), true };
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

            const std::uint32_t globalId = ctx.FindGlobalId(name).value_or(0);
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
    switch (node.GetType())
    {
        case NodeType::Literal:
        {
            auto& lit = static_cast<LiteralNode&>(node);
            if (lit.Value.Type == TokenType::String)
            {
                Exir::Global g;
                g.Id = ctx.globalCounter++;
                g.Name = "str_" + std::to_string(g.Id);
                g.Ty = Type::MakeBuffer(lit.Value.Text.size() + 1);
                g.IsConst = true;
                for (char c : lit.Value.Text)
                {
                    g.InitialData.push_back(static_cast<std::uint8_t>(c));
                }
                g.InitialData.push_back(0);

                const uint32_t strGlobalId = g.Id;
                ctx.CurrentModule->Globals.push_back(std::move(g));

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
            return ExprResult{ Exir::Value::MakeConstant(val.ToUInt64(), lit.EvaluatedType), false };
        }

        case NodeType::VariableCall:
        {
            auto& varCall = static_cast<VariableCallNode&>(node);
            const std::string name = varCall.Name.Text;

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
            const std::uint32_t globalId = ctx.FindGlobalId(name).value_or(0);
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
                return Coerce(inner, castNode.EvaluatedType, ctx);
            }
            return ExprResult{ Exir::Value::MakeNone(), false };
        }

        case NodeType::Index:
        {
            auto& indexNode = static_cast<IndexNode&>(node);

            // Para buffers locais (B16, B32, etc.) o endereco do slot JA e
            // o ponteiro base — nao podemos fazer Load (que leria o conteudo
            // do buffer como se fosse um ponteiro).  Para ponteiros normais
            // (u8*, i32*, etc.) precisamos de GetValue para ler o ponteiro.
            ExprResult baseVal;
            bool baseIsBuffer = false;
            if (indexNode.Children[0] &&
                indexNode.Children[0]->GetType() == NodeType::VariableCall)
            {
                auto& varCall = static_cast<VariableCallNode&>(*indexNode.Children[0]);
                const std::string name = varCall.Name.Text;
                Type localTy = ctx.FindLocalType(name);
                if (localTy.IsValid() && localTy.IsBuffer())
                {
                    baseVal = GetAddress(*indexNode.Children[0], ctx);
                    baseIsBuffer = true;
                }
            }
            if (!baseIsBuffer)
            {
                baseVal = GetValue(*indexNode.Children[0], ctx);
            }
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
        const std::uint32_t globalId = ctx.FindGlobalId(name).value_or(0);
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
