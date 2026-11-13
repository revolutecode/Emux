#include "Emux/Generator/X86Generator.hpp"
#include "Emux/Generator/NasmUtils.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace Emux
{

namespace
{

// Os ids de scratch ficam fora do dominio dos temporarios da Exir: as operacoes
// de par precisam de memoria para literais largos e para os produtos parciais
// da multiplicacao de 64 bits.
constexpr std::uint32_t kScratchLeft = 0xFFFFFFFA;
constexpr std::uint32_t kScratchRight = 0xFFFFFFFB;
constexpr std::uint32_t kScratchCarry = 0xFFFFFFFC;
constexpr std::uint32_t kScratchLow = 0xFFFFFFFD;

// O i386 nao tem 'divq' nem multiplicacao 64x64: divisao e resto de 64 bits
// correm em rotinas de assembleio proprio, com o quociente gravado por
// referencia e o resto em __emux_rem64.
const char* const kUdiv64Helper = R"NASM(
__emux_udiv64:
    push ebp
    mov ebp, esp
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi
    sub esp, 32

    mov eax, [ebp+12]
    mov [ebp-8], eax
    mov eax, [ebp+16]
    mov [ebp-4], eax
    mov eax, [ebp+20]
    mov [ebp-16], eax
    mov eax, [ebp+24]
    mov [ebp-12], eax
    mov dword [ebp-24], 0
    mov dword [ebp-20], 0
    mov dword [ebp-32], 0
    mov dword [ebp-28], 0

    mov eax, [ebp-16]
    or eax, [ebp-12]
    jz .store_zero

    mov ecx, 64
.loop:
    mov eax, [ebp-32]
    mov edx, [ebp-28]
    shld edx, eax, 1
    shl eax, 1
    mov [ebp-32], eax
    mov [ebp-28], edx

    mov eax, [ebp-8]
    mov edx, [ebp-4]
    shl eax, 1
    rcl edx, 1
    mov [ebp-8], eax
    mov [ebp-4], edx
    adc dword [ebp-32], 0

    mov eax, [ebp-32]
    mov edx, [ebp-28]
    sub eax, [ebp-16]
    sbb edx, [ebp-12]
    jc .no_sub

    mov [ebp-32], eax
    mov [ebp-28], edx

    mov eax, [ebp-24]
    mov edx, [ebp-20]
    shld edx, eax, 1
    shl eax, 1
    or eax, 1
    mov [ebp-24], eax
    mov [ebp-20], edx
    jmp .next

.no_sub:
    mov eax, [ebp-24]
    mov edx, [ebp-20]
    shld edx, eax, 1
    shl eax, 1
    mov [ebp-24], eax
    mov [ebp-20], edx

.next:
    dec ecx
    jnz .loop

    mov edi, [ebp+8]
    mov eax, [ebp-24]
    mov edx, [ebp-20]
    mov [edi], eax
    mov [edi+4], edx

    mov eax, [ebp-32]
    mov edx, [ebp-28]
    mov [__emux_rem64], eax
    mov [__emux_rem64+4], edx
    jmp .done

.store_zero:
    mov edi, [ebp+8]
    mov dword [edi], 0
    mov dword [edi+4], 0
    mov dword [__emux_rem64], 0
    mov dword [__emux_rem64+4], 0

.done:
    add esp, 32
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    pop ebp
    ret
)NASM";

const char* const kIdiv64Helper = R"NASM(
__emux_idiv64:
    push ebp
    mov ebp, esp
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi
    sub esp, 32

    mov eax, [ebp+16]
    mov edx, [ebp+24]
    xor eax, edx
    sar eax, 31
    mov [ebp-4], eax

    ; O sinal do resto segue o do dividendo.
    mov eax, [ebp+16]
    sar eax, 31
    mov [ebp-8], eax

    mov eax, [ebp+12]
    mov edx, [ebp+16]
    test edx, edx
    jns .a_pos
    not edx
    neg eax
    sbb edx, -1
.a_pos:
    mov [ebp-16], eax
    mov [ebp-12], edx

    mov eax, [ebp+20]
    mov edx, [ebp+24]
    test edx, edx
    jns .b_pos
    not edx
    neg eax
    sbb edx, -1
.b_pos:
    mov [ebp-24], eax
    mov [ebp-20], edx

    push dword [ebp-20]
    push dword [ebp-24]
    push dword [ebp-12]
    push dword [ebp-16]
    lea eax, [ebp-32]
    push eax
    call __emux_udiv64
    add esp, 20

    mov eax, [ebp-32]
    mov edx, [ebp-28]
    mov ecx, [ebp-4]
    test ecx, ecx
    jz .store

    not edx
    neg eax
    sbb edx, -1

.store:
    mov edi, [ebp+8]
    mov [edi], eax
    mov [edi+4], edx

    mov eax, [__emux_rem64]
    mov edx, [__emux_rem64+4]
    mov ecx, [ebp-8]
    test ecx, ecx
    jz .rem_done
    not edx
    neg eax
    sbb edx, -1

.rem_done:
    mov [__emux_rem64], eax
    mov [__emux_rem64+4], edx

    add esp, 32
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    pop ebp
    ret
)NASM";

// Ponto fixo partilha o comportamento de inteiro com sinal; um ponteiro e uma
// magnitude, por isso trata-se como sem sinal.
bool SignedLike(const Type& type)
{
    const Type repr = type.Representation();
    return repr.IsSigned() || repr.IsFixed();
}

std::uint32_t WidthOf(const Type& type)
{
    return type.IsValid() && type.Bits > 0 ? type.Bits : 32;
}

std::uint32_t BytesOf(const Type& type)
{
    if (!type.IsValid())
    {
        return 4;
    }

    const std::uint32_t bytes = type.GetByteSize();
    return bytes == 0 ? 4 : bytes;
}

// Bytes que um argumento ou resultado ocupa na pilha: o i386 nunca empilha
// menos de um dword.
std::uint32_t SlotBytes(const Type& type)
{
    return BytesOf(type) <= 4 ? 4 : 8;
}

bool IsWide(const Type& type)
{
    return WidthOf(type) > 32;
}

bool IsRegisterWidth(std::uint32_t bits)
{
    return bits == 8 || bits == 16 || bits == 32;
}

std::uint64_t LowMask(std::uint32_t bits)
{
    if (bits == 0 || bits >= 64)
    {
        return ~std::uint64_t(0);
    }

    return (std::uint64_t(1) << bits) - 1;
}

// Registo de 32 bits partido para a largura pedida; sil e dil so existem em
// x86_64, por isso as larguras sub-byte ficam limitadas aos registos classic os.
std::string RegName(std::string_view reg32, std::uint32_t bits)
{
    if (bits >= 32)
    {
        return std::string(reg32);
    }

    struct Register
    {
        const char* r32;
        const char* r16;
        const char* r8;
    };

    static const Register registers[] = {
        { "eax", "ax", "al" },
        { "ebx", "bx", "bl" },
        { "ecx", "cx", "cl" },
        { "edx", "dx", "dl" },
        { "esi", "si", "" },
        { "edi", "di", "" }
    };

    for (const Register& reg : registers)
    {
        if (reg32 == reg.r32)
        {
            if (bits <= 8 && reg.r8[0] != '\0')
            {
                return reg.r8;
            }
            if (bits <= 16)
            {
                return reg.r16;
            }
            return reg.r32;
        }
    }

    return std::string(reg32);
}

// O outro registo livre para scratch das cargas e guardas byte a byte.
std::string ScratchOf(const std::string& reg)
{
    return reg == "edx" ? "ecx" : "edx";
}

std::string OffsetText(int offset)
{
    return offset >= 0 ? "+" + std::to_string(offset) : "-" + std::to_string(-offset);
}

