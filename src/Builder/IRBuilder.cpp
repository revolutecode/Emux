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
#include "Emux/Utilities/NumericLiteral.hpp"
#include "Emux/Exir/ExirPrinter.hpp"

#include <algorithm>
#include <optional>
#include <unordered_set>

namespace Emux
{

namespace
{

// Bytes iniciais de uma declaracao com inicializador — [[const]] (.rdata) ou
// mutavel (.data): um literal de string num bloco b ocupa a capacidade toda,
// com o NUL e o resto a zero; um escalar ocupa o numero de bytes do tipo em
// little-endian. 'folded' substitui o literal quando o valor ja foi calculado
// pela analise semantica, como no auto-incremento de um enum.
std::vector<std::uint8_t> ConstInitialBytes(
    const Node* init,
    const Type& type,
    const std::optional<BigInt>& folded = std::nullopt)
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

        const std::size_t count = std::min<std::size_t>(bytes.size(), literal.Value.Text.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            bytes[i] = static_cast<std::uint8_t>(literal.Value.Text[i]);
        }

        return bytes;
    }

    std::optional<BigInt> value = folded;
    if (!value && init && init->GetType() == NodeType::Literal)
    {
        const auto& literal = static_cast<const LiteralNode&>(*init);
        if (literal.Value.Type != TokenType::Number)
        {
            return bytes;
        }

        int base = 10;
        std::string_view digits = literal.Value.Text;
        if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        {
            base = 16;
            digits = digits.substr(2);
        }
        else if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B'))
        {
            base = 2;
            digits = digits.substr(2);
        }

        value = BigInt::Parse(std::string(digits), base);
    }

    if (!value)
    {
        return bytes;
    }

    const std::size_t width = bytes.size() > 0 ? bytes.size() : 8;
    if (type.IsFixed())
    {
        value = NumericLiteral::FixedStorageValue(*value, type);
    }
    else if (type.IsFloat())
    {
        value = NumericLiteral::FloatStorageBits(*value, type);
    }

    const std::vector<std::uint8_t> stored = value->ToLittleEndianBytes(width);
    for (std::size_t i = 0; i < stored.size() && i < bytes.size(); ++i)
    {
        bytes[i] = stored[i];
    }

    return bytes;
}

// O endereco de um slot da frame ou de uma regiao estatica. Um bloco e
// apontado como u8*, porque a regiao nao e um valor com a forma do tipo.
Exir::Value EmitAddress(
    IRContext& ctx,
    const Exir::Value& target,
    const Type& pointee)
{
    const Type ptrTy = pointee.IsBuffer()
        ? Type::MakePointer(Type::MakeUnsigned(8), ctx.pointerBits)
        : Type::MakePointer(pointee, ctx.pointerBits);

    const std::uint32_t addrId = ctx.NewTempId();
    Exir::Instruction inst;
    inst.Op = Exir::OpCode::Addr;
    inst.ResultTy = ptrTy;
    inst.DstId = addrId;
    inst.Operands[0] = target;
    ctx.Emit(std::move(inst));

    return Exir::Value::MakeTemp(addrId, ptrTy);
}

// Copia e escrita de bytes de uma regiao: os operandos sao ponteiros ja
// materializados e a contagem e um imediato de compile-time.
void EmitBlockCopy(
    IRContext& ctx,
    const Exir::Value& dst,
    const Exir::Value& src,
    std::uint32_t bytes)
{
    Exir::Instruction inst;
    inst.Op = Exir::OpCode::Memcpy;
    inst.Operands[0] = dst;
    inst.Operands[1] = src;
    inst.Operands[2] = Exir::Value::MakeConstant(bytes, Type::MakeUnsigned(ctx.pointerBits));
    ctx.Emit(std::move(inst));
}

void EmitBlockZero(IRContext& ctx, const Exir::Value& dst, std::uint32_t bytes)
{
    Exir::Instruction inst;
    inst.Op = Exir::OpCode::Memset;
    inst.Operands[0] = dst;
    inst.Operands[1] = Exir::Value::MakeConstant(0, Type::MakeUnsigned(8));
    inst.Operands[2] = Exir::Value::MakeConstant(bytes, Type::MakeUnsigned(ctx.pointerBits));
    ctx.Emit(std::move(inst));
}

