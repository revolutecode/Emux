#include "Emux/Generator/X64Generator.hpp"

namespace Emux
{

X64Generator::X64Generator()
    : NasmGeneratorBase(Architecture::X86_64)
{
}

void X64Generator::EmitArchHelpers(std::ostringstream&)
{
}

void X64Generator::EmitFunction(std::ostringstream& out, const Exir::Function& function)
{
    m_CurrentFunction = &function;
    StackSlots slots;
    std::ostringstream body;

    // Aloca slots para variáveis locais declaradas
    for (const auto& local : function.Locals)
    {
        slots.AllocateLocal(local.Id, local.Ty.GetByteSize());
    }

    // Emite cada BasicBlock acumulando os temporários em slots.Used
    for (const auto& block : function.Blocks)
    {
        if (!block) continue;

        body << ".bb_" << block->Id << ":\n";

        for (const auto& inst : block->Instructions)
        {
            EmitInstruction(body, inst, slots);
        }

        EmitTerminator(body, block->Terminator, slots);
    }

    out << function.Name << ":\n";
    out << "    push rbp\n";
    out << "    mov rbp, rsp\n";

    // Reserva espaço na stack: slots de variáveis + temporários + 32 bytes de shadow space do Win64
    int stackSize = slots.Used + 32;
    stackSize = (stackSize + 15) & ~15;
    out << "    sub rsp, " << stackSize << "\n";

    // Salva os primeiros 4 parâmetros passados por registradores (RCX, RDX, R8, R9) nos seus slots de stack
    static const char* paramRegs[] = { "rcx", "rdx", "r8", "r9" };
    for (std::size_t i = 0; i < function.Parameters.size() && i < 4; ++i)
    {
        const int offset = slots.GetLocal(static_cast<std::uint32_t>(i));
        out << "    mov [rbp " << (offset >= 0 ? "+" : "") << offset << "], " << paramRegs[i] << "\n";
    }

    out << body.str();
}

void X64Generator::EmitInstruction(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots)
{
    switch (inst.Op)
    {
        case Exir::OpCode::Addr:
        {
            const auto& op0 = inst.Operands[0];
            slots.Allocate(inst.DstId, 8);
            if (op0.Kind == Exir::ValueKind::LocalSlot)
            {
                const int offset = slots.GetLocal(op0.Id);
                out << "    lea rax, [rbp " << (offset >= 0 ? "+" : "") << offset << "]\n";
            }
            else if (op0.Kind == Exir::ValueKind::GlobalRef)
            {
                out << "    lea rax, [@global_" << op0.Id << "]\n";
            }
            out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            break;
        }

        case Exir::OpCode::Load:
        {
            slots.Allocate(inst.DstId, 8);
            const int ptrOffset = slots.Get(inst.Operands[0].Id);
            out << "    mov rax, [rbp " << ptrOffset << "]\n";
            const int bytes = inst.ResultTy.GetByteSize();
            if (bytes == 1)
            {
                out << "    movzx eax, byte [rax]\n";
            }
            else if (bytes == 2)
            {
                out << "    movzx eax, word [rax]\n";
            }
            else if (bytes == 4)
            {
                out << "    mov eax, dword [rax]\n";
            }
            else
            {
                out << "    mov rax, qword [rax]\n";
            }
            out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            break;
        }

        case Exir::OpCode::Store:
        {
            const auto& val = inst.Operands[0];
            const auto& ptr = inst.Operands[1];

            if (val.Kind == Exir::ValueKind::Constant)
            {
                out << "    mov rax, " << val.Imm << "\n";
            }
            else
            {
                out << "    mov rax, [rbp " << slots.Get(val.Id) << "]\n";
            }

            out << "    mov rdx, [rbp " << slots.Get(ptr.Id) << "]\n";
            const int bytes = inst.ResultTy.GetByteSize();
            if (bytes == 1)
            {
                out << "    mov [rdx], al\n";
            }
            else if (bytes == 2)
            {
                out << "    mov [rdx], ax\n";
            }
            else if (bytes == 4)
            {
                out << "    mov [rdx], eax\n";
            }
            else
            {
                out << "    mov [rdx], rax\n";
            }
            break;
        }

        // Aritmética
        case Exir::OpCode::Add:
        case Exir::OpCode::Sub:
        case Exir::OpCode::Mul:
        case Exir::OpCode::And:
        case Exir::OpCode::Or:
        case Exir::OpCode::Xor:
        {
            slots.Allocate(inst.DstId, 8);
            const auto& left = inst.Operands[0];
            const auto& right = inst.Operands[1];

            if (left.Kind == Exir::ValueKind::Constant)
                out << "    mov rax, " << left.Imm << "\n";
            else
                out << "    mov rax, [rbp " << slots.Get(left.Id) << "]\n";

            if (right.Kind == Exir::ValueKind::Constant)
                out << "    mov rdx, " << right.Imm << "\n";
            else
                out << "    mov rdx, [rbp " << slots.Get(right.Id) << "]\n";

            if (inst.Op == Exir::OpCode::Add) out << "    add rax, rdx\n";
            if (inst.Op == Exir::OpCode::Sub) out << "    sub rax, rdx\n";
            if (inst.Op == Exir::OpCode::Mul) out << "    imul rax, rdx\n";
            if (inst.Op == Exir::OpCode::And) out << "    and rax, rdx\n";
            if (inst.Op == Exir::OpCode::Or)  out << "    or rax, rdx\n";
            if (inst.Op == Exir::OpCode::Xor) out << "    xor rax, rdx\n";

            out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            break;
        }

        // Divisão e Resto
        case Exir::OpCode::SDiv:
        case Exir::OpCode::SRem:
        {
            slots.Allocate(inst.DstId, 8);
            const auto& left = inst.Operands[0];
            const auto& right = inst.Operands[1];

            if (left.Kind == Exir::ValueKind::Constant)
                out << "    mov rax, " << left.Imm << "\n";
            else
                out << "    mov rax, [rbp " << slots.Get(left.Id) << "]\n";

            if (right.Kind == Exir::ValueKind::Constant)
                out << "    mov rcx, " << right.Imm << "\n";
            else
                out << "    mov rcx, [rbp " << slots.Get(right.Id) << "]\n";

            out << "    cqo\n";
            out << "    idiv rcx\n";

            if (inst.Op == Exir::OpCode::SDiv)
                out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            else
                out << "    mov [rbp " << slots.Get(inst.DstId) << "], rdx\n";
            break;
        }

        case Exir::OpCode::UDiv:
        case Exir::OpCode::URem:
        {
            slots.Allocate(inst.DstId, 8);
            const auto& left = inst.Operands[0];
            const auto& right = inst.Operands[1];

            if (left.Kind == Exir::ValueKind::Constant)
                out << "    mov rax, " << left.Imm << "\n";
            else
                out << "    mov rax, [rbp " << slots.Get(left.Id) << "]\n";

            if (right.Kind == Exir::ValueKind::Constant)
                out << "    mov rcx, " << right.Imm << "\n";
            else
                out << "    mov rcx, [rbp " << slots.Get(right.Id) << "]\n";

            out << "    xor rdx, rdx\n";
            out << "    div rcx\n";

            if (inst.Op == Exir::OpCode::UDiv)
                out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            else
                out << "    mov [rbp " << slots.Get(inst.DstId) << "], rdx\n";
            break;
        }

        // Deslocamentos (Shifts)
        case Exir::OpCode::Shl:
        case Exir::OpCode::LShr:
        case Exir::OpCode::AShr:
        {
            slots.Allocate(inst.DstId, 8);
            const auto& left = inst.Operands[0];
            const auto& right = inst.Operands[1];

            if (left.Kind == Exir::ValueKind::Constant)
                out << "    mov rax, " << left.Imm << "\n";
            else
                out << "    mov rax, [rbp " << slots.Get(left.Id) << "]\n";

            if (right.Kind == Exir::ValueKind::Constant)
                out << "    mov rcx, " << right.Imm << "\n";
            else
                out << "    mov rcx, [rbp " << slots.Get(right.Id) << "]\n";

            if (inst.Op == Exir::OpCode::Shl)  out << "    shl rax, cl\n";
            if (inst.Op == Exir::OpCode::LShr) out << "    shr rax, cl\n";
            if (inst.Op == Exir::OpCode::AShr) out << "    sar rax, cl\n";

            out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            break;
        }

        // Comparações relacionais
        case Exir::OpCode::CmpEq:
        case Exir::OpCode::CmpNe:
        case Exir::OpCode::CmpSlt:
        case Exir::OpCode::CmpUlt:
        case Exir::OpCode::CmpSle:
        case Exir::OpCode::CmpUle:
        case Exir::OpCode::CmpSgt:
        case Exir::OpCode::CmpUgt:
        case Exir::OpCode::CmpSge:
        case Exir::OpCode::CmpUge:
        {
            slots.Allocate(inst.DstId, 8);
            const auto& left = inst.Operands[0];
            const auto& right = inst.Operands[1];

            if (left.Kind == Exir::ValueKind::Constant)
                out << "    mov rax, " << left.Imm << "\n";
            else
                out << "    mov rax, [rbp " << slots.Get(left.Id) << "]\n";

            if (right.Kind == Exir::ValueKind::Constant)
                out << "    mov rdx, " << right.Imm << "\n";
            else
                out << "    mov rdx, [rbp " << slots.Get(right.Id) << "]\n";

            out << "    cmp rax, rdx\n";

            const char* setcc = "sete";
            switch (inst.Op)
            {
                case Exir::OpCode::CmpEq:  setcc = "sete"; break;
                case Exir::OpCode::CmpNe:  setcc = "setne"; break;
                case Exir::OpCode::CmpSlt: setcc = "setl"; break;
                case Exir::OpCode::CmpUlt: setcc = "setb"; break;
                case Exir::OpCode::CmpSle: setcc = "setle"; break;
                case Exir::OpCode::CmpUle: setcc = "setbe"; break;
                case Exir::OpCode::CmpSgt: setcc = "setg"; break;
                case Exir::OpCode::CmpUgt: setcc = "seta"; break;
                case Exir::OpCode::CmpSge: setcc = "setge"; break;
                case Exir::OpCode::CmpUge: setcc = "setae"; break;
                default: break;
            }

            out << "    " << setcc << " al\n";
            out << "    movzx eax, al\n";
            out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            break;
        }

        // Operações Unárias
        case Exir::OpCode::Neg:
        case Exir::OpCode::Not:
        {
            slots.Allocate(inst.DstId, 8);
            const auto& op = inst.Operands[0];
            if (op.Kind == Exir::ValueKind::Constant)
                out << "    mov rax, " << op.Imm << "\n";
            else
                out << "    mov rax, [rbp " << slots.Get(op.Id) << "]\n";

            if (inst.Op == Exir::OpCode::Neg)
                out << "    neg rax\n";
            else
                out << "    not rax\n";

            out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            break;
        }

        // Chamadas de Função (Win64 ABI: RCX, RDX, R8, R9 + Shadow Space)
        case Exir::OpCode::Call:
        {
            if (inst.Extra)
            {
                static const char* argRegs[] = { "rcx", "rdx", "r8", "r9" };
                const auto& args = inst.Extra->Arguments;

                std::size_t extraArgs = (args.size() > 4) ? (args.size() - 4) : 0;
                std::size_t callStack = 32 + extraArgs * 8;
                if (callStack % 16 != 0) callStack += 8;

                out << "    sub rsp, " << callStack << "\n";

                for (std::size_t i = 0; i < args.size(); ++i)
                {
                    if (i < 4)
                    {
                        if (args[i].Kind == Exir::ValueKind::Constant)
                        {
                            out << "    mov " << argRegs[i] << ", " << args[i].Imm << "\n";
                        }
                        else
                        {
                            const int offset = slots.Get(args[i].Id);
                            out << "    mov " << argRegs[i] << ", [rbp " << (offset >= 0 ? "+" : "") << offset << "]\n";
                        }
                    }
                    else
                    {
                        // Argumentos adicionais na stack do caller acima do shadow space
                        if (args[i].Kind == Exir::ValueKind::Constant)
                        {
                            out << "    mov qword [rsp + " << (32 + (i - 4) * 8) << "], " << args[i].Imm << "\n";
                        }
                        else
                        {
                            const int offset = slots.Get(args[i].Id);
                            out << "    mov rax, [rbp " << (offset >= 0 ? "+" : "") << offset << "]\n";
                            out << "    mov [rsp + " << (32 + (i - 4) * 8) << "], rax\n";
                        }
                    }
                }

                out << "    call " << inst.Extra->Text << "\n";
                out << "    add rsp, " << callStack << "\n";

                if (inst.ResultTy.IsValid() && !inst.ResultTy.IsVoid())
                {
                    slots.Allocate(inst.DstId, 8);
                    out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
                }
            }
            break;
        }

        // Conversões e Casts
        case Exir::OpCode::Trunc:
        case Exir::OpCode::BitCast:
        case Exir::OpCode::IntToPtr:
        case Exir::OpCode::PtrToInt:
        {
            slots.Allocate(inst.DstId, 8);
            const auto& op = inst.Operands[0];
            if (op.Kind == Exir::ValueKind::Constant)
                out << "    mov rax, " << op.Imm << "\n";
            else
                out << "    mov rax, [rbp " << slots.Get(op.Id) << "]\n";
            out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            break;
        }

        case Exir::OpCode::ZExt:
        {
            slots.Allocate(inst.DstId, 8);
            const auto& op = inst.Operands[0];
            if (op.Kind == Exir::ValueKind::Constant)
            {
                out << "    mov rax, " << op.Imm << "\n";
            }
            else
            {
                const uint32_t srcBits = op.Ty.Bits;
                if (srcBits <= 8)
                    out << "    movzx eax, byte [rbp " << slots.Get(op.Id) << "]\n";
                else if (srcBits <= 16)
                    out << "    movzx eax, word [rbp " << slots.Get(op.Id) << "]\n";
                else if (srcBits <= 32)
                    out << "    mov eax, dword [rbp " << slots.Get(op.Id) << "]\n";
                else
                    out << "    mov rax, [rbp " << slots.Get(op.Id) << "]\n";
            }
            out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            break;
        }

        case Exir::OpCode::SExt:
        {
            slots.Allocate(inst.DstId, 8);
            const auto& op = inst.Operands[0];
            if (op.Kind == Exir::ValueKind::Constant)
            {
                out << "    mov rax, " << op.Imm << "\n";
            }
            else
            {
                const uint32_t srcBits = op.Ty.Bits;
                if (srcBits <= 8)
                    out << "    movsx rax, byte [rbp " << slots.Get(op.Id) << "]\n";
                else if (srcBits <= 16)
                    out << "    movsx rax, word [rbp " << slots.Get(op.Id) << "]\n";
                else if (srcBits <= 32)
                    out << "    movsxd rax, dword [rbp " << slots.Get(op.Id) << "]\n";
                else
                    out << "    mov rax, [rbp " << slots.Get(op.Id) << "]\n";
            }
            out << "    mov [rbp " << slots.Get(inst.DstId) << "], rax\n";
            break;
        }

        // Operações de bloco
        case Exir::OpCode::Memcpy:
        {
            // memcpy dst, src, bytes
            out << "    mov rdi, [rbp " << slots.Get(inst.Operands[0].Id) << "]\n";
            out << "    mov rsi, [rbp " << slots.Get(inst.Operands[1].Id) << "]\n";
            out << "    mov rcx, " << inst.Operands[2].Imm << "\n";
            out << "    rep movsb\n";
            break;
        }

        case Exir::OpCode::Memset:
        {
            // memset dst, val, bytes
            out << "    mov rdi, [rbp " << slots.Get(inst.Operands[0].Id) << "]\n";
            out << "    mov al, " << (inst.Operands[1].Imm & 0xFF) << "\n";
            out << "    mov rcx, " << inst.Operands[2].Imm << "\n";
            out << "    rep stosb\n";
            break;
        }

        default:
            break;
    }
}

void X64Generator::EmitTerminator(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots)
{
    switch (inst.Op)
    {
        case Exir::OpCode::Br:
            out << "    jmp .bb_" << inst.Operands[0].Id << "\n";
            break;

        case Exir::OpCode::CBr:
        {
            const auto& cond = inst.Operands[0];
            if (cond.Kind == Exir::ValueKind::Constant)
                out << "    mov al, " << cond.Imm << "\n";
            else
                out << "    mov al, [rbp " << slots.Get(cond.Id) << "]\n";

            out << "    test al, al\n";
            out << "    jnz .bb_" << inst.Operands[1].Id << "\n";
            out << "    jmp .bb_" << inst.Operands[2].Id << "\n";
            break;
        }

        case Exir::OpCode::Ret:
        {
            const auto& retVal = inst.Operands[0];
            if (retVal.Kind == Exir::ValueKind::Constant)
            {
                out << "    mov rax, " << retVal.Imm << "\n";
            }
            else if (retVal.Kind == Exir::ValueKind::Temp)
            {
                out << "    mov rax, [rbp " << slots.Get(retVal.Id) << "]\n";
            }
            out << "    mov rsp, rbp\n";
            out << "    pop rbp\n";
            out << "    ret\n";
            break;
        }

        default:
            out << "    mov rsp, rbp\n";
            out << "    pop rbp\n";
            out << "    ret\n";
            break;
    }
}

} // namespace Emux