// Desloca um operando de memoria ja escrito: "[ecx]" + 4 -> "[ecx+4]". Em bits
// 32 nao ha 'default rel', entao "[simbolo+4]" e o endereco absoluto + 4.
std::string AddMemOffset(const std::string& mem, int offset)
{
    if (offset == 0 || mem.empty() || mem.back() != ']')
    {
        return mem;
    }

    const std::string body = mem.substr(0, mem.size() - 1);
    const std::size_t bracket = body.rfind('[');
    const std::string inner = bracket == std::string::npos ? body : body.substr(bracket + 1);

    // "[ebp-8]" com +4 vira "[ebp-4]": o deslocamento entra no calculo do frame.
    if (inner.rfind("ebp", 0) == 0)
    {
        int current = 0;
        std::size_t at = 3;
        if (at < inner.size())
        {
            const bool negative = inner[at] == '-';
            if (negative || inner[at] == '+') ++at;
            int digits = 0;
            while (at < inner.size() && inner[at] >= '0' && inner[at] <= '9')
            {
                digits = digits * 10 + (inner[at] - '0');
                ++at;
            }
            current = negative ? -digits : digits;
        }

        return "[ebp" + OffsetText(current + offset) + "]";
    }

    return body + (offset > 0 ? "+" : "-") + std::to_string(offset < 0 ? -offset : offset) + "]";
}

// Carga por word/byte para uma largura que nao e registo: ler um dword de um
// 'resb 1' ou de um bloco de 3 bytes pisgar o simbolo vizinho.
void EmitExactLoad(std::ostringstream& out, const std::string& mem, std::uint32_t bytes,
                   const std::string& reg)
{
    const std::string scratch = ScratchOf(reg);

    out << "    xor " << reg << ", " << reg << '\n';

    std::uint32_t offset = 0;
    while (bytes > 0)
    {
        const std::uint32_t chunk = bytes >= 2 ? 2 : 1;
        out << "    movzx " << scratch << ", " << (chunk == 2 ? "word " : "byte ")
            << AddMemOffset(mem, static_cast<int>(offset)) << '\n';
        if (offset > 0)
        {
            out << "    shl " << scratch << ", " << (offset * 8) << '\n';
        }
        out << "    or " << reg << ", " << scratch << '\n';

        offset += chunk;
        bytes -= chunk;
    }
}

// Guarda byte a byte os bytes baixos de reg, sem ultrapassar o objeto.
void EmitExactStore(std::ostringstream& out, const std::string& mem, std::uint32_t bytes,
                    const std::string& valueReg)
{
    const std::string scratch = ScratchOf(valueReg);

    out << "    mov " << scratch << ", " << valueReg << '\n';

    std::uint32_t offset = 0;
    while (bytes > 0)
    {
        const std::uint32_t chunk = bytes >= 2 ? 2 : 1;
        out << "    mov " << (chunk == 2 ? "word " : "byte ")
            << AddMemOffset(mem, static_cast<int>(offset)) << ", "
            << RegName(scratch, chunk * 8) << '\n';

        offset += chunk;
        bytes -= chunk;

        if (bytes > 0)
        {
            out << "    shr " << scratch << ", " << (offset * 8) << '\n';
        }
    }
}

// Guardas de 5 a 7 bytes de um par edx:eax: um i40 escrito num simbolo de .data
// com apenas os bytes do tipo nao pode ocupar os 8 do registador.
void EmitWideStoreBytes(std::ostringstream& out, const std::string& mem, std::uint32_t bytes)
{
    out << "    mov dword " << mem << ", eax\n";
    out << "    mov ecx, edx\n";

    if (bytes >= 5)
    {
        out << "    mov byte " << AddMemOffset(mem, 4) << ", cl\n";
    }
    if (bytes >= 6)
    {
        out << "    mov byte " << AddMemOffset(mem, 5) << ", ch\n";
    }
    if (bytes >= 7)
    {
        out << "    shr ecx, 16\n";
        out << "    mov byte " << AddMemOffset(mem, 6) << ", cl\n";
    }
}

const char* SetccFor(Exir::OpCode op)
{
    switch (op)
    {
        case Exir::OpCode::CmpEq: return "sete";
        case Exir::OpCode::CmpNe: return "setne";
        case Exir::OpCode::CmpUlt: return "setb";
        case Exir::OpCode::CmpUle: return "setbe";
        case Exir::OpCode::CmpUgt: return "seta";
        case Exir::OpCode::CmpUge: return "setae";
        case Exir::OpCode::CmpSlt: return "setl";
        case Exir::OpCode::CmpSle: return "setle";
        case Exir::OpCode::CmpSgt: return "setg";
        case Exir::OpCode::CmpSge: return "setge";
        default: return "sete";
    }
}

// Halves altos iguais num par edx:eax: o sinal ja se decidiu la em cima, por
// isso os halves baixos comparam-se sempre como magnitudes.
const char* UnsignedSetccFor(Exir::OpCode op)
{
    switch (op)
    {
        case Exir::OpCode::CmpSlt: return "setb";
        case Exir::OpCode::CmpSle: return "setbe";
        case Exir::OpCode::CmpSgt: return "seta";
        case Exir::OpCode::CmpSge: return "setae";
        default: return SetccFor(op);
    }
}

// Bytes que ficam na pilha numa chamada. Em cdecl vao todos; stdcall e
// fastcall mandam os dois primeiros dword em ecx/edx, e o AbiStackBytes ja
// os desconta, por isso serve tambem para a decoracao @N.
std::uint32_t PushedBytes(const std::vector<std::uint32_t>& sizes, Abi abi)
{
    if (abi != Abi::Cdecl)
    {
        return static_cast<std::uint32_t>(AbiStackBytes(abi, sizes, Architecture::X86));
    }

    std::uint32_t total = 0;
    for (const std::uint32_t size : sizes)
    {
        total += size;
    }

    return total;
}

const char* OpName(Exir::OpCode op)
{
    switch (op)
    {
        case Exir::OpCode::Gep: return "gep";
        case Exir::OpCode::Extract: return "extract";
        case Exir::OpCode::Insert: return "insert";
        case Exir::OpCode::Switch: return "switch";
        case Exir::OpCode::Select: return "select";
        case Exir::OpCode::CallPtr: return "callptr";
        case Exir::OpCode::Asm: return "asm";
        case Exir::OpCode::Trap: return "trap";
        case Exir::OpCode::AtomicLoad: return "atomicload";
        case Exir::OpCode::AtomicStore: return "atomicstore";
        case Exir::OpCode::AtomicCas: return "atomiccas";
        case Exir::OpCode::Fence: return "fence";
        default: return "opcode";
    }
}

} // namespace

X86Generator::X86Generator()
    : NasmGeneratorBase(Architecture::X86)
{
}

void X86Generator::EmitArchHelpers(std::ostringstream& text)
{
    // As rotinas vivem no proprio ficheiro porque o runtime numerico compilado
    // com gcc so existe em x86_64; a __emux_rem64 e o seu canal de resto.
    if (m_NeedsUdiv64)
    {
        text << kUdiv64Helper;
    }

    if (m_NeedsIdiv64)
    {
        text << kIdiv64Helper;
    }

    if (m_NeedsUdiv64 || m_NeedsIdiv64)
    {
        text << "\nsection .bss\n__emux_rem64: resd 2\nsection .text\n";
    }
}

// ---------------------------------------------------------------------------
// Localizacao de armazenamento
// ---------------------------------------------------------------------------

std::string X86Generator::GlobalName(std::uint32_t id) const
{
    if (m_CurrentModule)
    {
        for (const Exir::Global& global : m_CurrentModule->Globals)
        {
            if (global.Id == id)
            {
                return global.Name;
            }
        }
        for (const Exir::Global& global : m_CurrentModule->ExternalGlobals)
        {
            if (global.Id == id)
            {
                return global.Name;
            }
        }
    }

    return "@global_" + std::to_string(id);
}

std::string X86Generator::Memory(const Address& address, int extra) const
{
    if (address.FromFrame)
    {
        return "[ebp" + OffsetText(address.Offset + extra) + "]";
    }

    if (extra == 0)
    {
        return "[" + address.Symbol + "]";
    }

    return "[" + address.Symbol + OffsetText(extra) + "]";
}

X86Generator::Address X86Generator::ValueAddress(const Exir::Value& value,
                                                 StackSlots& slots) const
{
    switch (value.Kind)
    {
        case Exir::ValueKind::LocalSlot:
            return Address{ std::string(), slots.GetLocal(value.Id), true };
        case Exir::ValueKind::GlobalRef:
            return Address{ GlobalSymbol(GlobalName(value.Id)), 0, false };
        default:
            return Address{ std::string(), slots.GetTemp(value.Id), true };
    }
}