// Recolhe os nomes das chamadas de um ramo da arvore. Os nos de controlo de
// fluxo guardam condicao e blocos em membros proprios, nao em Children, por
// isso o percurso tem de os visitar a mao: sem isto, uma chamada dentro de um
// while seria invisivel e o respetivo import seria podado em -O1+.
void CollectFunctionCalls(const Node& node, std::unordered_set<std::string>& calls)
{
    if (node.GetType() == NodeType::FunctionCall)
    {
        calls.insert(IRContext::ReplaceDoublePoints(node.GetName().Text));
    }

    switch (node.GetType())
    {
        case NodeType::If:
        {
            const auto& flow = static_cast<const IfNode&>(node);
            if (flow.Condition) CollectFunctionCalls(*flow.Condition, calls);
            for (const NodePtr& child : flow.ThenBody) if (child) CollectFunctionCalls(*child, calls);
            for (const NodePtr& child : flow.ElseBody) if (child) CollectFunctionCalls(*child, calls);
            return;
        }
        case NodeType::While:
        {
            const auto& flow = static_cast<const WhileNode&>(node);
            if (flow.Condition) CollectFunctionCalls(*flow.Condition, calls);
            for (const NodePtr& child : flow.Body) if (child) CollectFunctionCalls(*child, calls);
            return;
        }
        case NodeType::For:
        {
            const auto& flow = static_cast<const ForNode&>(node);
            const NodePtr* parts[] = { &flow.Init, &flow.Condition, &flow.Step };
            for (const NodePtr* part : parts)
            {
                if (*part) CollectFunctionCalls(**part, calls);
            }
            for (const NodePtr& child : flow.Body) if (child) CollectFunctionCalls(*child, calls);
            return;
        }
        default:
            break;
    }

    for (const NodePtr& child : node.Children)
    {
        if (child) CollectFunctionCalls(*child, calls);
    }
}

// Uma expressao usada como statement so pode ficar por construir se nao escreve
// nem chama nada. Um 'static_cast<i32>(g)' descartado e o caso tipico: so
// produziria um load morto.
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
        case NodeType::Local:
        case NodeType::Return:
        case NodeType::Break:
        case NodeType::Continue:
        case NodeType::IR:
            return false;
        default:
            break;
    }

    for (const NodePtr& child : node.Children)
    {
        if (child && !IsSideEffectFree(*child))
        {
            return false;
        }
    }

    return true;
}

