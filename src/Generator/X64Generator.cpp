#include "Emux/Generator/X64Generator.hpp"
#include "Emux/Generator/NasmUtils.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Emux
{

namespace
{

// As rotinas do runtime recebem enderecos, nunca valores: um imediato tem de
// ser materializado em memoria antes da chamada. Dois slots chegam porque o
// operando ja materializado nunca e reescrito dentro da mesma instrucao.
constexpr std::uint32_t kScratchIdBase = 0xF0000000u;
constexpr std::size_t kScratchCount = 2;

// Slots das conversoes de argumento nas chamadas; longe dos IDs dos
// temporarios do modulo e dos dois scratch de imediatos.
constexpr std::uint32_t kArgConvIdBase = 0xF0010000u;

// Ponto fixo partilha o comportamento inteiro com sinal; float e um padrao de
// bits que nao se estende.
bool SignedLike(const Type& type)
{
    const Type repr = type.Representation();
    return repr.IsSigned() || repr.IsFixed();
}

// Acima de 8 bytes o valor nao cabe num slot: vive numa regiao de
// ceil(bytes/8) e so e tocado por 'lea' mais as rotinas multi-limb.
bool IsRegion(const Type& type)
{
    const Type repr = type.Representation();
    return repr.IsValid() && repr.GetByteSize() > 8;
}

std::uint32_t StorageBytes(const Type& type)
{
    const Type repr = type.Representation();
    if (!repr.IsValid())
    {
        return 8;
    }

    return std::max<std::uint32_t>(repr.GetByteSize(), 1);
}

std::uint32_t SlotBytes(const Type& type)
{
    const std::uint32_t bytes = StorageBytes(type);
    return bytes <= 8 ? 8 : ((bytes + 7) & ~7u);
}

const char* OpName(Exir::OpCode op)
{
    switch (op)
    {
        case Exir::OpCode::Add: return "add";
        case Exir::OpCode::Sub: return "sub";
        case Exir::OpCode::Mul: return "mul";
        case Exir::OpCode::UDiv: return "udiv";
        case Exir::OpCode::SDiv: return "sdiv";
        case Exir::OpCode::URem: return "urem";
        case Exir::OpCode::SRem: return "srem";
        case Exir::OpCode::Shl: return "shl";
        case Exir::OpCode::LShr: return "lshr";
        case Exir::OpCode::AShr: return "ashr";
        case Exir::OpCode::Neg: return "neg";
        case Exir::OpCode::Not: return "not";
        case Exir::OpCode::Conv: return "conv";
        case Exir::OpCode::Trunc: return "trunc";
        case Exir::OpCode::SExt: return "sext";
        case Exir::OpCode::ZExt: return "zext";
        case Exir::OpCode::BitCast: return "bitcast";
        default: return "?";
    }
}

// Empacotamento entendido pelo runtime: bits | (expoente << 20).
std::uint64_t FloatSpec(const Type& type)
{
    const Type repr = type.Representation();
    return static_cast<std::uint64_t>(repr.Bits) |
        (static_cast<std::uint64_t>(repr.FloatExponentBits()) << 20);
}

std::string Hex64(std::uint64_t value)
{
    static constexpr char digits[] = "0123456789ABCDEF";

    std::string body(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        body[static_cast<std::size_t>(i)] = digits[value & 0xFu];
        value >>= 4;
    }

    std::size_t start = 0;
    while (start + 1 < body.size() && body[start] == '0')
    {
        ++start;
    }

    return "0x" + body.substr(start);
}

// Padrao de 64 bits que um slot guarda para este tipo: sinal replicado nos
// inteiros assinados, zeros acima dos bytes de armazenamento no float.
std::uint64_t Canonicalize(std::uint64_t imm, const Type& type)
{
    const Type repr = type.Representation();
    if (!repr.IsValid() || repr.Bits == 0 || repr.Bits >= 64)
    {
        return imm;
    }

    const std::uint64_t mask = (1ull << repr.Bits) - 1;
    imm &= mask;

    if (repr.IsFloat() || !SignedLike(repr))
    {
        return imm;
    }

    const std::uint64_t signBit = 1ull << (repr.Bits - 1);
    return (imm ^ signBit) - signBit;
}

std::string RegName(std::string_view reg64, std::uint32_t bits)
{
    struct Register
    {
        const char* r64;
        const char* r32;
        const char* r16;
        const char* r8;
    };

    static const Register registers[] = {
        { "rax", "eax", "ax", "al" },
        { "rcx", "ecx", "cx", "cl" },
        { "rdx", "edx", "dx", "dl" },
        { "rbx", "ebx", "bx", "bl" },
        { "rsi", "esi", "si", "sil" },
        { "rdi", "edi", "di", "dil" },
        { "r8", "r8d", "r8w", "r8b" },
        { "r9", "r9d", "r9w", "r9b" },
        { "r10", "r10d", "r10w", "r10b" },
        { "r11", "r11d", "r11w", "r11b" }
    };

    for (const Register& reg : registers)
    {
        if (reg64 == reg.r64)
        {
            if (bits <= 8) return reg.r8;
            if (bits <= 16) return reg.r16;
            if (bits <= 32) return reg.r32;
            return reg.r64;
        }
    }

    return std::string(reg64);
}

// [rbp] quando o deslocamento e zero: "[rbp + 0]" nao e um operando valido.
std::string MemAt(int offset)
{
    if (offset == 0)
    {
        return "[rbp]";
    }

    return offset < 0
        ? "[rbp - " + std::to_string(-offset) + "]"
        : "[rbp + " + std::to_string(offset) + "]";
}

std::string MemAt(StackSlots& slots, std::uint32_t id, std::uint32_t bytes)
{
    return MemAt(slots.AllocateTemp(id, static_cast<int>(bytes)));
}

std::string ScratchMemory(StackSlots& slots, std::size_t index)
{
    return MemAt(slots, kScratchIdBase + static_cast<std::uint32_t>(index), 8);
}

std::string AddOffset(const std::string& mem, std::uint32_t extra)
{
    if (extra == 0)
    {
        return mem;
    }

    return mem.substr(0, mem.size() - 1) + " + " + std::to_string(extra) + "]";
}

void EmitImmediate(std::ostringstream& out, std::uint64_t imm, const char* reg)
{
    const std::int64_t signedValue = static_cast<std::int64_t>(imm);

    if (signedValue >= std::numeric_limits<std::int32_t>::min() &&
        signedValue <= std::numeric_limits<std::int32_t>::max())
    {
        out << "    mov " << reg << ", " << signedValue << "\n";
        return;
    }

    out << "    mov " << reg << ", " << Hex64(imm) << "\n";
}

// Pedacos de 8, 4, 2 ou 1 byte na ordem maxima que nao ultrapassa o valor,
// porque o x64 so le e escreve em larguras de registo.
void EmitExactLoad(std::ostringstream& out, const std::string& mem, std::uint32_t bytes, std::string_view reg)
{
    if (bytes >= 8)
    {
        out << "    mov " << reg << ", qword " << mem << "\n";
        return;
    }

    const std::string scratch = reg == "r11" ? "r10" : "r11";

    out << "    xor " << RegName(reg, 32) << ", " << RegName(reg, 32) << "\n";

    std::uint32_t offset = 0;
    while (bytes > 0)
    {
        const std::uint32_t chunk = bytes >= 4 ? 4 : bytes >= 2 ? 2 : 1;

        if (chunk == 4)
        {
            out << "    mov " << RegName(scratch, 32) << ", dword "
                << AddOffset(mem, offset) << "\n";
        }
        else
        {
            out << "    movzx " << RegName(scratch, 32) << ", "
                << NasmUtils::NasmSize(static_cast<int>(chunk * 8)) << " "
                << AddOffset(mem, offset) << "\n";
        }

        if (offset > 0)
        {
            out << "    shl " << scratch << ", " << (offset * 8) << "\n";
        }

        out << "    or " << reg << ", " << scratch << "\n";

        offset += chunk;
        bytes -= chunk;
    }
}

void EmitExactStore(std::ostringstream& out, const std::string& mem, std::uint32_t bytes, std::string_view reg)
{
    if (bytes == 0)
    {
        return;
    }

    if (bytes >= 8)
    {
        out << "    mov qword " << mem << ", " << reg << "\n";
        return;
    }

    const std::string scratch = reg == "r10" ? "r11" : "r10";
    bool shifted = false;
    std::uint32_t offset = 0;

    while (bytes > 0)
    {
        const std::uint32_t chunk = bytes >= 4 ? 4 : bytes >= 2 ? 2 : 1;
        const std::string source = shifted
            ? RegName(scratch, chunk * 8)
            : RegName(reg, chunk * 8);

        out << "    mov " << NasmUtils::NasmSize(static_cast<int>(chunk * 8)) << " "
            << AddOffset(mem, offset) << ", " << source << "\n";

        offset += chunk;
        bytes -= chunk;

        if (bytes > 0)
        {
            out << "    mov " << scratch << ", " << reg << "\n";
            out << "    shr " << scratch << ", " << (offset * 8) << "\n";
            shifted = true;
        }
    }
}

// Vaivem shl/sar (ou shl/shr) que deixa no registo apenas os Bits uteis. O
// float usa a variante que zera: e um padrao de bits, nunca se estende.
void EmitIsolate(std::ostringstream& out, std::string_view reg, const Type& type)
{
    const Type repr = type.Representation();
    if (!repr.IsValid() || repr.Bits == 0 || repr.Bits >= 64)
    {
        return;
    }

    const std::uint32_t shift = 64 - repr.Bits;
    out << "    shl " << reg << ", " << shift << "\n";
    out << (SignedLike(repr) ? "    sar " : "    shr ") << reg << ", " << shift << "\n";
}

// Leitura de uma memoria com a largura exacta do tipo: slots de locais (que o
// Store escreve pela mesma medida) e memoria apontada por um ponteiro.
void EmitTypedLoad(std::ostringstream& out, const std::string& mem, const Type& type, std::string_view reg)
{
    EmitExactLoad(out, mem, std::min<std::uint32_t>(StorageBytes(type), 8), reg);
    EmitIsolate(out, reg, type);
}

// Leitura do slot de 8 bytes de um temporario, sempre no padrao canonico.
void EmitSlotLoad(std::ostringstream& out, const std::string& mem, const Type& type, std::string_view reg)
{
    out << "    mov " << reg << ", qword " << mem << "\n";
    EmitIsolate(out, reg, type);
}

const char* ComparisonSetcc(Exir::OpCode op, bool isSigned)
{
    switch (op)
    {
        case Exir::OpCode::CmpEq: return "sete";
        case Exir::OpCode::CmpNe: return "setne";
        case Exir::OpCode::CmpSlt: case Exir::OpCode::CmpUlt: return isSigned ? "setl" : "setb";
        case Exir::OpCode::CmpSgt: case Exir::OpCode::CmpUgt: return isSigned ? "setg" : "seta";
        case Exir::OpCode::CmpSle: case Exir::OpCode::CmpUle: return isSigned ? "setle" : "setbe";
        case Exir::OpCode::CmpSge: case Exir::OpCode::CmpUge: return isSigned ? "setge" : "setae";
        default: return "sete";
    }
}

bool IsComparison(Exir::OpCode op)
{
    return ComparisonSetcc(op, false) != nullptr && op != Exir::OpCode::CmpEq ||
        op == Exir::OpCode::CmpNe || op == Exir::OpCode::CmpUlt ||
        op == Exir::OpCode::CmpSlt || op == Exir::OpCode::CmpUle ||
        op == Exir::OpCode::CmpSle || op == Exir::OpCode::CmpUgt ||
        op == Exir::OpCode::CmpSgt || op == Exir::OpCode::CmpUge ||
        op == Exir::OpCode::CmpSge;
}

// Funcao definida neste modulo procurada pelo simbolo da chamada; nula para
// os 'import' C, cujo prototipo o backend nao conhece.
const Exir::Function* FindCallee(const Exir::Module* module, const std::string& symbol)
{
    if (module == nullptr)
    {
        return nullptr;
    }

    for (const std::unique_ptr<Exir::Function>& function : module->Functions)
    {
        if (function && function->Name == symbol)
        {
            return function.get();
        }
    }

    return nullptr;
}

// Mudanca de classe numerica (inteiro, float, fixo), de escala ou de largura
// de float: o padrao de bits nao se preserva, pelo que o argumento carece de
// conversao. Inteiro para inteiro e ponteiro partilham o mesmo padrao.
bool NeedsArgumentConv(const Type& from, const Type& to)
{
    if (!from.IsValid() || !to.IsValid())
    {
        return false;
    }

    const Type source = from.Representation();
    const Type target = to.Representation();

    if (source.IsFloat() != target.IsFloat() || source.IsFixed() != target.IsFixed())
    {
        return true;
    }

    if (source.IsFloat())
    {
        return source.Bits != target.Bits ||
            source.FloatExponentBits() != target.FloatExponentBits();
    }

    if (source.IsFixed())
    {
        return source.Bits != target.Bits || source.Scale != target.Scale;
    }

    return false;
}

} // namespace

X64Generator::X64Generator()
    : NasmGeneratorBase(Architecture::X86_64)
{
}

std::string X64Generator::SlotMemory(StackSlots& slots, std::uint32_t id, std::uint32_t bytes)
{
    return MemAt(slots, id, bytes);
}

std::string X64Generator::ScratchMemory(StackSlots& slots, std::size_t index)
{
    return ::Emux::ScratchMemory(slots, index);
}

std::string X64Generator::NewLabel(std::string_view prefix)
{
    return ".L" + std::string(prefix) + "_" + std::to_string(m_LabelCounter++);
}

void X64Generator::EmitOperand(
    std::ostringstream& out,
    const Exir::Value& value,
    const Type& type,
    const char* reg,
    StackSlots& slots
)
{
    switch (value.Kind)
    {
        case Exir::ValueKind::Constant:
            EmitImmediate(out, Canonicalize(value.Imm, type), reg);
            return;

        case Exir::ValueKind::GlobalRef:
            EmitTypedLoad(out, "[" + GlobalLabel(value) + "]", type, reg);
            return;

        case Exir::ValueKind::Temp:
            EmitSlotLoad(out, MemAt(slots.GetTemp(value.Id)), type, reg);
            return;

        case Exir::ValueKind::LocalSlot:
            EmitTypedLoad(out, MemAt(slots.GetLocal(value.Id)), type, reg);
            return;

        default:
            throw std::runtime_error(
                "Backend x64: operando sem leitura escalar para o tipo '" +
                type.ToString() + "'."
            );
    }
}

void X64Generator::EmitPointer(
    std::ostringstream& out,
    const Exir::Value& value,
    const char* reg,
    StackSlots& slots
)
{
    switch (value.Kind)
    {
        case Exir::ValueKind::Constant:
            EmitImmediate(out, value.Imm, reg);
            return;

        case Exir::ValueKind::GlobalRef:
            out << "    lea " << reg << ", [" << GlobalLabel(value) << "]\n";
            return;

        case Exir::ValueKind::Temp:
            out << "    mov " << reg << ", qword " << MemAt(slots.GetTemp(value.Id)) << "\n";
            return;

        case Exir::ValueKind::LocalSlot:
            out << "    mov " << reg << ", qword " << MemAt(slots.GetLocal(value.Id)) << "\n";
            return;

        case Exir::ValueKind::BlockRef:
            out << "    lea " << reg << ", [.bb_" << value.Id << "]\n";
            return;

        default:
            throw std::runtime_error("Backend x64: ponteiro a partir de um operando vazio.");
    }
}

void X64Generator::EmitOperandAddress(
    std::ostringstream& out,
    const Exir::Value& value,
    StackSlots& slots,
    const char* reg,
    std::size_t scratchIndex
)
{
    switch (value.Kind)
    {
        case Exir::ValueKind::GlobalRef:
            out << "    lea " << reg << ", [" << GlobalLabel(value) << "]\n";
            return;

        case Exir::ValueKind::Temp:
            out << "    lea " << reg << ", " << MemAt(slots.GetTemp(value.Id)) << "\n";
            return;

        case Exir::ValueKind::LocalSlot:
            out << "    lea " << reg << ", " << MemAt(slots.GetLocal(value.Id)) << "\n";
            return;

        case Exir::ValueKind::Constant:
        {
            if (StorageBytes(value.Ty) > 8)
            {
                throw std::runtime_error(
                    "Backend x64: constante larga sem blob de dados; o builder "
                    "deve internala na .rdata."
                );
            }

            const std::string scratch = ScratchMemory(slots, scratchIndex);
            EmitImmediate(out, Canonicalize(value.Imm, value.Ty), "r11");
            out << "    mov qword " << scratch << ", r11\n";
            out << "    lea " << reg << ", " << scratch << "\n";
            return;
        }

        default:
            throw std::runtime_error("Backend x64: endereco de um operando sem storage.");
    }
}

// ---------------------------------------------------------------------------
// Funcao
// ---------------------------------------------------------------------------

void X64Generator::EmitFunction(std::ostringstream& text, const Exir::Function& function)
{
    m_CurrentFunction = &function;
    m_LabelCounter = 0;

    StackSlots slots;
    for (const Exir::LocalSlot& local : function.Locals)
    {
        slots.AllocateLocal(local.Id, static_cast<int>(SlotBytes(local.Ty)));
    }

    for (std::size_t i = 0; i < kScratchCount; ++i)
    {
        ScratchMemory(slots, i);
    }

    std::ostringstream body;
    for (const std::unique_ptr<Exir::BasicBlock>& block : function.Blocks)
    {
        if (!block)
        {
            continue;
        }

        body << ".bb_" << block->Id << ":\n";
        for (const Exir::Instruction& inst : block->Instructions)
        {
            EmitInstruction(body, inst, slots);
        }

        EmitTerminator(body, block->Terminator, slots);
    }

    // Os 32 bytes de shadow space do Win64 vao no fundo da frame, que fica
    // assim no minimo alinhada a 16.
    const std::uint32_t frameSize =
        (static_cast<std::uint32_t>(slots.Used) + 32 + 15) & ~15u;

    text << function.Name << ":\n";
    text << "    push rbp\n";
    text << "    mov rbp, rsp\n";
    text << "    sub rsp, " << frameSize << "\n";

    if (function.Parameters.size() > function.Locals.size())
    {
        throw std::runtime_error("Backend x64: parametros sem slot local reservado.");
    }

    // Os parametros partilham o espaco dos locais: os primeiros quatro chegam
    // em rcx, rdx, r8 e r9; os restantes ja estao acima do retorno.
    static const char* const paramRegs[4] = { "rcx", "rdx", "r8", "r9" };
    for (std::size_t i = 0; i < function.Parameters.size(); ++i)
    {
        const std::string mem = MemAt(slots.GetLocal(function.Locals[i].Id));

        if (i < 4)
        {
            text << "    mov qword " << mem << ", " << paramRegs[i] << "\n";
            continue;
        }

        text << "    mov rax, [rbp + " << (48 + 8 * (i - 4)) << "]\n";
        text << "    mov qword " << mem << ", rax\n";
    }

    text << body.str();
}

// ---------------------------------------------------------------------------
// Instrucoes
// ---------------------------------------------------------------------------

void X64Generator::EmitInstruction(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    switch (inst.Op)
    {
        case Exir::OpCode::Addr:
        {
            const Exir::Value& base = inst.Operands[0];
            const std::string mem = SlotMemory(slots, inst.DstId, 8);

            if (base.Kind == Exir::ValueKind::GlobalRef)
            {
                out << "    lea rax, [" << GlobalLabel(base) << "]\n";
            }
            else if (base.Kind == Exir::ValueKind::LocalSlot)
            {
                out << "    lea rax, " << MemAt(slots.GetLocal(base.Id)) << "\n";
            }
            else if (base.Kind == Exir::ValueKind::Temp)
            {
                out << "    lea rax, " << MemAt(slots.GetTemp(base.Id)) << "\n";
            }
            else
            {
                throw std::runtime_error("Backend x64: addr de um operando sem storage.");
            }

            out << "    mov qword " << mem << ", rax\n";
            return;
        }

        case Exir::OpCode::Load:
        {
            const Type& type = inst.ResultTy;
            EmitPointer(out, inst.Operands[0], "rsi", slots);

            if (IsRegion(type))
            {
                EmitWideCopy(out, inst.Operands[0], inst.DstId, StorageBytes(type), slots, true);
                return;
            }

            EmitTypedLoad(out, "[rsi]", type, "rax");
            out << "    mov qword " << SlotMemory(slots, inst.DstId, StorageBytes(type)) << ", rax\n";
            return;
        }

        case Exir::OpCode::Store:
        {
            const Type& type = inst.ResultTy;
            const Exir::Value& value = inst.Operands[0];

            if (IsRegion(type))
            {
                // A regiao de destino pode ser mais larga que o valor (um i72
                // guardado num i128): os bytes que faltam recebem a extensao de
                // sinal da origem, nunca o lixo do slot.
                const std::uint32_t dstBytes = StorageBytes(type);

                EmitPointer(out, inst.Operands[1], "r11", slots);

                std::uint32_t filled = 8;
                if (IsRegion(value.Ty))
                {
                    const std::uint32_t srcBytes = StorageBytes(value.Ty);

                    EmitOperandAddress(out, value, slots, "r10", 0);
                    out << "    mov rcx, r11\n";
                    out << "    mov rdx, r10\n";
                    out << "    mov r8d, " << std::min(srcBytes, dstBytes) << "\n";
                    out << "    call __emux_wcopy\n";

                    if (srcBytes >= dstBytes)
                    {
                        return;
                    }

                    out << "    movzx eax, byte [r10+" << (srcBytes - 1) << "]\n";
                    out << (SignedLike(value.Ty) ? "    sar al, 7\n" : "    xor eax, eax\n");
                    filled = srcBytes;
                }
                else
                {
                    EmitOperand(out, value, value.Ty, "rax", slots);
                    out << "    mov qword [r11], rax\n";
                    out << "    mov rdx, rax\n";
                    out << (SignedLike(value.Ty) ? "    sar rdx, 63\n" : "    xor edx, edx\n");
                    out << "    mov rax, rdx\n";
                }

                const std::string loop = NewLabel("wstore");
                const std::string done = NewLabel("wstored");

                out << "    mov ecx, " << filled << "\n";
                out << loop << ":\n";
                out << "    cmp ecx, " << dstBytes << "\n";
                out << "    jae " << done << "\n";
                out << "    mov [r11+rcx], al\n";
                out << "    inc ecx\n";
                out << "    jmp " << loop << "\n";
                out << done << ":\n";
                return;
            }

            EmitOperand(out, inst.Operands[0], type, "rax", slots);
            EmitPointer(out, inst.Operands[1], "rdx", slots);
            EmitExactStore(out, "[rdx]", StorageBytes(type), "rax");
            return;
        }

        case Exir::OpCode::Add:
        case Exir::OpCode::Sub:
        case Exir::OpCode::Mul:
        case Exir::OpCode::UDiv:
        case Exir::OpCode::SDiv:
        case Exir::OpCode::URem:
        case Exir::OpCode::SRem:
        case Exir::OpCode::And:
        case Exir::OpCode::Or:
        case Exir::OpCode::Xor:
        case Exir::OpCode::Shl:
        case Exir::OpCode::LShr:
        case Exir::OpCode::AShr:
            EmitArithmetic(out, inst, slots);
            return;

        case Exir::OpCode::CmpEq:
        case Exir::OpCode::CmpNe:
        case Exir::OpCode::CmpUlt:
        case Exir::OpCode::CmpSlt:
        case Exir::OpCode::CmpUle:
        case Exir::OpCode::CmpSle:
        case Exir::OpCode::CmpUgt:
        case Exir::OpCode::CmpSgt:
        case Exir::OpCode::CmpUge:
        case Exir::OpCode::CmpSge:
            EmitCompare(out, inst, slots);
            return;

        case Exir::OpCode::Neg:
        case Exir::OpCode::Not:
            EmitUnary(out, inst, slots);
            return;

        case Exir::OpCode::Conv:
            EmitConv(out, inst, slots);
            return;

        case Exir::OpCode::Trunc:
        case Exir::OpCode::ZExt:
        case Exir::OpCode::SExt:
        case Exir::OpCode::BitCast:
        case Exir::OpCode::PtrToInt:
        case Exir::OpCode::IntToPtr:
            EmitCast(out, inst, slots);
            return;

        case Exir::OpCode::Call:
        {
            static const char* const argRegs[4] = { "rcx", "rdx", "r8", "r9" };

            if (!inst.Extra || inst.Extra->Text.empty())
            {
                throw std::runtime_error("Backend x64: call sem simbolo de destino.");
            }

            const std::vector<Exir::Value>& args = inst.Extra->Arguments;
            const Exir::Function* callee = FindCallee(m_CurrentModule, inst.Extra->Text);

            // O builder nao coerce literais para o tipo do parametro: um
            // 'Half (u8 84)' chega aqui como inteiro cru e tem de virar f32
            // (ou q32.16) antes de entrar no registo de argumento.
            std::vector<Exir::Value> coerced = args;
            for (std::size_t i = 0; i < coerced.size() && callee != nullptr &&
                                       i < callee->Parameters.size(); ++i)
            {
                const Type& parameterType = callee->Parameters[i].Ty;
                if (!NeedsArgumentConv(coerced[i].Ty, parameterType))
                {
                    continue;
                }

                Exir::Instruction conversion;
                conversion.Op = Exir::OpCode::Conv;
                conversion.ResultTy = parameterType;
                conversion.DstId = kArgConvIdBase + static_cast<std::uint32_t>(i);
                conversion.Operands[0] = coerced[i];

                EmitConv(out, conversion, slots);
                coerced[i] = Exir::Value::MakeTemp(conversion.DstId, parameterType);
            }

            const std::size_t count = coerced.size();
            const std::size_t stackCount = count > 4 ? count - 4 : 0;

            // 32 bytes de shadow space mais um qword por argumento extra,
            // sempre com a pilha alinhada a 16 no 'call'.
            const std::uint32_t reserved = (32 + static_cast<std::uint32_t>(stackCount) * 8 + 15) & ~15u;

            if (stackCount > 0)
            {
                out << "    sub rsp, " << reserved << "\n";
            }

            for (std::size_t i = 0; i < count; ++i)
            {
                const Exir::Value& argument = coerced[i];
                const Type& type = argument.Ty;

                if (IsRegion(type))
                {
                    throw std::runtime_error(
                        "Backend x64: argumento de mais de 64 bits em chamada: '" +
                        type.ToString() + "'."
                    );
                }

                if (i < 4)
                {
                    EmitOperand(out, argument, type, argRegs[i], slots);
                    continue;
                }

                EmitOperand(out, argument, type, "rax", slots);
                out << "    mov qword [rsp + " << (32 + 8 * (i - 4)) << "], rax\n";
            }

            out << "    call " << inst.Extra->Text << "\n";

            if (stackCount > 0)
            {
                out << "    add rsp, " << reserved << "\n";
            }

            if (inst.ResultTy.IsValid() && !inst.ResultTy.IsVoid())
            {
                if (IsRegion(inst.ResultTy))
                {
                    throw std::runtime_error(
                        "Backend x64: retorno de mais de 64 bits: '" +
                        inst.ResultTy.ToString() + "'."
                    );
                }

                out << "    mov qword " << SlotMemory(slots, inst.DstId, 8) << ", rax\n";
                EmitNormalizeSlot(out, inst.DstId, inst.ResultTy, slots);
            }
            return;
        }

        case Exir::OpCode::Memcpy:
        case Exir::OpCode::Memset:
        {
            if (inst.Operands[2].Kind != Exir::ValueKind::Constant)
            {
                throw std::runtime_error("Backend x64: tamanho nao literal em memcpy/memset.");
            }

            // rep movsb/stosb usa rsi e rdi, callee-saved no Win64.
            out << "    push rsi\n";
            out << "    push rdi\n";
            EmitPointer(out, inst.Operands[0], "rdi", slots);

            if (inst.Op == Exir::OpCode::Memcpy)
            {
                EmitPointer(out, inst.Operands[1], "rsi", slots);
            }
            else
            {
                out << "    mov al, " << (inst.Operands[1].Imm & 0xFF) << "\n";
            }

            out << "    mov rcx, " << inst.Operands[2].Imm << "\n";
            out << "    rep " << (inst.Op == Exir::OpCode::Memcpy ? "movsb" : "stosb") << "\n";
            out << "    pop rdi\n";
            out << "    pop rsi\n";
            return;
        }

        case Exir::OpCode::Memcmp:
        {
            if (inst.Operands[2].Kind != Exir::ValueKind::Constant)
            {
                throw std::runtime_error("Backend x64: tamanho nao literal em memcmp.");
            }

            const std::string eq = NewLabel("cmpmem_eq");
            const std::string end = NewLabel("cmpmem_end");

            out << "    push rsi\n";
            out << "    push rdi\n";
            EmitPointer(out, inst.Operands[0], "rsi", slots);
            EmitPointer(out, inst.Operands[1], "rdi", slots);
            out << "    mov ecx, " << inst.Operands[2].Imm << "\n";
            out << "    repe cmpsb\n";
            out << "    je " << eq << "\n";
            // repe para no primeiro par de bytes diferentes: CF diz se o
            // primeiro bloco e o menor.
            out << "    mov eax, 1\n";
            out << "    mov edx, -1\n";
            out << "    cmovc eax, edx\n";
            out << "    jmp " << end << "\n";
            out << eq << ":\n";
            out << "    xor eax, eax\n";
            out << end << ":\n";
            out << "    movsxd rax, eax\n";
            out << "    mov qword " << SlotMemory(slots, inst.DstId, 8) << ", rax\n";
            out << "    pop rdi\n";
            out << "    pop rsi\n";
            return;
        }

        default:
            throw std::runtime_error(
                std::string("Backend x64: opcode sem baixagem (") + OpName(inst.Op) + ")."
            );
    }
}

// ---------------------------------------------------------------------------
// Aritmetica
// ---------------------------------------------------------------------------

void X64Generator::EmitArithmetic(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    const Type& type = inst.ResultTy;
    const Exir::Value& left = inst.Operands[0];
    const Exir::Value& right = inst.Operands[1];

    if (type.IsFloat())
    {
        EmitFloatBinary(out, inst, slots);
        return;
    }

    if (type.IsFixed())
    {
        EmitFixedBinary(out, inst, slots);
        return;
    }

    if (IsRegion(type))
    {
        EmitWideBinary(out, inst, slots);
        EmitWideMask(out, inst.DstId, type, slots);
        return;
    }

    const std::string dstMem = SlotMemory(slots, inst.DstId, 8);

    const char* mnemonic = nullptr;
    switch (inst.Op)
    {
        case Exir::OpCode::Add: mnemonic = "add"; break;
        case Exir::OpCode::Sub: mnemonic = "sub"; break;
        case Exir::OpCode::Mul: mnemonic = "imul"; break;
        case Exir::OpCode::And: mnemonic = "and"; break;
        case Exir::OpCode::Or: mnemonic = "or"; break;
        case Exir::OpCode::Xor: mnemonic = "xor"; break;
        default: break;
    }

    if (mnemonic != nullptr)
    {
        EmitOperand(out, left, type, "rax", slots);
        EmitOperand(out, right, type, "rcx", slots);
        out << "    " << mnemonic << " rax, rcx\n";
        EmitIsolate(out, "rax", type);
        out << "    mov qword " << dstMem << ", rax\n";
        return;
    }

    if (inst.Op == Exir::OpCode::Shl || inst.Op == Exir::OpCode::LShr ||
        inst.Op == Exir::OpCode::AShr)
    {
        const bool arithmetic = inst.Op == Exir::OpCode::AShr && SignedLike(type);
        const char* shift = inst.Op == Exir::OpCode::Shl
            ? "shl"
            : arithmetic ? "sar" : "shr";

        const std::uint64_t bits = type.Representation().Bits;

        if (right.Kind == Exir::ValueKind::Constant)
        {
            const std::uint64_t count = Canonicalize(right.Imm, right.Ty);

            if (count >= bits)
            {
                // Deslocar para fora da largura: zeros, ou o sinal repetido
                // quando a aritmetica e com sinal.
                if (arithmetic && (Canonicalize(left.Imm, type) & (1ull << (bits - 1))))
                {
                    out << "    mov rax, -1\n";
                }
                else
                {
                    out << "    xor eax, eax\n";
                }

                out << "    mov qword " << dstMem << ", rax\n";
                return;
            }

            EmitOperand(out, left, type, "rax", slots);
            out << "    " << shift << " rax, " << count << "\n";
            EmitIsolate(out, "rax", type);
            out << "    mov qword " << dstMem << ", rax\n";
            return;
        }

        EmitOperand(out, left, type, "rax", slots);
        EmitOperand(out, right, type, "rcx", slots);
        out << "    " << shift << " rax, cl\n";
        EmitIsolate(out, "rax", type);
        out << "    mov qword " << dstMem << ", rax\n";
        return;
    }

    // UDiv/SDiv/URem/SRem escalares. O padrao canonico do slot ja estende o
    // valor a 64 bits, pelo que a divisao de 64 por 64 e sempre correcta.
    const bool isSigned = inst.Op == Exir::OpCode::SDiv || inst.Op == Exir::OpCode::SRem;
    const bool wantsRemainder = inst.Op == Exir::OpCode::URem || inst.Op == Exir::OpCode::SRem;

    EmitOperand(out, left, type, "rax", slots);
    EmitOperand(out, right, type, "rcx", slots);

    out << (isSigned ? "    cqo\n" : "    xor edx, edx\n");
    out << (isSigned ? "    idiv rcx\n" : "    div rcx\n");

    if (wantsRemainder)
    {
        out << "    mov rax, rdx\n";
    }

    EmitIsolate(out, "rax", type);
    out << "    mov qword " << dstMem << ", rax\n";
}

void X64Generator::EmitWideBinary(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    const Type& type = inst.ResultTy;
    const std::uint32_t bytes = StorageBytes(type);
    const std::string dstMem = SlotMemory(slots, inst.DstId, SlotBytes(type));

    if (inst.Op == Exir::OpCode::Shl || inst.Op == Exir::OpCode::LShr ||
        inst.Op == Exir::OpCode::AShr)
    {
        EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
        out << "    lea rcx, " << dstMem << "\n";
        EmitShiftCount(out, inst.Operands[1], bytes, slots);
        out << "    mov r9d, " << bytes << "\n";
        out << "    call "
            << (inst.Op == Exir::OpCode::Shl
                    ? "__emux_wshl"
                    : inst.Op == Exir::OpCode::LShr ? "__emux_wshr" : "__emux_wsar")
            << "\n";
        return;
    }

    if (inst.Op == Exir::OpCode::SDiv || inst.Op == Exir::OpCode::UDiv)
    {
        EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
        EmitOperandAddress(out, inst.Operands[1], slots, "r8", 1);
        out << "    lea rcx, " << dstMem << "\n";
        out << "    mov r9d, " << bytes << "\n";
        out << "    call "
            << (inst.Op == Exir::OpCode::SDiv ? "__emux_wsdiv" : "__emux_wudiv") << "\n";
        return;
    }

    if (inst.Op == Exir::OpCode::SRem)
    {
        EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
        EmitOperandAddress(out, inst.Operands[1], slots, "r8", 1);
        out << "    lea rcx, " << dstMem << "\n";
        out << "    mov r9d, " << bytes << "\n";
        out << "    call __emux_wsrem\n";
        return;
    }

    if (inst.Op == Exir::OpCode::URem)
    {
        // __emux_wudivmod escreve o quociente e depois o resto; usar o mesmo
        // destino nos dois e seguro porque o resto e copiado por ultimo.
        EmitOperandAddress(out, inst.Operands[0], slots, "r8", 0);
        EmitOperandAddress(out, inst.Operands[1], slots, "r9", 1);
        out << "    lea rcx, " << dstMem << "\n";
        out << "    mov rdx, rcx\n";
        out << "    mov r10d, " << bytes << "\n";
        out << "    call __emux_wudivmod\n";
        return;
    }

    const char* helper = nullptr;
    switch (inst.Op)
    {
        case Exir::OpCode::Add: helper = "__emux_wadd"; break;
        case Exir::OpCode::Sub: helper = "__emux_wsub"; break;
        case Exir::OpCode::Mul: helper = "__emux_wmul"; break;
        case Exir::OpCode::And: helper = "__emux_wand"; break;
        case Exir::OpCode::Or: helper = "__emux_wor"; break;
        case Exir::OpCode::Xor: helper = "__emux_wxor"; break;
        default: break;
    }

    if (helper == nullptr)
    {
        throw std::runtime_error(
            std::string("Backend x64: operacao larga nao suportada (") + OpName(inst.Op) + ")."
        );
    }

    EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
    EmitOperandAddress(out, inst.Operands[1], slots, "r8", 1);
    out << "    lea rcx, " << dstMem << "\n";
    out << "    mov r9d, " << bytes << "\n";
    out << "    call " << helper << "\n";
}

void X64Generator::EmitShiftCount(
    std::ostringstream& out,
    const Exir::Value& count,
    std::uint32_t bytes,
    StackSlots& slots
)
{
    const std::string huge = NewLabel("wcnt_huge");
    const std::string done = NewLabel("wcnt_done");
    const std::uint64_t bits = static_cast<std::uint64_t>(bytes) * 8;

    if (IsRegion(count.Ty))
    {
        // A contagem e um valor largo: so os 32 bits baixos contam, qualquer
        // byte alto nao nulo excede a largura total.
        EmitOperandAddress(out, count, slots, "r11", 0);

        const std::string scan = NewLabel("wcnt");
        out << "    mov r8d, [r11]\n";
        out << "    mov r10d, 4\n";
        out << scan << ":\n";
        out << "    cmp r10d, " << StorageBytes(count.Ty) << "\n";
        out << "    jae " << done << "\n";
        out << "    cmp byte [r11+r10], 0\n";
        out << "    jne " << huge << "\n";
        out << "    inc r10d\n";
        out << "    jmp " << scan << "\n";
    }
    else
    {
        EmitOperand(out, count, count.Ty, "r8", slots);
        out << "    cmp r8, " << bits << "\n";
        out << "    jb " << done << "\n";
    }

    out << huge << ":\n";
    out << "    mov r8d, -1\n";
    out << done << ":\n";
}

void X64Generator::EmitFloatBinary(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    const Type& type = inst.ResultTy;
    const Type repr = type.Representation();

    const char* helper = nullptr;
    switch (inst.Op)
    {
        case Exir::OpCode::Add: helper = "__emux_f_add"; break;
        case Exir::OpCode::Sub: helper = "__emux_f_sub"; break;
        case Exir::OpCode::Mul: helper = "__emux_f_mul"; break;
        case Exir::OpCode::SDiv:
        case Exir::OpCode::UDiv: helper = "__emux_f_div"; break;
        default: break;
    }

    if (helper == nullptr)
    {
        throw std::runtime_error(
            "Backend x64: operacao binaria em float '" + repr.ToString() + "'."
        );
    }

    EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
    EmitOperandAddress(out, inst.Operands[1], slots, "r8", 1);
    out << "    lea rcx, " << SlotMemory(slots, inst.DstId, SlotBytes(type)) << "\n";
    out << "    mov r9d, " << FloatSpec(repr) << "\n";

    AddExtern(helper);
    out << "    call " << helper << "\n";

    EmitNormalizeSlot(out, inst.DstId, type, slots);
}

void X64Generator::EmitFixedBinary(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    const Type& type = inst.ResultTy;
    const Type repr = type.Representation();
    const std::uint32_t bytes = StorageBytes(type);

    switch (inst.Op)
    {
        case Exir::OpCode::Add:
        case Exir::OpCode::Sub:
        {
            const bool subtract = inst.Op == Exir::OpCode::Sub;

            if (IsRegion(type))
            {
                EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
                EmitOperandAddress(out, inst.Operands[1], slots, "r8", 1);
                out << "    lea rcx, " << SlotMemory(slots, inst.DstId, SlotBytes(type)) << "\n";
                out << "    mov r9d, " << bytes << "\n";
                out << "    call " << (subtract ? "__emux_wsub" : "__emux_wadd") << "\n";
                EmitWideMask(out, inst.DstId, type, slots);
                return;
            }

            EmitOperand(out, inst.Operands[0], type, "rax", slots);
            EmitOperand(out, inst.Operands[1], type, "rcx", slots);
            out << "    " << (subtract ? "sub" : "add") << " rax, rcx\n";
            EmitIsolate(out, "rax", type);
            out << "    mov qword " << SlotMemory(slots, inst.DstId, 8) << ", rax\n";
            return;
        }

        case Exir::OpCode::Mul:
        case Exir::OpCode::SDiv:
        case Exir::OpCode::UDiv:
        {
            const bool divide = inst.Op != Exir::OpCode::Mul;
            const char* helper = divide ? "__emux_q_div" : "__emux_q_mul";

            EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
            EmitOperandAddress(out, inst.Operands[1], slots, "r8", 1);
            out << "    lea rcx, " << SlotMemory(slots, inst.DstId, SlotBytes(type)) << "\n";

            const std::uint64_t packed =
                static_cast<std::uint64_t>(bytes) | (static_cast<std::uint64_t>(repr.Scale) << 32);

            out << "    mov r9, " << packed << "\n";
            AddExtern(helper);
            out << "    call " << helper << "\n";

            if (IsRegion(type))
            {
                EmitWideMask(out, inst.DstId, type, slots);
            }
            else
            {
                EmitNormalizeSlot(out, inst.DstId, type, slots);
            }
            return;
        }

        case Exir::OpCode::SRem:
        case Exir::OpCode::URem:
        {
            // Os dois lados partilham a escala, pelo que o resto cru dos bytes
            // escalados ja e o resto do numero.
            EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
            EmitOperandAddress(out, inst.Operands[1], slots, "r8", 1);
            out << "    lea rcx, " << SlotMemory(slots, inst.DstId, SlotBytes(type)) << "\n";
            out << "    mov r9d, " << bytes << "\n";
            out << "    call __emux_wsrem\n";

            if (IsRegion(type))
            {
                EmitWideMask(out, inst.DstId, type, slots);
            }
            else
            {
                EmitNormalizeSlot(out, inst.DstId, type, slots);
            }
            return;
        }

        default:
            throw std::runtime_error(
                std::string("Backend x64: operacao binaria em ponto fixo '") +
                repr.ToString() + "' (" + OpName(inst.Op) + ")."
            );
    }
}

// ---------------------------------------------------------------------------
// Comparacoes
// ---------------------------------------------------------------------------

void X64Generator::EmitCompare(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    Type valueType = inst.Operands[0].Ty;
    if (!valueType.IsValid() || valueType.IsVoid())
    {
        valueType = inst.Operands[1].Ty;
    }

    if (!valueType.IsValid() || valueType.IsVoid())
    {
        valueType = inst.ResultTy;
    }

    // Comparar dois escalares de largura diferente e trabalho do builder: o
    // resultado do tipo mais largo decide sinal e numero de bytes.
    if (inst.Operands[1].Ty.IsValid() &&
        inst.Operands[1].Ty.Representation().GetByteSize() > valueType.Representation().GetByteSize())
    {
        valueType = inst.Operands[1].Ty.Representation();
    }

    const std::string dstMem = SlotMemory(slots, inst.DstId, 8);

    if (valueType.IsFloat())
    {
        throw std::runtime_error(
            "Backend x64: comparacao de float '" + valueType.Representation().ToString() +
            "'; falta a rotina __emux_f_cmp no runtime."
        );
    }

    if (IsRegion(valueType))
    {
        EmitWideCompare(out, inst, slots);
        return;
    }

    EmitOperand(out, inst.Operands[0], valueType, "rax", slots);
    EmitOperand(out, inst.Operands[1], valueType, "rcx", slots);

    const bool isSigned = SignedLike(valueType);
    out << "    cmp rax, rcx\n";
    out << "    " << ComparisonSetcc(inst.Op, isSigned) << " al\n";
    out << "    movzx eax, al\n";
    out << "    mov qword " << dstMem << ", rax\n";
}

void X64Generator::EmitWideCompare(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    Type valueType = inst.Operands[0].Ty.Representation();
    const std::uint32_t bytes = StorageBytes(valueType);

    EmitOperandAddress(out, inst.Operands[0], slots, "rcx", 0);
    EmitOperandAddress(out, inst.Operands[1], slots, "rdx", 1);
    out << "    mov r8d, " << bytes << "\n";
    out << "    mov r9d, " << (SignedLike(valueType) ? 1 : 0) << "\n";
    out << "    call __emux_wcmp\n";

    // A rotina devolve -1, 0 ou +1: a decisao e sempre a variante com sinal.
    out << "    cmp eax, 0\n";
    out << "    " << ComparisonSetcc(inst.Op, true) << " al\n";
    out << "    movzx eax, al\n";
    out << "    mov qword " << SlotMemory(slots, inst.DstId, 8) << ", rax\n";
}

// ---------------------------------------------------------------------------
// Unarios
// ---------------------------------------------------------------------------

void X64Generator::EmitUnary(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    const Type& type = inst.ResultTy;
    const Type repr = type.Representation();

    if (type.IsFloat())
    {
        if (inst.Op != Exir::OpCode::Neg)
        {
            throw std::runtime_error(
                "Backend x64: operacao unaria em float '" + repr.ToString() + "'."
            );
        }

        // Negar um float e inverter o bit de sinal; o resto do padrao nao se
        // toca, nem o isolamento de largura.
        if (IsRegion(type))
        {
            const std::uint32_t bytes = StorageBytes(type);
            EmitWideCopy(out, inst.Operands[0], inst.DstId, bytes, slots, false);
            out << "    xor byte " << AddOffset(SlotMemory(slots, inst.DstId, SlotBytes(type)), bytes - 1)
                << ", " << (1u << ((repr.Bits - 1) % 8)) << "\n";
            return;
        }

        EmitOperand(out, inst.Operands[0], type, "rax", slots);
        out << "    xor rax, " << Hex64(1ull << (repr.Bits - 1)) << "\n";
        out << "    mov qword " << SlotMemory(slots, inst.DstId, 8) << ", rax\n";
        return;
    }

    if (IsRegion(type))
    {
        EmitWideUnary(out, inst, slots);
        EmitWideMask(out, inst.DstId, type, slots);
        return;
    }

    EmitOperand(out, inst.Operands[0], type, "rax", slots);

    if (inst.Op == Exir::OpCode::Neg)
    {
        out << "    neg rax\n";
    }
    else if (inst.Op == Exir::OpCode::Not)
    {
        out << "    not rax\n";
    }
    else
    {
        throw std::runtime_error(
            std::string("Backend x64: operacao unaria (") + OpName(inst.Op) + ")."
        );
    }

    EmitIsolate(out, "rax", type);
    out << "    mov qword " << SlotMemory(slots, inst.DstId, 8) << ", rax\n";
}

void X64Generator::EmitWideUnary(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    const std::uint32_t bytes = StorageBytes(inst.ResultTy);

    const char* helper = nullptr;
    switch (inst.Op)
    {
        case Exir::OpCode::Neg: helper = "__emux_wneg"; break;
        case Exir::OpCode::Not: helper = "__emux_wnot"; break;
        default: break;
    }

    if (helper == nullptr)
    {
        throw std::runtime_error(
            std::string("Backend x64: operacao unaria larga (") + OpName(inst.Op) + ")."
        );
    }

    EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
    out << "    lea rcx, " << SlotMemory(slots, inst.DstId, SlotBytes(inst.ResultTy)) << "\n";
    out << "    mov r8d, " << bytes << "\n";
    out << "    call " << helper << "\n";
}

// ---------------------------------------------------------------------------
// Conversoes e casts
// ---------------------------------------------------------------------------

void X64Generator::EmitConv(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    const Type dst = inst.ResultTy;
    const Type src = inst.Operands[0].Ty.IsValid() ? inst.Operands[0].Ty : dst;

    const Type dstRepr = dst.Representation();
    const Type srcRepr = src.Representation();

    if (!dstRepr.IsFloat() && !dstRepr.IsFixed() &&
        !srcRepr.IsFloat() && !srcRepr.IsFixed())
    {
        // Inteiro <-> inteiro (ou ponteiro): muda a largura, nao o padrao.
        EmitCast(out, inst, slots);
        return;
    }

    const std::uint32_t dstBytes = StorageBytes(dstRepr);
    const std::uint32_t srcBytes = StorageBytes(srcRepr);
    const std::string dstMem = SlotMemory(slots, inst.DstId, SlotBytes(dst));

    if (dstRepr.IsFloat())
    {
        const char* helper = "__emux_f_from_int";

        if (srcRepr.IsFloat())
        {
            helper = "__emux_f_convert";
        }
        else if (srcRepr.IsFixed())
        {
            helper = "__emux_f_from_fixed";
        }

        EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
        out << "    lea rcx, " << dstMem << "\n";

        if (srcRepr.IsFloat())
        {
            out << "    mov r8d, " << FloatSpec(dstRepr) << "\n";
            out << "    mov r9d, " << FloatSpec(srcRepr) << "\n";
        }
        else if (srcRepr.IsFixed())
        {
            const std::uint64_t packed =
                static_cast<std::uint64_t>(srcBytes) | (static_cast<std::uint64_t>(srcRepr.Scale) << 16);

            out << "    mov r8, " << packed << "\n";
            out << "    mov r9d, " << FloatSpec(dstRepr) << "\n";
        }
        else
        {
            const std::uint64_t signedFlag = srcRepr.IsSigned() ? 1 : 0;
            const std::uint64_t packed =
                static_cast<std::uint64_t>(srcBytes) | (signedFlag << 16);

            out << "    mov r8, " << packed << "\n";
            out << "    mov r9d, " << FloatSpec(dstRepr) << "\n";
        }

        AddExtern(helper);
        out << "    call " << helper << "\n";

        if (IsRegion(dst))
        {
            EmitWideMask(out, inst.DstId, dst, slots);
        }
        else
        {
            EmitNormalizeSlot(out, inst.DstId, dst, slots);
        }
        return;
    }

    if (dstRepr.IsFixed())
    {
        EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
        out << "    lea rcx, " << dstMem << "\n";

        if (srcRepr.IsFloat())
        {
            const std::uint64_t packed =
                static_cast<std::uint64_t>(dstBytes) | (static_cast<std::uint64_t>(dstRepr.Scale) << 16);

            out << "    mov r8, " << packed << "\n";
            out << "    mov r9d, " << FloatSpec(srcRepr) << "\n";
            AddExtern("__emux_f_to_fixed");
            out << "    call __emux_f_to_fixed\n";
        }
        else
        {
            const int sourceScale = srcRepr.IsFixed() ? static_cast<int>(srcRepr.Scale) : 0;
            const int shift = static_cast<int>(dstRepr.Scale) - sourceScale;

            const std::uint64_t packed =
                static_cast<std::uint64_t>(dstBytes) |
                (static_cast<std::uint64_t>(srcBytes) << 16) |
                (SignedLike(srcRepr) ? (1ull << 32) : 0);

            out << "    mov r8, " << packed << "\n";
            out << "    mov r9d, " << shift << "\n";
            AddExtern("__emux_q_rescale");
            out << "    call __emux_q_rescale\n";
        }

        if (IsRegion(dst))
        {
            EmitWideMask(out, inst.DstId, dst, slots);
        }
        else
        {
            EmitNormalizeSlot(out, inst.DstId, dst, slots);
        }
        return;
    }

    // Destino inteiro ou ponteiro.
    if (srcRepr.IsFloat())
    {
        EmitOperandAddress(out, inst.Operands[0], slots, "r8", 0);
        out << "    lea rcx, " << dstMem << "\n";
        out << "    mov edx, " << dstBytes << "\n";
        out << "    mov r9d, " << FloatSpec(srcRepr) << "\n";
        AddExtern("__emux_f_to_int");
        out << "    call __emux_f_to_int\n";
    }
    else
    {
        // Ponto fixo -> inteiro: desfaz a escala.
        EmitOperandAddress(out, inst.Operands[0], slots, "rdx", 0);
        out << "    lea rcx, " << dstMem << "\n";

        const std::uint64_t packed =
            static_cast<std::uint64_t>(dstBytes) |
            (static_cast<std::uint64_t>(srcBytes) << 16) |
            (1ull << 32);

        out << "    mov r8, " << packed << "\n";
        out << "    mov r9d, " << (-static_cast<int>(srcRepr.Scale)) << "\n";
        AddExtern("__emux_q_rescale");
        out << "    call __emux_q_rescale\n";
    }

    if (IsRegion(dst))
    {
        EmitWideMask(out, inst.DstId, dst, slots);
    }
    else
    {
        EmitNormalizeSlot(out, inst.DstId, dst, slots);
    }
}

void X64Generator::EmitCast(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    const Type& dst = inst.ResultTy;
    const Type src = inst.Operands[0].Ty.IsValid() ? inst.Operands[0].Ty : dst;

    if (IsRegion(dst))
    {
        const bool signedFill = inst.Op != Exir::OpCode::ZExt && SignedLike(src);

        EmitWideExtendFill(
            out, inst.Operands[0], src, inst.DstId, StorageBytes(dst), signedFill, slots
        );
        EmitWideMask(out, inst.DstId, dst, slots);
        return;
    }

    // Destino escalonar: ler com o tipo do destino isola ja os bits uteis, e
    // quando a origem e uma regiao larga ficam os seus bytes baixos, que e a
    // truncacao pedida.
    EmitOperand(out, inst.Operands[0], dst, "rax", slots);
    out << "    mov qword " << SlotMemory(slots, inst.DstId, 8) << ", rax\n";
}

// ---------------------------------------------------------------------------
// Regioes largas
// ---------------------------------------------------------------------------

void X64Generator::EmitWideCopy(
    std::ostringstream& out,
    const Exir::Value& source,
    std::uint32_t dstId,
    std::uint32_t bytes,
    StackSlots& slots,
    bool sourceIsPointer
)
{
    if (sourceIsPointer)
    {
        // A origem e o conteudo de um ponteiro: e o proprio valor apontado.
        EmitPointer(out, source, "rdx", slots);
    }
    else
    {
        EmitOperandAddress(out, source, slots, "rdx", 0);
    }

    out << "    lea rcx, " << SlotMemory(slots, dstId, bytes) << "\n";
    out << "    mov r8d, " << bytes << "\n";
    out << "    call __emux_wcopy\n";
}

void X64Generator::EmitWideExtendFill(
    std::ostringstream& out,
    const Exir::Value& source,
    const Type& sourceType,
    std::uint32_t dstId,
    std::uint32_t dstBytes,
    bool signedFill,
    StackSlots& slots
)
{
    const std::string dstMem = SlotMemory(slots, dstId, dstBytes);

    if (IsRegion(sourceType))
    {
        const std::uint32_t srcBytes = StorageBytes(sourceType);
        const std::uint32_t copyBytes = std::min(srcBytes, dstBytes);

        EmitOperandAddress(out, source, slots, "rdx", 0);
        out << "    lea rcx, " << dstMem << "\n";
        out << "    mov r8d, " << copyBytes << "\n";
        out << "    call __emux_wcopy\n";

        if (srcBytes >= dstBytes)
        {
            return;
        }

        // Preencher acima do que foi copiado com o byte de sinal da origem.
        out << "    mov r10, rdx\n";
        out << "    movzx eax, byte [r10+" << (srcBytes - 1) << "]\n";
        out << (signedFill ? "    sar al, 7\n" : "    xor eax, eax\n");
        out << "    lea r11, " << dstMem << "\n";

        const std::string loop = NewLabel("wfill");
        const std::string done = NewLabel("wfilld");

        out << "    mov ecx, " << srcBytes << "\n";
        out << loop << ":\n";
        out << "    cmp ecx, " << dstBytes << "\n";
        out << "    jae " << done << "\n";
        out << "    mov [r11+rcx], al\n";
        out << "    inc ecx\n";
        out << "    jmp " << loop << "\n";
        out << done << ":\n";
        return;
    }

    // Pequeno -> largo: os 8 bytes do padrao canonico cobrem a largura do
    // tipo de origem; o resto e replicado a partir do seu bit de sinal.
    EmitOperand(out, source, sourceType, "rax", slots);

    out << "    lea r11, " << dstMem << "\n";
    out << "    mov [r11], rax\n";
    out << "    mov rdx, rax\n";
    out << (signedFill ? "    sar rdx, 63\n" : "    xor edx, edx\n");

    const std::string loop = NewLabel("wext");
    const std::string done = NewLabel("wextd");

    out << "    mov ecx, 8\n";
    out << loop << ":\n";
    out << "    cmp ecx, " << dstBytes << "\n";
    out << "    jae " << done << "\n";
    out << "    mov [r11+rcx], dl\n";
    out << "    inc ecx\n";
    out << "    jmp " << loop << "\n";
    out << done << ":\n";
}

void X64Generator::EmitWideMask(
    std::ostringstream& out,
    std::uint32_t dstId,
    const Type& type,
    StackSlots& slots
)
{
    const Type repr = type.Representation();
    const std::uint32_t top = repr.Bits % 8;

    if (repr.Bits == 0 || top == 0)
    {
        return;
    }

    const std::uint32_t bytes = StorageBytes(type);
    const std::string mem = AddOffset(SlotMemory(slots, dstId, SlotBytes(type)), bytes - 1);

    const std::uint32_t mask = (1u << top) - 1;
    out << "    and byte " << mem << ", " << mask << "\n";

    if (!SignedLike(repr))
    {
        return;
    }

    const std::string done = NewLabel("wmask");
    out << "    test byte " << mem << ", " << (1u << (top - 1)) << "\n";
    out << "    jz " << done << "\n";
    out << "    or byte " << mem << ", " << (~mask & 0xFFu) << "\n";
    out << done << ":\n";
}

void X64Generator::EmitNormalizeSlot(
    std::ostringstream& out,
    std::uint32_t dstId,
    const Type& type,
    StackSlots& slots
)
{
    if (IsRegion(type))
    {
        return;
    }

    const std::string mem = SlotMemory(slots, dstId, 8);

    out << "    mov rax, qword " << mem << "\n";
    EmitIsolate(out, "rax", type);
    out << "    mov qword " << mem << ", rax\n";
}

// ---------------------------------------------------------------------------
// Terminadores
// ---------------------------------------------------------------------------

void X64Generator::EmitTerminator(
    std::ostringstream& out,
    const Exir::Instruction& inst,
    StackSlots& slots
)
{
    auto epilogue = [&out]() {
        out << "    mov rsp, rbp\n";
        out << "    pop rbp\n";
        out << "    ret\n";
    };

    switch (inst.Op)
    {
        case Exir::OpCode::Br:
            out << "    jmp .bb_" << inst.Operands[0].Id << "\n";
            return;

        case Exir::OpCode::CBr:
        {
            const Exir::Value& cond = inst.Operands[0];

            if (cond.Kind == Exir::ValueKind::Constant)
            {
                if (cond.Imm != 0)
                {
                    out << "    jmp .bb_" << inst.Operands[1].Id << "\n";
                }
                else
                {
                    out << "    jmp .bb_" << inst.Operands[2].Id << "\n";
                }

                return;
            }

            EmitOperand(out, cond, cond.Ty.IsValid() ? cond.Ty : Type::MakeUnsigned(8), "rax", slots);
            out << "    test eax, eax\n";
            out << "    jnz .bb_" << inst.Operands[1].Id << "\n";
            out << "    jmp .bb_" << inst.Operands[2].Id << "\n";
            return;
        }

        case Exir::OpCode::Ret:
        {
            const Exir::Value& value = inst.Operands[0];

            if (value.Kind != Exir::ValueKind::None)
            {
                const Type type = value.Ty.IsValid() ? value.Ty : inst.ResultTy;

                if (IsRegion(type))
                {
                    throw std::runtime_error(
                        "Backend x64: retorno de um valor de mais de 64 bits: '" +
                        type.ToString() + "'."
                    );
                }

                EmitOperand(out, value, type, "rax", slots);
            }

            epilogue();
            return;
        }

        case Exir::OpCode::Unreachable:
            out << "    int3\n";
            return;

        default:
            throw std::runtime_error("Backend x64: terminador nao suportado.");
    }
}

// ---------------------------------------------------------------------------
// Rotinas multi-limb em NASM
//
// Convencoes (Win64: rcx, rdx, r8, r9, r10 para o quinto):
//   wcopy:        rcx=dst, rdx=src, r8d=n
//   add/sub/and/or/xor/mul: rcx=dst, rdx=a, r8=b, r9d=n
//   neg/not/lnot: rcx=dst, rdx=a, r8d=n
//   shl/shr/sar:  rcx=dst, rdx=a, r8d=contagem, r9d=n
//   cmp:          rcx=a, rdx=b, r8d=n, r9d=assinado -> eax = -1/0/+1
//   wudiv/wudivmod: ver abaixo; wsdiv e wsrem as variantes com sinal.
// n e sempre o numero de bytes do tipo (multiplo de 8 ou nao).
// ---------------------------------------------------------------------------

void X64Generator::EmitArchHelpers(std::ostringstream& text)
{
    text << R"ASM(
; __emux_wcmp: rcx=a, rdx=b, r8d=bytes, r9d=assinado -> eax = -1, 0 ou +1.
; Varre do byte mais significativo para baixo: la em cima decide o valor.
__emux_wcmp:
    mov r10d, r8d
    dec r10d
.Lwcmp_loop:
    mov al, [rcx+r10]
    cmp al, [rdx+r10]
    je .Lwcmp_next
    jb .Lwcmp_below
    mov eax, 1
    ret
.Lwcmp_below:
    mov eax, -1
    ret
.Lwcmp_next:
    dec r10d
    jns .Lwcmp_loop
    xor eax, eax
    ret

__emux_wcopy:
    test r8d, r8d
    jz .Lwcopy_done
    xor r9d, r9d
.Lwcopy_loop:
    mov al, [rdx+r9]
    mov [rcx+r9], al
    inc r9d
    cmp r9d, r8d
    jb .Lwcopy_loop
.Lwcopy_done:
    ret

__emux_wadd:
    clc
    xor r10d, r10d
.Lwadd_loop:
    mov al, [rdx+r10]
    adc al, [r8+r10]
    mov [rcx+r10], al
    inc r10d
    dec r9d
    jnz .Lwadd_loop
    ret

__emux_wsub:
    clc
    xor r10d, r10d
.Lwsub_loop:
    mov al, [rdx+r10]
    sbb al, [r8+r10]
    mov [rcx+r10], al
    inc r10d
    dec r9d
    jnz .Lwsub_loop
    ret

__emux_wand:
    xor r10d, r10d
.Lwand_loop:
    mov al, [rdx+r10]
    and al, [r8+r10]
    mov [rcx+r10], al
    inc r10d
    dec r9d
    jnz .Lwand_loop
    ret

__emux_wor:
    xor r10d, r10d
.Lwor_loop:
    mov al, [rdx+r10]
    or al, [r8+r10]
    mov [rcx+r10], al
    inc r10d
    dec r9d
    jnz .Lwor_loop
    ret

__emux_wxor:
    xor r10d, r10d
.Lwxor_loop:
    mov al, [rdx+r10]
    xor al, [r8+r10]
    mov [rcx+r10], al
    inc r10d
    dec r9d
    jnz .Lwxor_loop
    ret

__emux_wneg:
    clc
    xor r10d, r10d
.Lwneg_loop:
    mov al, 0
    sbb al, [rdx+r10]
    mov [rcx+r10], al
    inc r10d
    dec r8d
    jnz .Lwneg_loop
    ret

__emux_wnot:
    xor r10d, r10d
.Lwnot_loop:
    mov al, [rdx+r10]
    not al
    mov [rcx+r10], al
    inc r10d
    dec r8d
    jnz .Lwnot_loop
    ret

__emux_wlnot:
    mov r9d, r8d
    xor eax, eax
    xor r10d, r10d
.Lwlnot_scan:
    or al, [rdx+r10]
    inc r10d
    cmp r10d, r9d
    jb .Lwlnot_scan
    test al, al
    setz al
    movzx eax, al
    xor r10d, r10d
.Lwlnot_fill:
    mov [rcx+r10], al
    inc r10d
    cmp r10d, r9d
    jb .Lwlnot_fill
    ret

__emux_wshl:
    push rbx
    push rsi
    push rdi
    mov rdi, rcx
    mov rsi, rdx
    mov eax, r9d
    shl eax, 3
    cmp r8d, eax
    jae .Lwshl_zeroall
    test r8d, r8d
    jz .Lwshl_copy
    mov r10d, r8d
    shr r10d, 3
    and r8d, 7
    mov bl, r8b
    lea r8d, [r9d-1]
.Lwshl_body:
    mov eax, r8d
    sub eax, r10d
    js .Lwshl_zero
    test eax, eax
    jz .Lwshl_first
    mov dx, [rsi+rax-1]
    mov cl, 8
    sub cl, bl
    shr dx, cl
    mov [rdi+r8], dl
    jmp .Lwshl_next
.Lwshl_first:
    mov cl, bl
    mov dl, [rsi]
    shl dl, cl
    mov [rdi+r8], dl
.Lwshl_next:
    dec r8d
    jns .Lwshl_body
    jmp .Lwshl_done
.Lwshl_zero:
    mov byte [rdi+r8], 0
    dec r8d
    jns .Lwshl_zero
    jmp .Lwshl_done
.Lwshl_zeroall:
    xor r10d, r10d
.Lwshl_zloop:
    mov byte [rdi+r10], 0
    inc r10d
    cmp r10d, r9d
    jb .Lwshl_zloop
    jmp .Lwshl_done
.Lwshl_copy:
    xor r10d, r10d
.Lwshl_cloop:
    mov al, [rsi+r10]
    mov [rdi+r10], al
    inc r10d
    cmp r10d, r9d
    jb .Lwshl_cloop
.Lwshl_done:
    pop rdi
    pop rsi
    pop rbx
    ret

__emux_wshr:
    push rbx
    push rsi
    push rdi
    mov rdi, rcx
    mov rsi, rdx
    mov eax, r9d
    shl eax, 3
    cmp r8d, eax
    jae .Lwshr_zeroall
    test r8d, r8d
    jz .Lwshr_copy
    mov r10d, r8d
    shr r10d, 3
    and r8d, 7
    mov bl, r8b
    lea r8d, [r9d-1]
.Lwshr_body:
    mov eax, r8d
    add eax, r10d
    cmp eax, r9d
    jae .Lwshr_zero
    mov edx, eax
    inc edx
    cmp edx, r9d
    jae .Lwshr_single
    mov dx, [rsi+rax]
    mov cl, bl
    shr dx, cl
    mov [rdi+r8], dl
    jmp .Lwshr_next
.Lwshr_single:
    mov cl, bl
    mov dl, [rsi+rax]
    shr dl, cl
    mov [rdi+r8], dl
.Lwshr_next:
    dec r8d
    jns .Lwshr_body
    jmp .Lwshr_done
.Lwshr_zero:
    mov byte [rdi+r8], 0
    jmp .Lwshr_next
.Lwshr_zeroall:
    xor r10d, r10d
.Lwshr_zloop:
    mov byte [rdi+r10], 0
    inc r10d
    cmp r10d, r9d
    jb .Lwshr_zloop
    jmp .Lwshr_done
.Lwshr_copy:
    xor r10d, r10d
.Lwshr_cloop:
    mov al, [rsi+r10]
    mov [rdi+r10], al
    inc r10d
    cmp r10d, r9d
    jb .Lwshr_cloop
.Lwshr_done:
    pop rdi
    pop rsi
    pop rbx
    ret

__emux_wsar:
    push rbx
    push rsi
    push rdi
    mov rdi, rcx
    mov rsi, rdx
    mov eax, r9d
    shl eax, 3
    cmp r8d, eax
    jae .Lwsar_signall
    test r8d, r8d
    jz .Lwsar_copy
    mov r10d, r8d
    shr r10d, 3
    and r8d, 7
    mov bl, r8b
    lea r8d, [r9d-1]
    mov al, [rsi+r8]
    sar al, 7
    mov r11b, al
.Lwsar_body:
    mov eax, r8d
    add eax, r10d
    cmp eax, r9d
    jae .Lwsar_zero
    mov edx, eax
    inc edx
    cmp edx, r9d
    jae .Lwsar_top
    mov dx, [rsi+rax]
    mov cl, bl
    shr dx, cl
    mov [rdi+r8], dl
    jmp .Lwsar_next
.Lwsar_top:
    mov cl, bl
    mov dl, [rsi+rax]
    shr dl, cl
    mov cl, 8
    sub cl, bl
    mov al, r11b
    shl al, cl
    or dl, al
    mov [rdi+r8], dl
.Lwsar_next:
    dec r8d
    jns .Lwsar_body
    jmp .Lwsar_done
.Lwsar_zero:
    mov [rdi+r8], r11b
    jmp .Lwsar_next
.Lwsar_signall:
    xor r10d, r10d
    mov al, [rsi+r9-1]
    sar al, 7
    mov r11b, al
.Lwsar_sloop:
    mov [rdi+r10], r11b
    inc r10d
    cmp r10d, r9d
    jb .Lwsar_sloop
    jmp .Lwsar_done
.Lwsar_copy:
    xor r10d, r10d
.Lwsar_cloop:
    mov al, [rsi+r10]
    mov [rdi+r10], al
    inc r10d
    cmp r10d, r9d
    jb .Lwsar_cloop
.Lwsar_done:
    pop rdi
    pop rsi
    pop rbx
    ret

__emux_wmul:
    push rbx
    push rsi
    push rdi
    mov rdi, rcx
    mov rsi, rdx
    mov rbx, r8
    xor r10d, r10d
.Lwmul_clear:
    mov byte [rdi+r10], 0
    inc r10d
    cmp r10d, r9d
    jb .Lwmul_clear
    xor r8d, r8d
.Lwmul_outer:
    xor r11d, r11d
    xor r10d, r10d
.Lwmul_inner:
    lea edx, [r8d+r10d]
    cmp edx, r9d
    jae .Lwmul_nexti
    movzx ecx, byte [rbx+r10]
    movzx eax, byte [rsi+r8]
    imul eax, ecx
    movzx ecx, byte [rdi+rdx]
    add eax, ecx
    add eax, r11d
    mov [rdi+rdx], al
    shr eax, 8
    mov r11d, eax
    inc r10d
    jmp .Lwmul_inner
.Lwmul_nexti:
    inc r8d
    cmp r8d, r9d
    jb .Lwmul_outer
    pop rdi
    pop rsi
    pop rbx
    ret

; __emux_wudivmod: rcx=quociente, rdx=resto (0 ou ponteiro),
;                  r8=a, r9=b, r10d=n
__emux_wudivmod:
    push rbx
    push rsi
    push rdi
    mov rbx, rcx
    mov rdi, r8
    mov rsi, r9
    mov r11d, r10d
    mov rax, r11
    add rax, 7
    and rax, -8
    sub rsp, rax
    mov r10, rsp
    xor r8d, r8d
.Lwud_clearr:
    mov byte [r10+r8], 0
    inc r8d
    cmp r8d, r11d
    jb .Lwud_clearr
    xor r8d, r8d
.Lwud_clearq:
    mov byte [rbx+r8], 0
    inc r8d
    cmp r8d, r11d
    jb .Lwud_clearq
    mov r9d, r11d
    shl r9d, 3
    dec r9d
.Lwud_bit:
    clc
    xor r8d, r8d
    mov ecx, r11d
.Lwud_shl:
    rcl byte [r10+r8], 1
    inc r8d
    loop .Lwud_shl
    mov ecx, r9d
    bt [rdi], ecx
    adc byte [r10], 0
    mov r8d, r11d
.Lwud_cmp:
    dec r8d
    mov al, [r10+r8]
    cmp al, [rsi+r8]
    jb .Lwud_next
    ja .Lwud_sub
    test r8d, r8d
    jnz .Lwud_cmp
.Lwud_sub:
    clc
    xor r8d, r8d
    mov ecx, r11d
.Lwud_subloop:
    mov al, [rsi+r8]
    sbb [r10+r8], al
    inc r8d
    loop .Lwud_subloop
    mov ecx, r9d
    bts [rbx], ecx
.Lwud_next:
    dec r9d
    jns .Lwud_bit
    test rdx, rdx
    jz .Lwud_done
    xor r8d, r8d
.Lwud_rem:
    mov al, [r10+r8]
    mov [rdx+r8], al
    inc r8d
    cmp r8d, r11d
    jb .Lwud_rem
.Lwud_done:
    mov rax, r11
    add rax, 7
    and rax, -8
    add rsp, rax
    pop rdi
    pop rsi
    pop rbx
    ret

__emux_wudiv:
    mov r10d, r9d
    mov r9, r8
    mov r8, rdx
    xor edx, edx
    jmp __emux_wudivmod

__emux_wsdiv:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov rbx, rcx
    mov rsi, rdx
    mov rdi, r8
    mov r12d, r9d
    mov rax, r12
    shl rax, 1
    add rax, 7
    and rax, -8
    sub rsp, rax
    mov r15, rax
    mov rax, r12
    dec rax
    movzx r13d, byte [rsi+rax]
    movzx r14d, byte [rdi+rax]
    shr r13d, 7
    shr r14d, 7
    mov rcx, rsp
    mov rdx, rsi
    mov r8d, r12d
    call __emux_wcopy
    test r13d, r13d
    jz .Lwsdiv_absb
    mov rcx, rsp
    mov rdx, rsp
    mov r8d, r12d
    call __emux_wneg
.Lwsdiv_absb:
    mov rcx, rsp
    add rcx, r12
    mov rdx, rdi
    mov r8d, r12d
    call __emux_wcopy
    test r14d, r14d
    jz .Lwsdiv_div
    mov rcx, rsp
    add rcx, r12
    mov rdx, rcx
    mov r8d, r12d
    call __emux_wneg
.Lwsdiv_div:
    mov rcx, rbx
    xor edx, edx
    mov r8, rsp
    mov r9, rsp
    add r9, r12
    mov r10d, r12d
    call __emux_wudivmod
    mov eax, r13d
    xor eax, r14d
    test eax, eax
    jz .Lwsdiv_done
    mov rcx, rbx
    mov rdx, rbx
    mov r8d, r12d
    call __emux_wneg
.Lwsdiv_done:
    add rsp, r15
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; __emux_wsrem: rcx=dst (resto), rdx=a, r8=b, r9d=n
; O resto de inteiros com sinal segue o sinal do dividendo (como em C), por
; isso so o dividendo decide a negacao final.
__emux_wsrem:
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov rbx, rcx
    mov rsi, rdx
    mov rdi, r8
    mov r12d, r9d
    mov rax, r12
    shl rax, 1
    add rax, 7
    and rax, -8
    sub rsp, rax
    mov r15, rax
    mov rax, r12
    dec rax
    movzx r13d, byte [rsi+rax]
    movzx r14d, byte [rdi+rax]
    shr r13d, 7
    shr r14d, 7
    mov rcx, rsp
    mov rdx, rsi
    mov r8d, r12d
    call __emux_wcopy
    test r13d, r13d
    jz .Lwsrem_absb
    mov rcx, rsp
    mov rdx, rsp
    mov r8d, r12d
    call __emux_wneg
.Lwsrem_absb:
    mov rcx, rsp
    add rcx, r12
    mov rdx, rdi
    mov r8d, r12d
    call __emux_wcopy
    test r14d, r14d
    jz .Lwsrem_div
    mov rcx, rsp
    add rcx, r12
    mov rdx, rcx
    mov r8d, r12d
    call __emux_wneg
.Lwsrem_div:
    ; Quociente e resto partilham Dst: o resto e copiado por ultimo.
    mov rcx, rbx
    mov rdx, rbx
    mov r8, rsp
    mov r9, rsp
    add r9, r12
    mov r10d, r12d
    call __emux_wudivmod
    test r13d, r13d
    jz .Lwsrem_done
    mov rcx, rbx
    mov rdx, rbx
    mov r8d, r12d
    call __emux_wneg
.Lwsrem_done:
    add rsp, r15
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

)ASM";
}

} // namespace Emux