X86Generator::Address X86Generator::ScratchAddress(std::uint32_t id, StackSlots& slots) const
{
    return Address{ std::string(), slots.GetTemp(id), true };
}

X86Generator::Address X86Generator::FunctionParameter(std::size_t index, StackSlots& slots) const
{
    // Os parametros sao os primeiros locais da funcao, por ordem da
    // assinatura: o slot id coincide com a posicao na lista.
    return Address{ std::string(), slots.GetLocal(static_cast<std::uint32_t>(index)), true };
}

bool X86Generator::TryStaticPointer(const Exir::Value& value, Address& address) const
{
    if (value.Kind != Exir::ValueKind::Temp)
    {
        return false;
    }

    auto it = m_Addrs.find(value.Id);
    if (it == m_Addrs.end())
    {
        return false;
    }

    address = it->second;
    return true;
}

std::string X86Generator::Immediate(std::uint64_t value, std::uint32_t bits) const
{
    const std::uint64_t mask = bits == 0 || bits >= 32 ? 0xFFFFFFFFull : LowMask(bits);
    return std::to_string(value & mask);
}

// ---------------------------------------------------------------------------
// Cargas e guardas
// ---------------------------------------------------------------------------

void X86Generator::IsolateScalar(std::uint32_t bits, bool signedLike, const std::string& reg,
                                 std::ostringstream& out) const
{
    if (bits == 0 || bits >= 32)
    {
        return;
    }

    if (signedLike)
    {
        const std::uint32_t shift = 32 - bits;
        out << "    shl " << reg << ", " << shift << '\n';
        out << "    sar " << reg << ", " << shift << '\n';
        return;
    }

    out << "    and " << reg << ", " << LowMask(bits) << '\n';
}

void X86Generator::LoadScalarFrom(const std::string& mem, const Type& type,
                                  const std::string& reg, std::ostringstream& out) const
{
    const std::uint32_t bits = WidthOf(type);
    const std::uint32_t bytes = BytesOf(type);
    const bool signedLike = SignedLike(type);

    if (bits == 32)
    {
        out << "    mov " << reg << ", dword " << mem << '\n';
        return;
    }

    if (bits == 16)
    {
        out << "    " << (signedLike ? "movsx " : "movzx ") << reg << ", word " << mem << '\n';
        return;
    }

    if (bits == 8)
    {
        out << "    " << (signedLike ? "movsx " : "movzx ") << reg << ", byte " << mem << '\n';
        return;
    }

    if (bytes >= 4)
    {
        // u24/i24 e afins: um dword lido dentro do slot de 8 bytes ou do 'resd'
        // do proprio simbolo, e depois cortado a largura logica.
        out << "    mov " << reg << ", dword " << mem << '\n';
        if (bits % 8 != 0)
        {
            IsolateScalar(bits, signedLike, reg, out);
        }
        return;
    }

    EmitExactLoad(out, mem, bytes, reg);

    // Uma largura que nao e multiplo de 8 pode ter lixo acima do bit de sinal.
    if (bits % 8 != 0)
    {
        IsolateScalar(bits, signedLike, reg, out);
    }
}

void X86Generator::LoadScalar(const Exir::Value& value, const std::string& reg,
                              std::ostringstream& out, StackSlots& slots) const
{
    if (value.Kind == Exir::ValueKind::Constant)
    {
        out << "    mov " << reg << ", " << Immediate(value.Imm, WidthOf(value.Ty)) << '\n';
        return;
    }

    const Type type = value.Ty;
    if (WidthOf(type) > 32)
    {
        // Um valor largo lido para um registo de 32 bits e o seu half baixo.
        out << "    mov " << reg << ", dword " << Memory(ValueAddress(value, slots)) << '\n';
        return;
    }

    LoadScalarFrom(Memory(ValueAddress(value, slots)), type, reg, out);
}

void X86Generator::LoadWideFrom(const std::string& mem, const Type& type,
                                const std::string& low, const std::string& high,
                                std::ostringstream& out) const
{
    const std::uint32_t bits = WidthOf(type);

    out << "    mov " << low << ", dword " << mem << '\n';
    out << "    mov " << high << ", dword " << AddMemOffset(mem, 4) << '\n';

    IsolateWideIn(bits, SignedLike(type), high, out);
}

void X86Generator::IsolateWideIn(std::uint32_t bits, bool signedLike, const std::string& high,
                                 std::ostringstream& out) const
{
    if (bits == 0 || bits <= 32 || bits >= 64)
    {
        return;
    }

    if (signedLike)
    {
        const std::uint32_t shift = 64 - bits;
        out << "    mov ecx, " << shift << '\n';
        out << "    shl " << high << ", cl\n";
        out << "    sar " << high << ", cl\n";
        return;
    }

    out << "    and " << high << ", " << LowMask(bits - 32) << '\n';
}

void X86Generator::LoadWide(const Exir::Value& value, const std::string& low,
                            const std::string& high, std::ostringstream& out,
                            StackSlots& slots) const
{
    const Type type = value.Ty;
    const std::uint32_t bits = WidthOf(type);

    if (value.Kind == Exir::ValueKind::Constant)
    {
        out << "    mov " << low << ", " << Immediate(value.Imm, 32) << '\n';
        out << "    mov " << high << ", " << Immediate(value.Imm >> 32, 32) << '\n';
        IsolateWideIn(bits, SignedLike(type), high, out);
        return;
    }

    if (bits <= 32)
    {
        LoadScalarFrom(Memory(ValueAddress(value, slots)), type, low, out);
        out << (SignedLike(type) ? "    cdq\n" : "    xor " + high + ", " + high + "\n");
        return;
    }

    LoadWideFrom(Memory(ValueAddress(value, slots)), type, low, high, out);
}

X86Generator::Address X86Generator::WideOperand(const Exir::Value& value, std::uint32_t scratch,
                                                std::ostringstream& out, StackSlots& slots) const
{
    const Type type = value.Ty;

    if (value.Kind == Exir::ValueKind::Constant)
    {
        // Um literal guarda ja as duas metades em complemento para dois: o half
        // alto e o sinal para os negativos, zero para os sem sinal.
        const Address where = ScratchAddress(scratch, slots);
        out << "    mov dword " << Memory(where) << ", " << Immediate(value.Imm, 32) << '\n';
        out << "    mov dword " << Memory(where, 4) << ", " << Immediate(value.Imm >> 32, 32)
            << '\n';
        return where;
    }

    if (WidthOf(type) > 32)
    {
        return ValueAddress(value, slots);
    }

    // Valor estreito usado numa operacao de par: estende-se para o scratch.
    const Address where = ScratchAddress(scratch, slots);
    LoadScalar(value, "eax", out, slots);
    out << (SignedLike(type) ? "    cdq\n" : "    xor edx, edx\n");
    StoreWideAt(Memory(where), 8, out);
    return where;
}

void X86Generator::StoreScalarAt(const std::string& mem, std::uint32_t bytes,
                                 const std::string& reg, std::ostringstream& out) const
{
    if (bytes >= 4)
    {
        out << "    mov dword " << mem << ", " << reg << '\n';
        return;
    }

    if (bytes == 2)
    {
        out << "    mov word " << mem << ", " << RegName(reg, 16) << '\n';
        return;
    }

    out << "    mov byte " << mem << ", " << RegName(reg, 8) << '\n';
}

void X86Generator::StoreWideAt(const std::string& mem, std::uint32_t bytes,
                               std::ostringstream& out) const
{
    if (bytes >= 8)
    {
        out << "    mov dword " << mem << ", eax\n";
        out << "    mov dword " << AddMemOffset(mem, 4) << ", edx\n";
        return;
    }

    EmitWideStoreBytes(out, mem, bytes);
}

void X86Generator::StoreResult(const Exir::Instruction& inst, std::ostringstream& out,
                               StackSlots& slots) const
{
    const Type type = inst.ResultTy;
    if (!type.IsValid() || type.IsVoid())
    {
        return;
    }

    const Address where = ScratchAddress(inst.DstId, slots);

    if (IsWide(type))
    {
        StoreWideAt(Memory(where), BytesOf(type), out);
        return;
    }

    StoreScalarAt(Memory(where), BytesOf(type), "eax", out);
}