// Atribuicao para um escalar float/fixo: o numero tem de ser codificado, um
// imediato '10' cru num slot f32 nao e o 10.0f que o programa pediu. Entre
// inteiros (e para ponteiros/blocos) o padrao de bits nao muda, logo nao ha
// nada a converter -- e e justamente o caso que nao se pode transformar em
// instrucao nova, sob pena de o backend nao lhe alojar destino.
bool NeedsValueConversion(const Type& from, const Type& to)
{
    const Type fromRepr = from.Representation();
    const Type toRepr = to.Representation();

    if (fromRepr == toRepr)
    {
        return false;
    }

    const bool toNumericClass = toRepr.IsFloat() || toRepr.IsFixed();
    if (!toNumericClass)
    {
        return false;
    }

    const bool fromNumericClass = fromRepr.IsFloat() || fromRepr.IsFixed();

    return !fromNumericClass || fromRepr.Bits != toRepr.Bits || fromRepr.Scale != toRepr.Scale;
}

} // namespace

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

    // Registra imports. Em -O1+ um import nunca chamado e podado antes de
    // virar Exir: a chamada resolve-se para o simbolo nativo, por isso o
    // casamento vale tanto para o nome IR como para o simbolo.
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

    if (imports)
    {
        for (const auto& [name, info] : *imports)
        {
            Exir::Import imp;
            imp.Name = IRContext::ReplaceDoublePoints(name);
            imp.Symbol = info.Symbol;
            imp.Convention = info.CallingConvention;

            if (optimize && !calledFunctions.contains(imp.Name) &&
                !calledFunctions.contains(imp.Symbol))
            {
                continue;
            }

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
    g.IsExported = !node.Attrs.Has(AttrId::Local);

    const Node* init = node.Children.empty() || !node.Children[0]
        ? nullptr
        : node.Children[0].get();

    // Um global mutavel sem inicializador nao precisa de bytes: vai para .bss,
    // reservado e zerado pelo loader. [[const]] sem inicializador continua a
    // imprimir o bloco de zeros, porque .rdata nao tem reserva estatica.
    if (g.Ty.IsValid() && (g.IsConst || init))
    {
        g.InitialData = ConstInitialBytes(init, g.Ty);
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
    // Elementos escalares dobram para a tabela de constantes e nao ocupam
    // memoria; os blocos b precisam de uma regiao, logo viram globais const.
    for (const auto& elem : node.Elements)
    {
        if (elem.Key.empty() || !elem.EvaluatedType.IsValid())
        {
            continue;
        }

        const std::string irName = IRContext::ReplaceDoublePoints(elem.Key);

        if (elem.EvaluatedType.IsBuffer())
        {
            Exir::Global g;
            g.Id = m_Context->globalCounter++;
            g.Name = irName;
            g.Ty = elem.EvaluatedType;
            g.IsConst = true;
            g.InitialData = ConstInitialBytes(
                elem.Value.get(), elem.EvaluatedType, elem.FoldedValue);

            m_Context->globalIds[elem.Key] = g.Id;
            m_Context->globalIds[irName] = g.Id;
            auto lastColon = elem.Key.rfind("::");
            if (lastColon != std::string::npos)
            {
                m_Context->globalIds[elem.Key.substr(lastColon + 2)] = g.Id;
            }

            m_Context->CurrentModule->Globals.push_back(std::move(g));
            continue;
        }

        if (!elem.FoldedValue)
        {
            continue;
        }

        // A tabela de constantes e indexada pelo nome IR, tal como as
        // referencias que o gerador procura.
        const BigInt& value = *elem.FoldedValue;
        const std::int64_t as64 = value.FitsInt64()
            ? value.ToInt64()
            : static_cast<std::int64_t>(value.ToUInt64());

        m_Context->constValues[irName] = as64;
        m_Context->constValues[elem.Key] = as64;
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
    func->IsExported = !node.Attrs.Has(AttrId::Local);

    // A convencao decide, so em win32, quem limpa a pilha e como o simbolo se
    // decora. O conflito entre atributos ja foi validado na analise semantica.
    if (node.Attrs.Has(AttrId::Stdcall))
    {
        func->Convention = Abi::Stdcall;
    }
    else if (node.Attrs.Has(AttrId::Fastcall))
    {
        func->Convention = Abi::Fastcall;
    }
    m_Context->CurrentFunction = func.get();

    // [[export("simbolo")]] da ao modulo o alias publico da funcao; o gerador
    // emite o 'global' e um segundo label, sem duplicar o corpo.
    if (node.Attrs.Has(AttrId::Export))
    {
        const std::string alias = std::string(node.Attrs.Value(AttrId::Export));
        if (!alias.empty())
        {
            Exir::Export exported;
            exported.Name = func->Name;
            exported.Alias = alias;
            m_Context->CurrentModule->Exports.push_back(std::move(exported));
        }
    }

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
            case NodeType::Break:
            case NodeType::Continue:
                BuildBreakContinue(*stmt);
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

    Node* init = node.Children.empty() || !node.Children[0]
        ? nullptr
        : node.Children[0].get();

    // Um bloco local e uma regiao dentro do slot, nao um valor: o arranque
    // zero-a a capacidade e depois copia-lhe os bytes do inicializador. A
    // frame nao nasce limpa, ao contrario da .bss.
    if (node.EvaluatedType.IsBuffer())
    {
        const std::uint32_t capacity = node.EvaluatedType.GetByteSize();
        const Exir::Value dst = EmitAddress(
            *m_Context,
            Exir::Value::MakeLocal(slotId, node.EvaluatedType),
            node.EvaluatedType);

        EmitBlockZero(*m_Context, dst, capacity);

        if (!init)
        {
            return;
        }

        auto src = IRExpressionBuilder::GetValue(*init, *m_Context);
        std::uint32_t bytes = capacity;

        if (init->GetType() == NodeType::Literal)
        {
            const auto& literal = static_cast<const LiteralNode&>(*init);
            bytes = static_cast<std::uint32_t>(
                std::min<std::size_t>(capacity, literal.Value.Text.size() + 1));
        }
        else if (init->GetType() == NodeType::VariableCall)
        {
            const Type sourceTy = init->EvaluatedType;
            if (sourceTy.IsBuffer())
            {
                bytes = std::min(capacity, sourceTy.GetByteSize());
            }
        }

        EmitBlockCopy(*m_Context, dst, src.Val, bytes);
        return;
    }

    const Exir::Value dst = EmitAddress(
        *m_Context,
        Exir::Value::MakeLocal(slotId, node.EvaluatedType),
        node.EvaluatedType);

    // Sem inicializador o slot tambem recebe zero: um 'local' nao e lido
    // antes de escrito, mas a frame partilha espaco com os temporarios.
    auto val = init
        ? IRExpressionBuilder::Coerce(
              IRExpressionBuilder::GetValue(*init, *m_Context),
              node.EvaluatedType, *m_Context)
        : IRExpressionBuilder::ExprResult{
              Exir::Value::MakeConstant(0, node.EvaluatedType), false };

    Exir::Instruction storeInst;
    storeInst.Op = Exir::OpCode::Store;
    storeInst.ResultTy = node.EvaluatedType;
    storeInst.Operands[0] = val.Val;
    storeInst.Operands[1] = dst;
    m_Context->Emit(std::move(storeInst));
}

namespace
{

// 'alvo op= rhs' = 'alvo = alvo op rhs'. Os dois lados sao normalizados para o
// tipo do alvo e o deslocamento de 'p += n' e escalado por sizeof(pointee).
IRExpressionBuilder::ExprResult ApplyCompound(
    const IRExpressionBuilder::ExprResult& current,
    const IRExpressionBuilder::ExprResult& rhs,
    BinaryOperation operation,
    const Type& type,
    IRContext& ctx
)
{
    IRExpressionBuilder::ExprResult value = rhs;

    if ((operation == BinaryOperation::Add || operation == BinaryOperation::Sub) &&
        type.IsPointer())
    {
        value = IRExpressionBuilder::ScalePointerOffset(value, type, ctx);
    }

    const IRExpressionBuilder::ExprResult left = IRExpressionBuilder::Coerce(current, type, ctx);
    value = IRExpressionBuilder::Coerce(value, type, ctx);

    return IRExpressionBuilder::EmitBinaryInst(
        IRExpressionBuilder::MapBinaryOp(operation, type), left, value, type, ctx
    );
}

} // namespace

void IRBuilder::BuildAssignment(const AssignmentNode& node)
{
    if (node.Children.empty() || !node.Children[0]) return;

    const Type addrType = Type::MakeUnsigned(m_Context->pointerBits);

    // O endereco do alvo calcula-se uma unica vez: a leitura do valor actual
    // (atribuicao composta) e a escrita tem de cair no mesmo sítio.
    Exir::Value addr;
    Type elemType;
    bool coerceValue = false;

    // Atribuição indexada: arr[i] = val
    if (node.IsIndexed && node.Target)
    {
        auto& indexNode = static_cast<IndexNode&>(*node.Target);

        // Um bloco ja decai para o endereco do primeiro byte em contexto de
        // valor; um ponteiro T* e lido e o indice escala por sizeof(T).
        IRExpressionBuilder::ExprResult baseVal =
            IRExpressionBuilder::GetValue(*indexNode.Children[0], *m_Context);

        auto idxVal = IRExpressionBuilder::GetValue(*indexNode.Children[1], *m_Context);

        idxVal = IRExpressionBuilder::Coerce(idxVal, addrType, *m_Context);

        elemType = indexNode.EvaluatedType;
        if (!elemType.IsValid())
        {
            elemType = Type::MakeUnsigned(8);
        }

        const uint32_t elemSize = elemType.GetByteSize();
        if (elemSize > 1)
        {
            auto scale = IRExpressionBuilder::ExprResult{ Exir::Value::MakeConstant(elemSize, addrType), false };
            idxVal = IRExpressionBuilder::EmitBinaryInst(Exir::OpCode::Mul, idxVal, scale, addrType, *m_Context);
        }

        addr = IRExpressionBuilder::EmitBinaryInst(
            Exir::OpCode::Add, baseVal, idxVal, Type::MakePointer(elemType, m_Context->pointerBits), *m_Context
        ).Val;
        coerceValue = true;
    }
    // Atribuição por dereferência: *ptr = val
    else if (node.IsDereference && node.Target)
    {
        addr = IRExpressionBuilder::GetValue(*node.Target, *m_Context).Val;
        elemType = node.EvaluatedType;
        coerceValue = true;
    }
    else
    {
        std::string name = node.Name.Text;
        if (name.empty() && node.Target)
        {
            name = node.Target->GetName().Text;
        }

        if (auto slotOpt = m_Context->FindLocalSlot(name); slotOpt.has_value())
        {
            const std::uint32_t slotId = *slotOpt;
            elemType = m_Context->FindLocalType(name);
            if (!elemType.IsValid())
            {
                elemType = node.EvaluatedType;
            }
            if (!elemType.IsValid())
            {
                elemType = Type::MakeUnsigned(8);
            }

            addr = EmitAddress(
                *m_Context,
                Exir::Value::MakeLocal(slotId, elemType),
                elemType);
        }
        else
        {
            elemType = node.EvaluatedType;

            // Um 'var' de bloco e uma regiao: o tipo declarado da analise e que
            // diz se a atribuicao copia bytes, nao o tipo do valor a direita.
            if (auto declared = m_Context->LookupType(name);
                declared.has_value() && declared->IsBuffer())
            {
                elemType = *declared;
            }

            if (!elemType.IsValid())
            {
                elemType = Type::MakeUnsigned(8);
            }

            const std::uint32_t globalId = m_Context->FindOrImportGlobal(name).value_or(0);
            addr = EmitAddress(
                *m_Context,
                Exir::Value::MakeGlobal(globalId, elemType),
                elemType);
        }
    }

    if (!elemType.IsValid())
    {
        elemType = Type::MakeUnsigned(8);
    }

    auto val = IRExpressionBuilder::GetValue(*node.Children[0], *m_Context);

    // Atribuicao de bloco: o destino e uma regiao, por isso copiam-se bytes
    // da fonte em vez de armazenar um valor no endereco.
    if (elemType.IsBuffer() && !node.IsIndexed && !node.IsDereference)
    {
        const Node& valueNode = *node.Children[0];
        const std::uint32_t capacity = elemType.GetByteSize();
        std::uint32_t bytes = capacity;
        Exir::Value source = val.Val;

        if (valueNode.GetType() == NodeType::Literal)
        {
            const auto& literal = static_cast<const LiteralNode&>(valueNode);
            if (literal.Value.Type != TokenType::String)
            {
                return; // O diagnostico ja pertence a analise semantica.
            }

            bytes = static_cast<std::uint32_t>(
                std::min<std::size_t>(capacity, literal.Value.Text.size() + 1));
        }
        else if (valueNode.GetType() == NodeType::VariableCall)
        {
            if (valueNode.EvaluatedType.IsBuffer())
            {
                bytes = std::min(capacity, valueNode.EvaluatedType.GetByteSize());
            }
        }
        else
        {
            return;
        }

        EmitBlockCopy(*m_Context, addr, source, bytes);
        return;
    }

    if (node.CompoundOp)
    {
        const std::uint32_t currentId = m_Context->NewTempId();
        Exir::Instruction loadInst;
        loadInst.Op = Exir::OpCode::Load;
        loadInst.ResultTy = elemType;
        loadInst.DstId = currentId;
        loadInst.Operands[0] = addr;
        m_Context->Emit(std::move(loadInst));

        val = ApplyCompound(
            IRExpressionBuilder::ExprResult{ Exir::Value::MakeTemp(currentId, elemType), false },
            val,
            *node.CompoundOp,
            elemType,
            *m_Context
        );
    }
    else if (coerceValue || NeedsValueConversion(val.Val.Ty, elemType))
    {
        val = IRExpressionBuilder::Coerce(val, elemType, *m_Context);
    }

    Exir::Instruction storeInst;
    storeInst.Op = Exir::OpCode::Store;
    storeInst.ResultTy = elemType;
    storeInst.Operands[0] = val.Val;
    storeInst.Operands[1] = addr;
    m_Context->Emit(std::move(storeInst));
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

void IRBuilder::BuildBreakContinue(const Node& node)
{
    // O alvo nao vive no no, vive na pilha de loops do builder: 'break' salta
    // para a saida do loop mais interno, 'continue' para o passo (for) ou para
    // a reavaliacao da condicao (while).
    const IRContext::LoopTargets* loop = m_Context->InnerLoop();
    if (!loop)
    {
        return;
    }

    Exir::BasicBlock* target = node.GetType() == NodeType::Break ? loop->BreakBlock : loop->ContinueBlock;
    if (!target)
    {
        return;
    }

    Exir::Instruction brInst;
    brInst.Op = Exir::OpCode::Br;
    brInst.Operands[0] = Exir::Value::MakeBlock(target->Id);
    m_Context->Emit(std::move(brInst));

    // O bloco ficou fechado e o backend emite as instrucoes antes do terminator:
    // o que se seguir ao salt tem de cair num bloco novo, senao executava-se
    // antes do proprio salt.
    m_Context->SetInsertPoint(m_Context->CreateBlock("dead"));
}

void IRBuilder::BuildExpression(Node& node)
{
    if (m_Context->optimize && IsSideEffectFree(node))
    {
        return;
    }

    IRExpressionBuilder::GetValue(node, *m_Context);
}

} // namespace Emux
