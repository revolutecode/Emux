#include "Emux/Builder/IRBuilder.hpp"
#include "Emux/Builder/IRExpressionBuilder.hpp"
#include "Emux/Compiler/AST/AssignmentNode.hpp"
#include "Emux/Compiler/AST/ControlFlowNodes.hpp"
#include "Emux/Compiler/AST/EnumNode.hpp"
#include "Emux/Compiler/AST/FunctionNode.hpp"
#include "Emux/Compiler/AST/IndexNode.hpp"
#include "Emux/Compiler/AST/LiteralNode.hpp"
#include "Emux/Compiler/AST/LocalNode.hpp"
#include "Emux/Compiler/AST/NodeType.hpp"
#include "Emux/Compiler/AST/Program.hpp"
#include "Emux/Compiler/AST/ReturnNode.hpp"
#include "Emux/Compiler/AST/VariableNode.hpp"
#include "Emux/Compiler/AST/VariableCallNode.hpp"
#include "Emux/Exir/ExirPrinter.hpp"

namespace Emux
{

IRBuilder::IRBuilder()
    : m_Context(std::make_unique<IRContext>())
{
}

IRBuilder::~IRBuilder() = default;

std::unique_ptr<Exir::Module> IRBuilder::BuildModule(
    const Program& program,
    const std::unordered_map<std::string, Type>* globalVariables,
    const std::unordered_map<std::string, ImportInfo>* imports,
    const std::unordered_map<std::string, FunctionSignature>* functions,
    std::uint32_t pointerBits,
    bool optimize)
{
    m_Context->CurrentModule = std::make_unique<Exir::Module>();
    m_Context->CurrentModule->TargetPointerBits = pointerBits;
    m_Context->pointerBits = pointerBits;
    m_Context->optimize = optimize;
    m_Context->globalTypes = globalVariables;

    // Registra imports
    if (imports)
    {
        for (const auto& [name, info] : *imports)
        {
            Exir::Import imp;
            imp.Name = IRContext::ReplaceDoublePoints(name);
            imp.Symbol = info.Symbol;
            imp.Convention = info.CallingConvention;

            m_Context->importSymbols[name] = info.Symbol;
            m_Context->importSymbols[imp.Name] = info.Symbol;
            m_Context->importSymbols[info.Symbol] = info.Symbol;

            m_Context->CurrentModule->Imports.push_back(std::move(imp));
        }
    }

    // Registra globais do AST
    for (const auto& node : program.Children)
    {
        if (node->GetType() == NodeType::Variable)
        {
            BuildVariable(static_cast<const VariableNode&>(*node));
        }
        else if (node->GetType() == NodeType::Enum)
        {
            BuildEnum(static_cast<const EnumNode&>(*node));
        }
    }

    // Constrói funções
    for (const auto& node : program.Children)
    {
        if (node->GetType() == NodeType::Function)
        {
            BuildFunction(static_cast<const FunctionNode&>(*node));
        }
    }

    return std::move(m_Context->CurrentModule);
}

std::string IRBuilder::Build(
    const Program& program,
    const std::unordered_map<std::string, Type>* globalVariables,
    const std::unordered_map<std::string, ImportInfo>* imports,
    const std::unordered_map<std::string, FunctionSignature>* functions,
    std::uint32_t pointerBits,
    bool optimize)
{
    auto mod = BuildModule(program, globalVariables, imports, functions, pointerBits, optimize);
    return Exir::Printer::ToString(*mod);
}

std::string IRBuilder::Build(const Program& program)
{
    return Build(program, nullptr, nullptr, nullptr, 64, false);
}

void IRBuilder::BuildVariable(const VariableNode& node)
{
    Exir::Global g;
    g.Id = m_Context->globalCounter++;
    g.Name = IRContext::ReplaceDoublePoints(node.Name.Text);
    g.Ty = node.EvaluatedType;
    g.IsConst = node.Attrs.Has(AttrId::Const);

    if (!node.Children.empty() && node.Children[0])
    {
        if (node.Children[0]->GetType() == NodeType::Literal)
        {
            const auto& lit = static_cast<const LiteralNode&>(*node.Children[0]);
            if (lit.Value.Type == TokenType::String)
            {
                for (char c : lit.Value.Text)
                {
                    g.InitialData.push_back(static_cast<std::uint8_t>(c));
                }
                g.InitialData.push_back(0); // NUL terminator
            }
            else if (lit.Value.Type == TokenType::Number)
            {
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
                std::uint64_t v = val.ToUInt64();
                int bytes = g.Ty.GetByteSize();
                if (bytes <= 0) bytes = 8;
                for (int i = 0; i < bytes; ++i)
                {
                    g.InitialData.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
                }
            }
        }
    }

    m_Context->globalIds[node.Name.Text] = g.Id;
    m_Context->globalIds[g.Name] = g.Id;
    auto lastColon = node.Name.Text.rfind("::");
    if (lastColon != std::string::npos)
    {
        m_Context->globalIds[node.Name.Text.substr(lastColon + 2)] = g.Id;
    }

    m_Context->CurrentModule->Globals.push_back(std::move(g));
}

void IRBuilder::BuildEnum(const EnumNode& node)
{
    // Enums escalares dobram diretamente; blocos viram globais const
    for (const auto& elem : node.Elements)
    {
        if (elem.FoldedValue)
        {
            m_Context->constValues[elem.Key] = elem.FoldedValue->ToInt64();
        }
    }
}

void IRBuilder::BuildFunction(const FunctionNode& node)
{
    // Se a função for importada, ela não tem corpo — é um símbolo nativo externo
    if (node.Attrs.Has(AttrId::Import))
    {
        return;
    }

    m_Context->ResetFunction();

    auto func = std::make_unique<Exir::Function>();
    func->Name = IRContext::ReplaceDoublePoints(node.Name.Text);
    func->ReturnType = node.EvaluatedType;
    m_Context->CurrentFunction = func.get();

    // Mapeia parâmetros como slots locais iniciais
    for (const auto& param : node.Parameters)
    {
        const std::string paramName = param.Name.Text;
        const std::string scopedName = node.Name.Text + "::" + paramName;
        const std::uint32_t slotId = m_Context->localCounter++;

        m_Context->localSlotIds[paramName] = slotId;
        m_Context->localSlotIds[scopedName] = slotId;
        m_Context->localTypes[paramName] = param.EvaluatedType;
        m_Context->localTypes[scopedName] = param.EvaluatedType;

        // Se a função tiver escopo de módulo/seção (ex: "main::Somar"), mapeia "main::x"
        auto colonPos = node.Name.Text.rfind("::");
        if (colonPos != std::string::npos)
        {
            const std::string modPrefix = node.Name.Text.substr(0, colonPos);
            const std::string modScopedName = modPrefix + "::" + paramName;
            m_Context->localSlotIds[modScopedName] = slotId;
            m_Context->localTypes[modScopedName] = param.EvaluatedType;
        }

        Exir::LocalSlot slot;
        slot.Id = slotId;
        slot.Name = paramName;
        slot.Ty = param.EvaluatedType;
        func->Locals.push_back(std::move(slot));

        Exir::Parameter p;
        p.Name = paramName;
        p.Ty = param.EvaluatedType;
        func->Parameters.push_back(std::move(p));
    }

    // Cria bloco de entrada
    Exir::BasicBlock* entryBb = m_Context->CreateBlock("entry");
    m_Context->SetInsertPoint(entryBb);

    // Constrói statements
    BuildStatements(node.Children);

    // Se o bloco atual não tem terminador (ex: void sem ret explícito)
    if (m_Context->CurrentBlock && !m_Context->CurrentBlock->Terminator.IsTerminator())
    {
        Exir::Instruction retInst;
        retInst.Op = Exir::OpCode::Ret;
        m_Context->Emit(std::move(retInst));
    }

    m_Context->CurrentModule->Functions.push_back(std::move(func));
}

void IRBuilder::BuildStatements(const std::vector<NodePtr>& statements)
{
    for (const auto& stmt : statements)
    {
        if (!stmt) continue;

        switch (stmt->GetType())
        {
            case NodeType::Return:
                BuildReturn(static_cast<const ReturnNode&>(*stmt));
                break;
            case NodeType::Local:
                BuildLocal(static_cast<LocalNode&>(*stmt));
                break;
            case NodeType::Assign:
                BuildAssignment(static_cast<const AssignmentNode&>(*stmt));
                break;
            case NodeType::If:
                BuildIf(static_cast<const IfNode&>(*stmt));
                break;
            case NodeType::While:
                BuildWhile(static_cast<const WhileNode&>(*stmt));
                break;
            case NodeType::For:
                BuildFor(static_cast<const ForNode&>(*stmt));
                break;
            default:
                BuildExpression(*stmt);
                break;
        }
    }
}

void IRBuilder::BuildLocal(LocalNode& node)
{
    const std::string name = node.Name.Text;
    const std::uint32_t slotId = m_Context->localCounter++;
    m_Context->localSlotIds[name] = slotId;
    m_Context->localTypes[name] = node.EvaluatedType;

    Exir::LocalSlot slot;
    slot.Id = slotId;
    slot.Name = name;
    slot.Ty = node.EvaluatedType;
    m_Context->CurrentFunction->Locals.push_back(std::move(slot));

    if (!node.Children.empty() && node.Children[0])
    {
        auto val = IRExpressionBuilder::GetValue(*node.Children[0], *m_Context);

        // addr ptr, %local
        const std::uint32_t addrId = m_Context->NewTempId();
        Exir::Instruction addrInst;
        addrInst.Op = Exir::OpCode::Addr;
        addrInst.ResultTy = Type::MakePointer(node.EvaluatedType, m_Context->pointerBits);
        addrInst.DstId = addrId;
        addrInst.Operands[0] = Exir::Value::MakeLocal(slotId, node.EvaluatedType);
        m_Context->Emit(std::move(addrInst));

        // store <T> %val, %addr
        Exir::Instruction storeInst;
        storeInst.Op = Exir::OpCode::Store;
        storeInst.ResultTy = node.EvaluatedType;
        storeInst.Operands[0] = val.Val;
        storeInst.Operands[1] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
        m_Context->Emit(std::move(storeInst));
    }
}

void IRBuilder::BuildAssignment(const AssignmentNode& node)
{
    if (node.Children.empty() || !node.Children[0]) return;

    auto val = IRExpressionBuilder::GetValue(*node.Children[0], *m_Context);

    // Atribuição indexada: arr[i] = val
    if (node.IsIndexed && node.Target)
    {
        auto& indexNode = static_cast<IndexNode&>(*node.Target);

        // Para buffers locais (B16, B32, etc.) usar GetAddress em vez de
        // GetValue — o endereco do slot JA e o ponteiro base.
        IRExpressionBuilder::ExprResult baseVal;
        bool baseIsBuffer = false;
        if (indexNode.Children[0] &&
            indexNode.Children[0]->GetType() == NodeType::VariableCall)
        {
            auto& varCall = static_cast<VariableCallNode&>(*indexNode.Children[0]);
            const std::string name = varCall.Name.Text;
            Type localTy = m_Context->FindLocalType(name);
            if (localTy.IsValid() && localTy.IsBuffer())
            {
                baseVal = IRExpressionBuilder::GetAddress(*indexNode.Children[0], *m_Context);
                baseIsBuffer = true;
            }
        }
        if (!baseIsBuffer)
        {
            baseVal = IRExpressionBuilder::GetValue(*indexNode.Children[0], *m_Context);
        }

        auto idxVal = IRExpressionBuilder::GetValue(*indexNode.Children[1], *m_Context);

        const Type addrType = Type::MakeUnsigned(m_Context->pointerBits);
        idxVal = IRExpressionBuilder::Coerce(idxVal, addrType, *m_Context);

        Type elemType = indexNode.EvaluatedType;
        if (!elemType.IsValid()) elemType = Type::MakeUnsigned(8);

        const uint32_t elemSize = elemType.GetByteSize();
        if (elemSize > 1)
        {
            auto scale = IRExpressionBuilder::ExprResult{ Exir::Value::MakeConstant(elemSize, addrType), false };
            idxVal = IRExpressionBuilder::EmitBinaryInst(Exir::OpCode::Mul, idxVal, scale, addrType, *m_Context);
        }

        auto effectiveAddr = IRExpressionBuilder::EmitBinaryInst(Exir::OpCode::Add, baseVal, idxVal, Type::MakePointer(elemType, m_Context->pointerBits), *m_Context);

        val = IRExpressionBuilder::Coerce(val, elemType, *m_Context);

        Exir::Instruction storeInst;
        storeInst.Op = Exir::OpCode::Store;
        storeInst.ResultTy = elemType;
        storeInst.Operands[0] = val.Val;
        storeInst.Operands[1] = effectiveAddr.Val;
        m_Context->Emit(std::move(storeInst));
        return;
    }

    // Atribuição por dereferência: *ptr = val
    if (node.IsDereference && node.Target)
    {
        auto ptrVal = IRExpressionBuilder::GetValue(*node.Target, *m_Context);
        Type elemType = node.EvaluatedType;
        if (!elemType.IsValid()) elemType = Type::MakeUnsigned(8);

        val = IRExpressionBuilder::Coerce(val, elemType, *m_Context);

        Exir::Instruction storeInst;
        storeInst.Op = Exir::OpCode::Store;
        storeInst.ResultTy = elemType;
        storeInst.Operands[0] = val.Val;
        storeInst.Operands[1] = ptrVal.Val;
        m_Context->Emit(std::move(storeInst));
        return;
    }

    std::string name = node.Name.Text;
    if (name.empty() && node.Target)
    {
        name = node.Target->GetName().Text;
    }

    if (auto slotOpt = m_Context->FindLocalSlot(name); slotOpt.has_value())
    {
        const std::uint32_t slotId = *slotOpt;
        const Type ty = m_Context->FindLocalType(name).IsValid() ? m_Context->FindLocalType(name) : node.EvaluatedType;

        const std::uint32_t addrId = m_Context->NewTempId();
        Exir::Instruction addrInst;
        addrInst.Op = Exir::OpCode::Addr;
        addrInst.ResultTy = Type::MakePointer(ty, m_Context->pointerBits);
        addrInst.DstId = addrId;
        addrInst.Operands[0] = Exir::Value::MakeLocal(slotId, ty);
        m_Context->Emit(std::move(addrInst));

        Exir::Instruction storeInst;
        storeInst.Op = Exir::OpCode::Store;
        storeInst.ResultTy = ty;
        storeInst.Operands[0] = val.Val;
        storeInst.Operands[1] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
        m_Context->Emit(std::move(storeInst));
    }
    else
    {
        const std::uint32_t globalId = m_Context->FindGlobalId(name).value_or(0);
        const Type ty = node.EvaluatedType;

        const std::uint32_t addrId = m_Context->NewTempId();
        Exir::Instruction addrInst;
        addrInst.Op = Exir::OpCode::Addr;
        addrInst.ResultTy = Type::MakePointer(ty, m_Context->pointerBits);
        addrInst.DstId = addrId;
        addrInst.Operands[0] = Exir::Value::MakeGlobal(globalId, ty);
        m_Context->Emit(std::move(addrInst));

        Exir::Instruction storeInst;
        storeInst.Op = Exir::OpCode::Store;
        storeInst.ResultTy = ty;
        storeInst.Operands[0] = val.Val;
        storeInst.Operands[1] = Exir::Value::MakeTemp(addrId, addrInst.ResultTy);
        m_Context->Emit(std::move(storeInst));
    }
}

void IRBuilder::BuildReturn(const ReturnNode& node)
{
    Exir::Instruction retInst;
    retInst.Op = Exir::OpCode::Ret;

    if (!node.Children.empty() && node.Children[0])
    {
        auto val = IRExpressionBuilder::GetValue(*node.Children[0], *m_Context);
        retInst.ResultTy = val.Val.Ty;
        retInst.Operands[0] = val.Val;
    }

    m_Context->Emit(std::move(retInst));
}

void IRBuilder::BuildIf(const IfNode& node)
{
    auto cond = IRExpressionBuilder::GetValue(*node.Condition, *m_Context);

    Exir::BasicBlock* thenBb = m_Context->CreateBlock("if.then");
    Exir::BasicBlock* elseBb = node.HasElse ? m_Context->CreateBlock("if.else") : nullptr;
    Exir::BasicBlock* mergeBb = m_Context->CreateBlock("if.merge");

    // cbr u1 %cond, label %then, label %else_or_merge
    Exir::Instruction cbrInst;
    cbrInst.Op = Exir::OpCode::CBr;
    cbrInst.Operands[0] = cond.Val;
    cbrInst.Operands[1] = Exir::Value::MakeBlock(thenBb->Id);
    cbrInst.Operands[2] = Exir::Value::MakeBlock(elseBb ? elseBb->Id : mergeBb->Id);
    m_Context->Emit(std::move(cbrInst));

    // Then
    m_Context->SetInsertPoint(thenBb);
    BuildStatements(node.ThenBody);
    if (m_Context->CurrentBlock && !m_Context->CurrentBlock->Terminator.IsTerminator())
    {
        Exir::Instruction brInst;
        brInst.Op = Exir::OpCode::Br;
        brInst.Operands[0] = Exir::Value::MakeBlock(mergeBb->Id);
        m_Context->Emit(std::move(brInst));
    }

    // Else (se houver)
    if (elseBb)
    {
        m_Context->SetInsertPoint(elseBb);
        BuildStatements(node.ElseBody);
        if (m_Context->CurrentBlock && !m_Context->CurrentBlock->Terminator.IsTerminator())
        {
            Exir::Instruction brInst;
            brInst.Op = Exir::OpCode::Br;
            brInst.Operands[0] = Exir::Value::MakeBlock(mergeBb->Id);
            m_Context->Emit(std::move(brInst));
        }
    }

    m_Context->SetInsertPoint(mergeBb);
}

void IRBuilder::BuildWhile(const WhileNode& node)
{
    Exir::BasicBlock* condBb = m_Context->CreateBlock("while.cond");
    Exir::BasicBlock* bodyBb = m_Context->CreateBlock("while.body");
    Exir::BasicBlock* exitBb = m_Context->CreateBlock("while.exit");

    // br label %cond
    Exir::Instruction brInst;
    brInst.Op = Exir::OpCode::Br;
    brInst.Operands[0] = Exir::Value::MakeBlock(condBb->Id);
    m_Context->Emit(std::move(brInst));

    // Cond
    m_Context->SetInsertPoint(condBb);
    auto cond = IRExpressionBuilder::GetValue(*node.Condition, *m_Context);
    Exir::Instruction cbrInst;
    cbrInst.Op = Exir::OpCode::CBr;
    cbrInst.Operands[0] = cond.Val;
    cbrInst.Operands[1] = Exir::Value::MakeBlock(bodyBb->Id);
    cbrInst.Operands[2] = Exir::Value::MakeBlock(exitBb->Id);
    m_Context->Emit(std::move(cbrInst));

    // Body
    m_Context->loopStack.push_back({ exitBb, condBb });
    m_Context->SetInsertPoint(bodyBb);
    BuildStatements(node.Body);
    if (m_Context->CurrentBlock && !m_Context->CurrentBlock->Terminator.IsTerminator())
    {
        Exir::Instruction loopBr;
        loopBr.Op = Exir::OpCode::Br;
        loopBr.Operands[0] = Exir::Value::MakeBlock(condBb->Id);
        m_Context->Emit(std::move(loopBr));
    }
    m_Context->loopStack.pop_back();

    m_Context->SetInsertPoint(exitBb);
}

void IRBuilder::BuildFor(const ForNode& node)
{
    if (node.Init)
    {
        std::vector<NodePtr> initStmts;
        initStmts.push_back(NodePtr(const_cast<Node*>(node.Init.get())));
        BuildStatements(initStmts);
        initStmts[0].release();
    }

    Exir::BasicBlock* condBb = m_Context->CreateBlock("for.cond");
    Exir::BasicBlock* bodyBb = m_Context->CreateBlock("for.body");
    Exir::BasicBlock* stepBb = m_Context->CreateBlock("for.step");
    Exir::BasicBlock* exitBb = m_Context->CreateBlock("for.exit");

    Exir::Instruction brInst;
    brInst.Op = Exir::OpCode::Br;
    brInst.Operands[0] = Exir::Value::MakeBlock(condBb->Id);
    m_Context->Emit(std::move(brInst));

    // Cond
    m_Context->SetInsertPoint(condBb);
    if (node.Condition)
    {
        auto cond = IRExpressionBuilder::GetValue(*node.Condition, *m_Context);
        Exir::Instruction cbrInst;
        cbrInst.Op = Exir::OpCode::CBr;
        cbrInst.Operands[0] = cond.Val;
        cbrInst.Operands[1] = Exir::Value::MakeBlock(bodyBb->Id);
        cbrInst.Operands[2] = Exir::Value::MakeBlock(exitBb->Id);
        m_Context->Emit(std::move(cbrInst));
    }
    else
    {
        Exir::Instruction loopBr;
        loopBr.Op = Exir::OpCode::Br;
        loopBr.Operands[0] = Exir::Value::MakeBlock(bodyBb->Id);
        m_Context->Emit(std::move(loopBr));
    }

    // Body
    m_Context->loopStack.push_back({ exitBb, stepBb });
    m_Context->SetInsertPoint(bodyBb);
    BuildStatements(node.Body);
    if (m_Context->CurrentBlock && !m_Context->CurrentBlock->Terminator.IsTerminator())
    {
        Exir::Instruction brStep;
        brStep.Op = Exir::OpCode::Br;
        brStep.Operands[0] = Exir::Value::MakeBlock(stepBb->Id);
        m_Context->Emit(std::move(brStep));
    }
    m_Context->loopStack.pop_back();

    // Step
    m_Context->SetInsertPoint(stepBb);
    if (node.Step)
    {
        std::vector<NodePtr> stepStmts;
        stepStmts.push_back(NodePtr(const_cast<Node*>(node.Step.get())));
        BuildStatements(stepStmts);
        stepStmts[0].release();
    }
    Exir::Instruction brCond;
    brCond.Op = Exir::OpCode::Br;
    brCond.Operands[0] = Exir::Value::MakeBlock(condBb->Id);
    m_Context->Emit(std::move(brCond));

    m_Context->SetInsertPoint(exitBb);
}

void IRBuilder::BuildExpression(Node& node)
{
    IRExpressionBuilder::GetValue(node, *m_Context);
}

} // namespace Emux