// ---------------------------------------------------------------------------
// Memoria e blocos
// ---------------------------------------------------------------------------

void X86Generator::LoadPointerInto(const std::string& reg, const Exir::Value& value,
                                   std::ostringstream& out, StackSlots& slots) const
{
    Address base;
    if (TryStaticPointer(value, base))
    {
        // O ponteiro vem de um 'addr': o proprio nome do simbolo ja e o
        // endereco, por isso evita-se ler o slot onde ele foi guardado.
        out << "    lea " << reg << ", " << Memory(base) << '\n';
        return;
    }

    LoadScalar(value, reg, out, slots);
}

void X86Generator::EmitAddress(std::ostringstream& out, const Exir::Instruction& inst,
                               StackSlots& slots)
{
    const Exir::Value& target = inst.Operands[0];

    if (target.Kind != Exir::ValueKind::GlobalRef && target.Kind != Exir::ValueKind::LocalSlot &&
        target.Kind != Exir::ValueKind::Temp)
    {
        throw std::runtime_error("addr de um operando que nao e variavel nem global.");
    }

    const Address address = ValueAddress(target, slots);

    // A largura logica nunca e a de um ponteiro, por isso 'addr' e sempre um
    // valor de 32 bits guardado no slot do temporario.
    m_Addrs[inst.DstId] = address;

    out << "    lea eax, " << Memory(address) << '\n';
    StoreScalarAt(Memory(ScratchAddress(inst.DstId, slots)), 4, "eax", out);
}

void X86Generator::EmitLoad(std::ostringstream& out, const Exir::Instruction& inst,
                            StackSlots& slots)
{
    const Type type = inst.ResultTy;
    const Exir::Value& pointer = inst.Operands[0];

    Address base;
    const bool direct = TryStaticPointer(pointer, base);
    const std::string mem = direct ? Memory(base) : std::string();

    if (!direct)
    {
        LoadScalar(pointer, "ecx", out, slots);
    }

    const std::string target = direct ? mem : std::string("[ecx]");

    if (IsWide(type))
    {
        out << "    mov eax, dword " << target << '\n';
        out << "    mov edx, dword " << AddMemOffset(target, 4) << '\n';
        IsolateWideIn(WidthOf(type), SignedLike(type), "edx", out);
    }
    else
    {
        LoadScalarFrom(target, type, "eax", out);
    }

    StoreResult(inst, out, slots);
}

void X86Generator::EmitStore(std::ostringstream& out, const Exir::Instruction& inst,
                             StackSlots& slots)
{
    const Type type = inst.ResultTy;
    const Exir::Value& value = inst.Operands[0];
    const Exir::Value& pointer = inst.Operands[1];

    Address base;
    const bool dynamic = !TryStaticPointer(pointer, base);
    const std::string target = dynamic ? std::string("[ecx]") : Memory(base);

    // Só um valor largo ameaca ecx: o half alto isola-se com shifts em cl, e
    // o ponteiro teria de sobreviver a eles.
    const bool stashed = dynamic && IsWide(type);
    const Address stash = ScratchAddress(kScratchRight, slots);

    if (dynamic)
    {
        LoadScalar(pointer, "ecx", out, slots);
        if (stashed)
        {
            out << "    mov dword " << Memory(stash) << ", ecx\n";
        }
    }

    if (IsWide(type))
    {
        LoadWide(value, "eax", "edx", out, slots);
    }
    else
    {
        LoadScalar(value, "eax", out, slots);
    }

    if (stashed)
    {
        out << "    mov ecx, dword " << Memory(stash) << '\n';
    }

    if (IsWide(type))
    {
        StoreWideAt(target, BytesOf(type), out);
        return;
    }

    StoreScalarAt(target, BytesOf(type), "eax", out);
}

void X86Generator::EmitBlockOperation(std::ostringstream& out, const Exir::Instruction& inst,
                                      StackSlots& slots)
{
    const Exir::Value& count = inst.Operands[2];

    if (count.Kind != Exir::ValueKind::Constant)
    {
        throw std::runtime_error("tamanho de bloco nao literal em memcpy/memset.");
    }

    if (inst.Op == Exir::OpCode::Memcpy)
    {
        // esi e edi sao callee-saved no cdecl/win32, mas a sequencia rep e
        // autocontida: guardar e devolver em vez de assumir registos livres.
        out << "    push esi\n";
        out << "    push edi\n";
        LoadPointerInto("edi", inst.Operands[0], out, slots);
        LoadPointerInto("esi", inst.Operands[1], out, slots);
        out << "    mov ecx, " << count.Imm << '\n';
        out << "    rep movsb\n";
        out << "    pop edi\n";
        out << "    pop esi\n";
        return;
    }

    // A pilha nao vem limpa: zerar um bloco do frame e trabalho real.
    out << "    push edi\n";
    LoadPointerInto("edi", inst.Operands[0], out, slots);
    LoadScalar(inst.Operands[1], "eax", out, slots);
    out << "    mov ecx, " << count.Imm << '\n';
    out << "    rep stosb\n";
    out << "    pop edi\n";
}

void X86Generator::EmitMemcmp(std::ostringstream& out, const Exir::Instruction& inst,
                              StackSlots& slots)
{
    const Exir::Value& count = inst.Operands[2];

    if (count.Kind != Exir::ValueKind::Constant)
    {
        throw std::runtime_error("tamanho de bloco nao literal em memcmp.");
    }

    const std::string equal = NewLabel("cmpmem_eq");
    const std::string end = NewLabel("cmpmem_end");

    // 'repe cmpsb' deixa em CF a ordem do ultimo par de bytes: CF=1 significa
    // que a esquerda e menor. O empate e 0; o desempate de capacidades entra
    // no builder por aritmetica, nao aqui.
    out << "    push esi\n";
    out << "    push edi\n";
    LoadPointerInto("esi", inst.Operands[0], out, slots);
    LoadPointerInto("edi", inst.Operands[1], out, slots);
    out << "    mov ecx, " << count.Imm << '\n';
    out << "    repe cmpsb\n";
    out << "    je " << equal << '\n';
    out << "    mov eax, 1\n";
    out << "    mov edx, -1\n";
    out << "    cmovc eax, edx\n";
    out << "    jmp " << end << '\n';
    out << equal << ":\n";
    out << "    xor eax, eax\n";
    out << end << ":\n";
    out << "    pop edi\n";
    out << "    pop esi\n";

    StoreResult(inst, out, slots);
}

// ---------------------------------------------------------------------------
// Aritmetica
// ---------------------------------------------------------------------------

void X86Generator::EmitArithmetic(std::ostringstream& out, const Exir::Instruction& inst,
                                 StackSlots& slots)
{
    const Type type = inst.ResultTy;

    if (IsWide(type))
    {
        const Address a = WideOperand(inst.Operands[0], kScratchLeft, out, slots);
        const Address b = WideOperand(inst.Operands[1], kScratchRight, out, slots);

        switch (inst.Op)
        {
            case Exir::OpCode::Add:
                EmitPairLogic("add", "adc", a, b, out);
                break;
            case Exir::OpCode::Sub:
                EmitPairLogic("sub", "sbb", a, b, out);
                break;
            case Exir::OpCode::And:
                EmitPairLogic("and", "and", a, b, out);
                break;
            case Exir::OpCode::Or:
                EmitPairLogic("or", "or", a, b, out);
                break;
            case Exir::OpCode::Xor:
                EmitPairLogic("xor", "xor", a, b, out);
                break;
            case Exir::OpCode::Mul:
                EmitWideMultiply(a, b, out, slots);
                break;
            case Exir::OpCode::UDiv:
            case Exir::OpCode::URem:
                EmitWideDivRem(inst, a, b, false, out, slots);
                return;
            case Exir::OpCode::SDiv:
            case Exir::OpCode::SRem:
                EmitWideDivRem(inst, a, b, true, out, slots);
                return;
            default:
                throw std::runtime_error(std::string("operacao larga invalida: ") +
                                         OpName(inst.Op));
        }

        StoreResult(inst, out, slots);
        return;
    }

    LoadScalar(inst.Operands[0], "eax", out, slots);

    // O divisor e sempre o ultimo a ser carregado: as cargas exactas usam edx
    // como scratch, e edx faz parte do dividendo em 'div'/'idiv'.
    LoadScalar(inst.Operands[1], "ecx", out, slots);

    switch (inst.Op)
    {
        case Exir::OpCode::Add: out << "    add eax, ecx\n"; break;
        case Exir::OpCode::Sub: out << "    sub eax, ecx\n"; break;
        case Exir::OpCode::Mul: out << "    imul eax, ecx\n"; break;
        case Exir::OpCode::And: out << "    and eax, ecx\n"; break;
        case Exir::OpCode::Or: out << "    or eax, ecx\n"; break;
        case Exir::OpCode::Xor: out << "    xor eax, ecx\n"; break;
        case Exir::OpCode::UDiv:
            out << "    xor edx, edx\n";
            out << "    div ecx\n";
            break;
        case Exir::OpCode::URem:
            out << "    xor edx, edx\n";
            out << "    div ecx\n";
            out << "    mov eax, edx\n";
            break;
        case Exir::OpCode::SDiv:
            out << "    cdq\n";
            out << "    idiv ecx\n";
            break;
        case Exir::OpCode::SRem:
            out << "    cdq\n";
            out << "    idiv ecx\n";
            out << "    mov eax, edx\n";
            break;
        default:
            throw std::runtime_error(std::string("operacao invalida: ") + OpName(inst.Op));
    }

    StoreResult(inst, out, slots);
}

void X86Generator::EmitPairLogic(const std::string& lowOp, const std::string& highOp,
                                 const Address& a, const Address& b,
                                 std::ostringstream& out) const
{
    out << "    mov eax, dword " << Memory(a) << '\n';
    out << "    mov edx, dword " << Memory(a, 4) << '\n';
    out << "    " << lowOp << " eax, dword " << Memory(b) << '\n';
    out << "    " << highOp << " edx, dword " << Memory(b, 4) << '\n';
}

void X86Generator::EmitWideMultiply(const Address& a, const Address& b,
                                    std::ostringstream& out, StackSlots& slots) const
{
    const Address carry = ScratchAddress(kScratchCarry, slots);
    const Address low = ScratchAddress(kScratchLow, slots);

    // Produtos parciais em 'mul' (sem sinal): os bits baixos de 64x64 mod 2^64
    // nao dependem do sinal dos operandos.
    out << "    mov eax, dword " << Memory(a) << '\n';
    out << "    mov ecx, dword " << Memory(b) << '\n';
    out << "    mul ecx\n";
    out << "    mov dword " << Memory(carry) << ", edx\n";
    out << "    mov dword " << Memory(low) << ", eax\n";

    out << "    mov eax, dword " << Memory(a, 4) << '\n';
    out << "    mul ecx\n";
    out << "    add eax, dword " << Memory(carry) << '\n';
    out << "    mov dword " << Memory(carry) << ", eax\n";

    out << "    mov eax, dword " << Memory(a) << '\n';
    out << "    mov ecx, dword " << Memory(b, 4) << '\n';
    out << "    mul ecx\n";
    out << "    add eax, dword " << Memory(carry) << '\n';

    out << "    mov edx, eax\n";
    out << "    mov eax, dword " << Memory(low) << '\n';
}

void X86Generator::EmitWideDivRem(const Exir::Instruction& inst, const Address& a,
                                  const Address& b, bool isSigned, std::ostringstream& out,
                                  StackSlots& slots)
{
    const Type type = inst.ResultTy;
    const bool wantsRemainder = inst.Op == Exir::OpCode::URem || inst.Op == Exir::OpCode::SRem;
    const Address dst = ScratchAddress(inst.DstId, slots);

    // Empilhado de tras para a frente: o helper le [ebp+8] como o endereco de
    // saida do quociente e os quatro dword seguintes como dividendo e divisor.
    out << "    push dword " << Memory(b, 4) << '\n';
    out << "    push dword " << Memory(b) << '\n';
    out << "    push dword " << Memory(a, 4) << '\n';
    out << "    push dword " << Memory(a) << '\n';
    out << "    lea eax, " << Memory(dst) << '\n';
    out << "    push eax\n";

    if (isSigned)
    {
        m_NeedsUdiv64 = true;
        m_NeedsIdiv64 = true;
        out << "    call __emux_idiv64\n";
    }
    else
    {
        m_NeedsUdiv64 = true;
        out << "    call __emux_udiv64\n";
    }

    out << "    add esp, 20\n";

    // Os dois helpers saem com o resto em edx:eax; o quociente so existe no
    // slot escrito por referencia, por isso e dai que se carrega.
    if (wantsRemainder)
    {
        out << "    mov eax, dword [__emux_rem64]\n";
        out << "    mov edx, dword [__emux_rem64+4]\n";
    }
    else
    {
        out << "    mov eax, dword " << Memory(dst) << '\n';
        out << "    mov edx, dword " << Memory(dst, 4) << '\n';
    }

    if (IsWide(type))
    {
        IsolateWideIn(WidthOf(type), SignedLike(type), "edx", out);
    }

    StoreResult(inst, out, slots);
}

void X86Generator::EmitShift(std::ostringstream& out, const Exir::Instruction& inst,
                             StackSlots& slots)
{
    const Type type = inst.ResultTy;
    const Exir::Value& count = inst.Operands[1];

    if (IsWide(type))
    {
        const Address value = WideOperand(inst.Operands[0], kScratchLeft, out, slots);

        if (count.Kind == Exir::ValueKind::Constant)
        {
            out << "    mov ecx, " << Immediate(count.Imm, 32) << '\n';
        }
        else
        {
            // A contagem larga so usa o half baixo; acima de 63 e indefinido.
            out << "    mov ecx, dword " << Memory(ValueAddress(count, slots)) << '\n';
        }

        // O hardware mascara a contagem com 31; para um par de 64 bits a
        // semantica e a mesma do x86_64, ou seja, modulo 64.
        out << "    and ecx, 63\n";

        EmitWideShift(inst.Op, value, type, out);
        StoreResult(inst, out, slots);
        return;
    }

    LoadScalar(inst.Operands[0], "eax", out, slots);
    LoadScalar(count, "ecx", out, slots);

    switch (inst.Op)
    {
        case Exir::OpCode::Shl: out << "    shl eax, cl\n"; break;
        case Exir::OpCode::LShr: out << "    shr eax, cl\n"; break;
        case Exir::OpCode::AShr: out << "    sar eax, cl\n"; break;
        default:
            throw std::runtime_error(std::string("shift invalido: ") + OpName(inst.Op));
    }

    IsolateScalar(WidthOf(type), SignedLike(type), "eax", out);
    StoreResult(inst, out, slots);
}

void X86Generator::EmitWideShift(Exir::OpCode op, const Address& value, const Type& type,
                                 std::ostringstream& out) const
{
    out << "    mov eax, dword " << Memory(value) << '\n';
    out << "    mov edx, dword " << Memory(value, 4) << '\n';

    // Uma regiao de 33 a 63 bits nao preenche o dword alto: os bits acima de
    // 'Bits' pertencem ao vizinho do frame. Sem isolar ja, o ramo de contagem
    // >= 32 propagaria esse lixo quando move edx para eax.
    const std::uint32_t bits = WidthOf(type);
    if (bits > 32 && bits < 64)
    {
        if (SignedLike(type))
        {
            out << "    shl edx, " << (64 - bits) << '\n';
            out << "    sar edx, " << (64 - bits) << '\n';
        }
        else
        {
            out << "    and edx, " << LowMask(bits - 32) << '\n';
        }
    }

    const std::string low = NewLabel("shift_low");
    const std::string equal = NewLabel("shift_eq");
    const std::string done = NewLabel("shift_done");

    out << "    cmp ecx, 32\n";
    out << "    jb " << low << '\n';
    out << "    je " << equal << '\n';

    // Contagens de 33 a 63: um dos halves vai todo para o outro e o que fica
    // a baixo e zero (ou o sinal, no shift aritmetico).
    out << "    sub ecx, 32\n";

    if (op == Exir::OpCode::Shl)
    {
        out << "    mov edx, eax\n";
        out << "    shl edx, cl\n";
        out << "    xor eax, eax\n";
    }
    else
    {
        out << "    mov eax, edx\n";
        out << (op == Exir::OpCode::AShr ? "    sar eax, cl\n" : "    shr eax, cl\n");
        if (op == Exir::OpCode::AShr)
        {
            out << "    sar edx, 31\n";
        }
        else
        {
            out << "    xor edx, edx\n";
        }
    }

    out << "    jmp " << done << '\n';
    out << equal << ":\n";

    // Contagem exatamente 32: troca de half, 'cl' nao pode ser usado.
    if (op == Exir::OpCode::Shl)
    {
        out << "    mov edx, eax\n";
        out << "    xor eax, eax\n";
    }
    else
    {
        out << "    mov eax, edx\n";
        if (op == Exir::OpCode::AShr)
        {
            out << "    sar edx, 31\n";
        }
        else
        {
            out << "    xor edx, edx\n";
        }
    }

    out << "    jmp " << done << '\n';
    out << low << ":\n";

    // Contagens de 0 a 31: 'shld'/'shrd' mexem os dois halves de uma vez.
    if (op == Exir::OpCode::Shl)
    {
        out << "    shld edx, eax, cl\n";
        out << "    shl eax, cl\n";
    }
    else
    {
        out << "    shrd eax, edx, cl\n";
        out << (op == Exir::OpCode::AShr ? "    sar edx, cl\n" : "    shr edx, cl\n");
    }

    out << done << ":\n";
}

void X86Generator::EmitUnary(std::ostringstream& out, const Exir::Instruction& inst,
                             StackSlots& slots)
{
    const Type type = inst.ResultTy;

    if (IsWide(type))
    {
        const Address a = WideOperand(inst.Operands[0], kScratchLeft, out, slots);

        out << "    mov eax, dword " << Memory(a) << '\n';
        out << "    mov edx, dword " << Memory(a, 4) << '\n';

        if (inst.Op == Exir::OpCode::Neg)
        {
            out << "    not edx\n";
            out << "    neg eax\n";
            out << "    sbb edx, -1\n";
        }
        else
        {
            out << "    not eax\n";
            out << "    not edx\n";
        }

        IsolateWideIn(WidthOf(type), SignedLike(type), "edx", out);
        StoreResult(inst, out, slots);
        return;
    }

    LoadScalar(inst.Operands[0], "eax", out, slots);

    if (inst.Op == Exir::OpCode::Neg)
    {
        out << "    neg eax\n";
    }
    else
    {
        out << "    not eax\n";
    }

    IsolateScalar(WidthOf(type), SignedLike(type), "eax", out);
    StoreResult(inst, out, slots);
}

void X86Generator::EmitCompare(std::ostringstream& out, const Exir::Instruction& inst,
                               StackSlots& slots)
{
    // O tipo da comparacao e o dos operandos; ResultTy e so o booleano (u1/u8).
    const Type type = inst.Operands[0].Ty;
    const bool equality = inst.Op == Exir::OpCode::CmpEq || inst.Op == Exir::OpCode::CmpNe;

    if (IsWide(type))
    {
        const Address a = WideOperand(inst.Operands[0], kScratchLeft, out, slots);
        const Address b = WideOperand(inst.Operands[1], kScratchRight, out, slots);

        if (equality)
        {
            // Igualdade e diferenca nao precisam de ordem: xor das duas metades.
            out << "    mov eax, dword " << Memory(a) << '\n';
            out << "    xor eax, dword " << Memory(b) << '\n';
            out << "    mov edx, dword " << Memory(a, 4) << '\n';
            out << "    xor edx, dword " << Memory(b, 4) << '\n';
            out << "    or eax, edx\n";
            out << "    " << SetccFor(inst.Op) << " al\n";
            out << "    movzx eax, al\n";
            StoreResult(inst, out, slots);
            return;
        }

        const std::string sign = NewLabel("cmp_sign");
        const std::string end = NewLabel("cmp_end");

        out << "    mov eax, dword " << Memory(a) << '\n';
        out << "    mov edx, dword " << Memory(a, 4) << '\n';
        out << "    cmp edx, dword " << Memory(b, 4) << '\n';
        out << "    jne " << sign << '\n';
        out << "    cmp eax, dword " << Memory(b) << '\n';
        out << "    " << UnsignedSetccFor(inst.Op) << " al\n";
        out << "    movzx eax, al\n";
        out << "    jmp " << end << '\n';
        out << sign << ":\n";
        // Os halves altos mandam sozinhos: as flags do 'cmp' de cima vivem da
        // saida do registo, por isso o setcc entra aqui e nao depois do jmp.
        out << "    " << SetccFor(inst.Op) << " dl\n";
        out << "    movzx eax, dl\n";
        out << end << ":\n";
        StoreResult(inst, out, slots);
        return;
    }

    LoadScalar(inst.Operands[0], "eax", out, slots);
    LoadScalar(inst.Operands[1], "ecx", out, slots);

    out << "    cmp eax, ecx\n";
    out << "    " << SetccFor(inst.Op) << " al\n";
    out << "    movzx eax, al\n";

    StoreResult(inst, out, slots);
}

void X86Generator::EmitConversion(std::ostringstream& out, const Exir::Instruction& inst,
                                  StackSlots& slots)
{
    const Type from = inst.Operands[0].Ty;
    const Type to = inst.ResultTy;
    const std::uint32_t fromBits = WidthOf(from);
    const std::uint32_t toBits = WidthOf(to);
    const bool toWide = toBits > 32;
    const bool fromWide = fromBits > 32;

    if (from.IsFloat() || from.IsFixed() || to.IsFloat() || to.IsFixed())
    {
        throw std::runtime_error(
            "conversao de float ou ponto fixo exige x86_64; nao ha runtime em x86."
        );
    }

    switch (inst.Op)
    {
        case Exir::OpCode::Trunc:
            if (fromWide)
            {
                LoadWide(inst.Operands[0], "eax", "edx", out, slots);
                if (toWide)
                {
                    IsolateWideIn(toBits, SignedLike(to), "edx", out);
                }
            }
            else
            {
                LoadScalar(inst.Operands[0], "eax", out, slots);
                IsolateScalar(toBits, SignedLike(to), "eax", out);
            }
            break;

        case Exir::OpCode::SExt:
        case Exir::OpCode::ZExt:
        {
            // O sinal da extensao e o do operando: um u24 nunca se estende com
            // uns, mesmo que o destino seja com sinal.
            const bool sign = inst.Op == Exir::OpCode::SExt && SignedLike(from);

            if (fromWide)
            {
                LoadWide(inst.Operands[0], "eax", "edx", out, slots);
                IsolateWideIn(fromBits, sign, "edx", out);
                break;
            }

            LoadScalar(inst.Operands[0], "eax", out, slots);
            if (!toWide)
            {
                break;
            }

            if (fromBits < 32 && sign)
            {
                // O bit de sinal esta em fromBits-1: sobe-o ate 31 para que
                // 'cdq' produza o half alto correto.
                const std::uint32_t shift = 32 - fromBits;
                out << "    shl eax, " << shift << '\n';
                out << "    sar eax, " << shift << '\n';
            }

            out << (sign ? "    cdq\n" : "    xor edx, edx\n");
            break;
        }

        case Exir::OpCode::BitCast:
        case Exir::OpCode::PtrToInt:
        case Exir::OpCode::IntToPtr:
        case Exir::OpCode::Conv:
        {
            if (fromWide)
            {
                LoadWide(inst.Operands[0], "eax", "edx", out, slots);
                if (!toWide)
                {
                    IsolateScalar(toBits, SignedLike(to), "eax", out);
                }
                else
                {
                    IsolateWideIn(toBits, SignedLike(to), "edx", out);
                }
                break;
            }

            LoadScalar(inst.Operands[0], "eax", out, slots);
            if (!toWide)
            {
                // Reinterpretacao entre larguras pequenas: os bits baixos ficam
                // e o resto e lixo que o proximo load isola.
                break;
            }

            if (SignedLike(from) && fromBits < 32)
            {
                const std::uint32_t shift = 32 - fromBits;
                out << "    shl eax, " << shift << '\n';
                out << "    sar eax, " << shift << '\n';
            }
            out << (SignedLike(from) ? "    cdq\n" : "    xor edx, edx\n");
            break;
        }

        default:
            throw std::runtime_error(std::string("conversao invalida: ") + OpName(inst.Op));
    }

    StoreResult(inst, out, slots);
}

// ---------------------------------------------------------------------------
// Chamadas
// ---------------------------------------------------------------------------

const Exir::Function* X86Generator::FindFunction(std::string_view name) const
{
    if (!m_CurrentModule)
    {
        return nullptr;
    }

    for (const std::unique_ptr<Exir::Function>& function : m_CurrentModule->Functions)
    {
        if (function && function->Name == name)
        {
            return function.get();
        }
    }

    return nullptr;
}

const Exir::Import* X86Generator::FindImport(std::string_view name) const
{
    if (!m_CurrentModule)
    {
        return nullptr;
    }

    for (const Exir::Import& import : m_CurrentModule->Imports)
    {
        if (import.Name == name || import.Symbol == name)
        {
            return &import;
        }
    }

    return nullptr;
}

std::uint32_t X86Generator::CalleeStackBytes(const Exir::Function& function) const
{
    std::vector<std::uint32_t> sizes;
    for (const Exir::Parameter& parameter : function.Parameters)
    {
        sizes.push_back(SlotBytes(parameter.Ty));
    }

    return static_cast<std::uint32_t>(AbiStackBytes(function.Convention, sizes, Architecture::X86));
}

std::string X86Generator::FunctionSymbol(const Exir::Function& function) const
{
    return NasmUtils::Mangled(function.Name, CalleeStackBytes(function), function.Convention,
                             Architecture::X86);
}

std::string X86Generator::CallTarget(const Exir::Instruction& inst, std::uint32_t& stackBytes,
                                     Abi& abi) const
{
    const std::string& name = inst.Extra->Text;

    if (const Exir::Function* function = FindFunction(name))
    {
        abi = function->Convention;

        // Para uma funcao local o espaco de pilha vem dos parametros
        // declarados, nao dos tipos dos argumentos no call site: um u32
        // passado para um i64 ocupa 8 bytes na pilha.
        std::vector<std::uint32_t> sizes;
        for (std::size_t i = 0; i < inst.Extra->Arguments.size(); ++i)
        {
            const Type type = i < function->Parameters.size()
                ? function->Parameters[i].Ty
                : inst.Extra->Arguments[i].Ty;
            sizes.push_back(SlotBytes(type));
        }

        stackBytes = PushedBytes(sizes, abi);
        return NasmUtils::Mangled(function->Name, stackBytes, abi, Architecture::X86);
    }

    if (const Exir::Import* import = FindImport(name))
    {
        // Uma importacao nao traz assinatura: os tamanhos saem dos tipos dos
        // argumentos do call site, que o builder ja alargou ao parametro.
        std::vector<std::uint32_t> sizes;
        for (const Exir::Value& argument : inst.Extra->Arguments)
        {
            sizes.push_back(SlotBytes(argument.Ty));
        }

        abi = import->Convention;
        stackBytes = PushedBytes(sizes, abi);
        return NasmUtils::Mangled(import->Symbol, stackBytes, abi, Architecture::X86);
    }

    abi = Abi::Cdecl;
    std::vector<std::uint32_t> sizes;
    for (const Exir::Value& argument : inst.Extra->Arguments)
    {
        sizes.push_back(SlotBytes(argument.Ty));
    }

    stackBytes = PushedBytes(sizes, abi);
    return NasmUtils::Mangled(name, stackBytes, abi, Architecture::X86);
}

void X86Generator::PushArgument(const PendingArgument& argument, std::ostringstream& out,
                                StackSlots& slots) const
{
    const Exir::Value& value = *argument.Value;
    const bool wide = argument.Bytes > 4;

    if (value.Kind == Exir::ValueKind::Constant && !wide)
    {
        out << "    push dword " << Immediate(value.Imm, WidthOf(value.Ty)) << '\n';
        return;
    }

    if (value.Kind == Exir::ValueKind::Constant && wide)
    {
        out << "    push dword " << Immediate(value.Imm >> 32, 32) << '\n';
        out << "    push dword " << Immediate(value.Imm, 32) << '\n';
        return;
    }

    if (wide && IsWide(value.Ty))
    {
        LoadWide(value, "eax", "edx", out, slots);
        out << "    push edx\n";
        out << "    push eax\n";
        return;
    }

    // Argumento estreito num slot largo: o half alto e a extensao do proprio
    // tipo, porque a conversao implicita para o parametro e zero ou sinal.
    LoadScalar(value, "eax", out, slots);
    if (wide)
    {
        out << (SignedLike(value.Ty) ? "    cdq\n" : "    xor edx, edx\n");
        out << "    push edx\n";
    }
    out << "    push eax\n";
}

void X86Generator::EmitCall(std::ostringstream& out, const Exir::Instruction& inst,
                            StackSlots& slots)
{
    if (!inst.Extra || inst.Extra->Text.empty())
    {
        throw std::runtime_error("call sem simbolo de destino.");
    }

    std::uint32_t stackBytes = 0;
    Abi abi = Abi::Cdecl;
    const std::string symbol = CallTarget(inst, stackBytes, abi);

    const Exir::Function* callee = FindFunction(inst.Extra->Text);

    std::vector<PendingArgument> registerArguments;
    std::vector<PendingArgument> stackArguments;
    int registers = 0;

    for (std::size_t i = 0; i < inst.Extra->Arguments.size(); ++i)
    {
        const Exir::Value& value = inst.Extra->Arguments[i];
        Type as = value.Ty;

        if (callee && i < callee->Parameters.size())
        {
            as = callee->Parameters[i].Ty;
        }

        const std::uint32_t bytes = SlotBytes(as);
        if (abi == Abi::Fastcall && registers < 2 && bytes <= 4)
        {
            registerArguments.push_back(PendingArgument{ &value, as, bytes });
            ++registers;
            continue;
        }

        stackArguments.push_back(PendingArgument{ &value, as, bytes });
    }

    // Direita para a esquerda: o primeiro argumento fica no topo da pilha, que
    // e onde o chamado o procura em cdecl/stdcall.
    for (auto it = stackArguments.rbegin(); it != stackArguments.rend(); ++it)
    {
        PushArgument(*it, out, slots);
    }

    const Address registerStash = ScratchAddress(kScratchRight, slots);
    for (std::size_t i = 0; i < registerArguments.size(); ++i)
    {
        const PendingArgument& argument = registerArguments[i];
        const std::string reg = i == 0 ? "ecx" : "edx";

        // Os argumentos de registo carregam por ultimo: eax e edx podem ter
        // servido de scratch nas empilhas anteriores.
        LoadScalar(*argument.Value, reg, out, slots);

        // As cargas byte a byte do segundo argumento usam ecx como scratch.
        if (i == 0 && registerArguments.size() > 1)
        {
            out << "    mov dword " << Memory(registerStash) << ", ecx\n";
        }
    }

    if (registerArguments.size() > 1)
    {
        out << "    mov ecx, dword " << Memory(registerStash) << '\n';
    }

    if (!callee)
    {
        DeclareImport(symbol);
    }

    out << "    call " << symbol << '\n';

    if (abi == Abi::Cdecl && stackBytes > 0)
    {
        out << "    add esp, " << stackBytes << '\n';
    }

    StoreResult(inst, out, slots);
}

// ---------------------------------------------------------------------------
// Terminadores
// ---------------------------------------------------------------------------

std::string X86Generator::BlockLabel(std::uint32_t blockId) const
{
    return ".Lctl_bb_" + std::to_string(m_FunctionIndex) + "_" + std::to_string(blockId);
}

std::string X86Generator::NewLabel(std::string_view prefix) const
{
    return ".Lctl_" + std::string(prefix) + "_" + std::to_string(m_FunctionIndex) + "_" +
           std::to_string(m_LabelCounter++);
}

void X86Generator::EmitTerminator(std::ostringstream& out, const Exir::Instruction& inst,
                                  StackSlots& slots)
{
    switch (inst.Op)
    {
        case Exir::OpCode::Br:
            out << "    jmp " << BlockLabel(inst.Operands[0].Id) << '\n';
            break;

        case Exir::OpCode::CBr:
            LoadScalar(inst.Operands[0], "eax", out, slots);
            out << "    test eax, eax\n";
            // Os dois alvos vao por salto explicito: a ordem dos blocos no
            // ficheiro nao pode decidir o caminho de execucao.
            out << "    je " << BlockLabel(inst.Operands[2].Id) << '\n';
            out << "    jmp " << BlockLabel(inst.Operands[1].Id) << '\n';
            break;

        case Exir::OpCode::Ret:
            if (inst.Operands[0].Kind != Exir::ValueKind::None &&
                inst.Operands[0].Ty.IsValid() && !inst.Operands[0].Ty.IsVoid())
            {
                if (IsWide(inst.Operands[0].Ty))
                {
                    LoadWide(inst.Operands[0], "eax", "edx", out, slots);
                }
                else
                {
                    LoadScalar(inst.Operands[0], "eax", out, slots);
                }
            }
            EmitEpilogue(out, *m_CurrentFunction);
            break;

        case Exir::OpCode::Unreachable:
            break;

        default:
            throw std::runtime_error(std::string("terminador nao suportado em x86: ") +
                                     OpName(inst.Op));
    }
}

// ---------------------------------------------------------------------------
// Funcao
// ---------------------------------------------------------------------------

void X86Generator::ReserveSlots(const Exir::Function& function, StackSlots& slots)
{
    for (const Exir::LocalSlot& local : function.Locals)
    {
        // Um bloco precisa da capacidade toda no slot; os restantes valores
        // cabem nos 8 bytes minimos de um par edx:eax.
        slots.AllocateLocal(local.Id, BytesOf(local.Ty));
    }

    // Todos os temporarios antes de emitir: um offset devolvido como 0 seria
    // [ebp+0], o ebp guardado, e corromperia o frame.
    for (const std::unique_ptr<Exir::BasicBlock>& block : function.Blocks)
    {
        if (!block)
        {
            continue;
        }

        for (const Exir::Instruction& inst : block->Instructions)
        {
            if (inst.ResultTy.IsValid() && !inst.ResultTy.IsVoid())
            {
                slots.AllocateTemp(inst.DstId, 8);
            }
        }
    }

    slots.AllocateTemp(kScratchLeft, 8);
    slots.AllocateTemp(kScratchRight, 8);
    slots.AllocateTemp(kScratchCarry, 8);
    slots.AllocateTemp(kScratchLow, 8);
}

void X86Generator::EmitPrologue(std::ostringstream& out, const Exir::Function& function,
                                StackSlots& slots)
{
    out << "    push ebp\n";
    out << "    mov ebp, esp\n";

    // Multiplo de 16 menos 8: o 'push ebp' ja deixou esp a 8 mod 16, por isso
    // o frame devolve o alinhamento de 16 que as chamadas win32 esperam.
    const int reserved = ((slots.Used + 8 + 15) & ~15) - 8;
    if (reserved > 0)
    {
        out << "    sub esp, " << reserved << '\n';
    }

    const Abi abi = function.Convention;
    int registerUsed = 0;
    int stackOffset = 8;

    for (std::size_t i = 0; i < function.Parameters.size(); ++i)
    {
        const Exir::Parameter& parameter = function.Parameters[i];
        const Address where = FunctionParameter(i, slots);
        const std::uint32_t bytes = SlotBytes(parameter.Ty);

        if (abi == Abi::Fastcall && registerUsed < 2 && bytes <= 4)
        {
            // fastcall traz os dois primeiros argumentos curtos em ecx/edx: sem
            // os guardar aqui o slot do parametro ficaria com lixo. Nao ocupam
            // pilha, por isso o deslocamento do proximo argumento fica onde esta.
            out << "    mov " << Memory(where) << ", " << (registerUsed == 0 ? "ecx" : "edx")
                << '\n';
            ++registerUsed;
            continue;
        }

        out << "    mov eax, dword " << Memory(Address{ std::string(), stackOffset, true }) << '\n';
        out << "    mov " << Memory(where) << ", eax\n";

        if (bytes > 4)
        {
            out << "    mov eax, dword "
                << Memory(Address{ std::string(), stackOffset + 4, true }) << '\n';
            out << "    mov " << Memory(where, 4) << ", eax\n";
        }

        stackOffset += bytes;
    }
}

void X86Generator::EmitEpilogue(std::ostringstream& out, const Exir::Function& function)
{
    out << "    leave\n";

    const std::uint32_t cleaned = CalleeStackBytes(function);
    if (cleaned > 0)
    {
        // stdcall e fastcall: limpa a pilha quem foi chamado.
        out << "    ret " << cleaned << '\n';
        return;
    }

    out << "    ret\n";
}

void X86Generator::EmitInstruction(std::ostringstream& out, const Exir::Instruction& inst,
                                   StackSlots& slots)
{
    if (inst.IsTerminator())
    {
        throw std::runtime_error("terminador dentro de um bloco linear.");
    }

    switch (inst.Op)
    {
        case Exir::OpCode::Addr: EmitAddress(out, inst, slots); return;
        case Exir::OpCode::Load: EmitLoad(out, inst, slots); return;
        case Exir::OpCode::Store: EmitStore(out, inst, slots); return;
        case Exir::OpCode::Memcpy:
        case Exir::OpCode::Memset: EmitBlockOperation(out, inst, slots); return;
        case Exir::OpCode::Memcmp: EmitMemcmp(out, inst, slots); return;

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
            EmitArithmetic(out, inst, slots);
            return;

        case Exir::OpCode::Shl:
        case Exir::OpCode::LShr:
        case Exir::OpCode::AShr:
            EmitShift(out, inst, slots);
            return;

        case Exir::OpCode::Neg:
        case Exir::OpCode::Not:
            EmitUnary(out, inst, slots);
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

        case Exir::OpCode::Trunc:
        case Exir::OpCode::ZExt:
        case Exir::OpCode::SExt:
        case Exir::OpCode::BitCast:
        case Exir::OpCode::PtrToInt:
        case Exir::OpCode::IntToPtr:
        case Exir::OpCode::Conv:
            EmitConversion(out, inst, slots);
            return;

        case Exir::OpCode::Call:
            EmitCall(out, inst, slots);
            return;

        default:
            throw std::runtime_error(
                std::string("opcode '") + OpName(inst.Op) +
                "' nao tem lowering em x86 (i386); exige x86_64."
            );
    }
}

void X86Generator::DeclareImport(const std::string& symbol)
{
    // Os 'extern' do bloco de cabecalho usam o nome cru, porque a Exir ainda
    // nao transporta a decoracao; a forma com @N so existe aqui, e em NASM um
    // extern e valido em qualquer linha do ficheiro.
    m_Declared.insert("extern " + symbol);
}

void X86Generator::EmitFunction(std::ostringstream& text, const Exir::Function& function)
{
    m_CurrentFunction = &function;
    m_Addrs.clear();
    m_Declared.clear();

    StackSlots slots;
    ReserveSlots(function, slots);

    std::ostringstream body;
    EmitPrologue(body, function, slots);

    for (const std::unique_ptr<Exir::BasicBlock>& block : function.Blocks)
    {
        if (!block)
        {
            continue;
        }

        body << BlockLabel(block->Id) << ":\n";

        for (const Exir::Instruction& inst : block->Instructions)
        {
            EmitInstruction(body, inst, slots);
        }

        EmitTerminator(body, block->Terminator, slots);
    }

    // Cauda morta: um bloco que caia do ultimo terminador ainda tem de sair
    // do frame pelo mesmo caminho.
    EmitEpilogue(body, function);

    const std::string symbol = FunctionSymbol(function);

    // A base declara 'global' com o nome decorado simples e escreve esse label
    // antes de aqui chegar; so uma convencao com @N precisa de simbolo extra.
    if (symbol != GlobalSymbol(function.Name))
    {
        m_Declared.insert("global " + symbol);
        text << symbol << ":\n";
    }

    for (const std::string& line : m_Declared)
    {
        text << line << '\n';
    }

    text << body.str();
    text << "\n";

    ++m_FunctionIndex;
}

} // namespace Emux
