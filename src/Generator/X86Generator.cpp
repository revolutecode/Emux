#include "Emux/Generator/X86Generator.hpp"
#include "Emux/Generator/NasmUtils.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace Emux
{

namespace
{

const char* const kUdiv64Helper = R"(
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
)";

const char* const kIdiv64Helper = R"(
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
)";

std::string RegName(const std::string& reg32, int bits)
{
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
        { "esi", "si", "sil" },
        { "edi", "di", "dil" }
    };

    for (const Register& reg : registers)
    {
        if (reg32 == reg.r32)
        {
            if (bits <= 8) return reg.r8;
            if (bits <= 16) return reg.r16;
            return reg.r32;
        }
    }

    return reg32;
}

std::string Hex32(std::uint64_t value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%X", static_cast<unsigned>(value & 0xFFFFFFFFu));
    return buffer;
}

std::pair<std::string, std::string> WideLiteralParts(const std::string& text)
{
    std::uint64_t value = 0;

    try
    {
        value = static_cast<std::uint64_t>(std::stoll(text, nullptr, 0));
    }
    catch (...)
    {
        value = std::stoull(text, nullptr, 0);
    }

    return { Hex32(value), Hex32(value >> 32) };
}

} // namespace

X86Generator::X86Generator()
    : NasmGeneratorBase(Architecture::X86)
{
}

std::string X86Generator::FunctionSymbol(const Exir::Function& function) const
{
    return NasmUtils::Mangled(
        function.Name,
        function.StackBytes,
        function.Convention,
        Architecture::X86
    );
}

std::string X86Generator::CallSymbol(const std::string& target) const
{
    if (const Exir::Function* function = FindFunction(target))
    {
        return FunctionSymbol(*function);
    }

    if (const Exir::Import* import = FindImport(target))
    {
        return NasmUtils::Mangled(
            import->Symbol,
            import->StackBytes,
            import->Convention,
            Architecture::X86
        );
    }

    return NasmUtils::Mangled(target, 0, Abi::Cdecl, Architecture::X86);
}

void X86Generator::EmitArchHelpers(std::ostringstream& text)
{
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

std::string X86Generator::DataSymbol(std::string_view name)
{
    const std::string symbol = GlobalSymbol(name);
    if (!IsDefined(symbol))
    {
        AddExtern(symbol);
    }

    return symbol;
}

std::string X86Generator::NewLabel(std::string_view prefix)
{
    return ".L" + std::string(prefix) + "_" + std::to_string(m_LabelCounter++);
}

std::string X86Generator::MemoryOperand(
    const std::string& operand,
    StackSlots& slots,
    int extra
)
{
    int offset = 0;
    if (slots.Find(StripTemp(operand), offset))
    {
        return "[ebp" + FormatOffset(offset + extra) + "]";
    }

    std::string symbol = DataSymbol(operand);
    if (extra != 0)
    {
        symbol += "+" + std::to_string(extra);
    }

    return "[" + symbol + "]";
}

std::string X86Generator::WideSource(
    std::ostringstream& out,
    const std::string& operand,
    StackSlots& slots,
    const std::string& scratch
)
{
    if (!IsLiteralOperand(operand))
    {
        return operand;
    }

    const auto [lo, hi] = WideLiteralParts(operand);

    int offset = 0;
    slots.Find(scratch, offset);

    out << "    mov dword [ebp" << FormatOffset(offset) << "], " << lo << '\n';
    out << "    mov dword [ebp" << FormatOffset(offset + 4) << "], " << hi << '\n';

    return scratch;
}

void X86Generator::LoadValue(
    std::ostringstream& out,
    const std::string& operand,
    const Type& type,
    const std::string& reg32,
    StackSlots& slots
)
{
    if (IsLiteralOperand(operand))
    {
        try
        {
            const long long value = std::stoll(operand, nullptr, 0);
            if (value >= std::numeric_limits<int>::min() &&
                value <= std::numeric_limits<int>::max())
            {
                out << "    mov " << reg32 << ", " << operand << '\n';
                return;
            }

            out << "    mov " << reg32 << ", " << Hex32(static_cast<std::uint64_t>(value)) << '\n';
            return;
        }
        catch (...)
        {
            const auto [lo, hi] = WideLiteralParts(operand);
            out << "    mov " << reg32 << ", " << lo << '\n';
            return;
        }
    }

    const int bits = type.IsValid() ? static_cast<int>(type.Bits) : 32;
    const std::string mem = MemoryOperand(operand, slots);

    if (bits <= 8)
    {
        out << "    " << (type.IsSigned() ? "movsx " : "movzx ")
            << reg32 << ", byte " << mem << '\n';
    }
    else if (bits <= 16)
    {
        out << "    " << (type.IsSigned() ? "movsx " : "movzx ")
            << reg32 << ", word " << mem << '\n';
    }
    else
    {
        out << "    mov " << reg32 << ", dword " << mem << '\n';
    }
}

void X86Generator::LoadAddress(
    std::ostringstream& out,
    const std::string& operand,
    StackSlots& slots
)
{
    int offset = 0;

    if (IsLiteralOperand(operand))
    {
        const auto [lo, hi] = WideLiteralParts(operand);
        out << "    mov ecx, " << lo << '\n';
    }
    else if (slots.Find(StripTemp(operand), offset))
    {
        out << "    mov ecx, dword [ebp" << FormatOffset(offset) << "]\n";
    }
    else
    {
        out << "    lea ecx, [" << DataSymbol(operand) << "]\n";
    }
}

void X86Generator::LoadWide(
    std::ostringstream& out,
    const std::string& operand,
    StackSlots& slots
)
{
    if (IsLiteralOperand(operand))
    {
        const auto [lo, hi] = WideLiteralParts(operand);
        out << "    mov eax, " << lo << '\n';
        out << "    mov edx, " << hi << '\n';
        return;
    }

    const std::string mem = MemoryOperand(operand, slots);
    out << "    mov eax, dword " << mem << '\n';
    out << "    mov edx, dword " << MemoryOperand(operand, slots, 4) << '\n';
}

void X86Generator::PushValue(
    std::ostringstream& out,
    const std::string& operand,
    const Type& type,
    StackSlots& slots,
    int& pushedBytes
)
{
    const int bits = type.IsValid() ? static_cast<int>(type.Bits) : 32;

    if (bits > 32)
    {
        if (IsLiteralOperand(operand))
        {
            const auto [lo, hi] = WideLiteralParts(operand);
            out << "    push dword " << hi << '\n';
            out << "    push dword " << lo << '\n';
        }
        else
        {
            out << "    push dword " << MemoryOperand(operand, slots, 4) << '\n';
            out << "    push dword " << MemoryOperand(operand, slots) << '\n';
        }

        pushedBytes += 8;
        return;
    }

    LoadValue(out, operand, type, "eax", slots);
    out << "    push eax\n";
    pushedBytes += 4;
}

void X86Generator::StoreResult(
    std::ostringstream& out,
    const std::string& destination,
    const Type& type,
    StackSlots& slots
)
{
    if (destination.empty())
    {
        return;
    }

    int offset = 0;
    if (!slots.Find(StripTemp(destination), offset))
    {
        return;
    }

    const int bits = type.IsValid() ? static_cast<int>(type.Bits) : 32;

    if (bits > 32)
    {
        out << "    mov dword [ebp" << FormatOffset(offset) << "], eax\n";
        out << "    mov dword [ebp" << FormatOffset(offset + 4) << "], edx\n";
        return;
    }

    out << "    mov " << NasmSize(bits) << " [ebp" << FormatOffset(offset) << "], "
        << RegName("eax", bits) << '\n';
}

void X86Generator::StoreToName(
    std::ostringstream& out,
    const std::string& name,
    const Type& type,
    StackSlots& slots
)
{
    const int bits = type.IsValid() ? static_cast<int>(type.Bits) : 32;

    int offset = 0;
    std::string mem;
    if (slots.Find(StripTemp(name), offset))
    {
        mem = "[ebp" + FormatOffset(offset) + "]";
    }
    else
    {
        mem = "[" + DataSymbol(name) + "]";
    }

    if (bits > 32)
    {
        out << "    mov dword " << mem << ", eax\n";
        out << "    mov dword " << MemoryOperand(name, slots, 4) << ", edx\n";
        return;
    }

    out << "    mov " << NasmSize(bits) << " " << mem << ", "
        << RegName("eax", bits) << '\n';
}

void X86Generator::EmitBinary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeUnsigned(32);

    LoadValue(out, instruction.A, type, "eax", slots);
    LoadValue(out, instruction.B, type, "ecx", slots);

    switch (instruction.Binary)
    {
        case Exir::BinaryOp::Add: out << "    add eax, ecx\n"; break;
        case Exir::BinaryOp::Sub: out << "    sub eax, ecx\n"; break;
        case Exir::BinaryOp::Mul: out << "    imul eax, ecx\n"; break;
        case Exir::BinaryOp::And: out << "    and eax, ecx\n"; break;
        case Exir::BinaryOp::Or:  out << "    or eax, ecx\n"; break;
        case Exir::BinaryOp::Xor: out << "    xor eax, ecx\n"; break;
        case Exir::BinaryOp::Shl: out << "    shl eax, cl\n"; break;
        case Exir::BinaryOp::Shr: out << "    shr eax, cl\n"; break;
        case Exir::BinaryOp::Sar: out << "    sar eax, cl\n"; break;
        case Exir::BinaryOp::Div:
        case Exir::BinaryOp::Rem:
        {
            if (type.IsSigned())
            {
                out << "    cdq\n";
            }
            else
            {
                out << "    xor edx, edx\n";
            }

            out << "    " << (type.IsSigned() ? "idiv " : "div ") << "ecx\n";

            if (instruction.Binary == Exir::BinaryOp::Rem)
            {
                out << "    mov eax, edx\n";
            }
            break;
        }
    }

    StoreResult(out, instruction.Dst, type, slots);
}

void X86Generator::EmitWideBinary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeUnsigned(64);

    if (instruction.Binary == Exir::BinaryOp::Div ||
        instruction.Binary == Exir::BinaryOp::Rem)
    {
        const std::string a = WideSource(out, instruction.A, slots, "__wide0");
        const std::string b = WideSource(out, instruction.B, slots, "__wide1");

        int offset = 0;
        slots.Find(StripTemp(instruction.Dst), offset);

        out << "    push dword " << MemoryOperand(b, slots, 4) << '\n';
        out << "    push dword " << MemoryOperand(b, slots) << '\n';
        out << "    push dword " << MemoryOperand(a, slots, 4) << '\n';
        out << "    push dword " << MemoryOperand(a, slots) << '\n';
        out << "    lea eax, [ebp" << FormatOffset(offset) << "]\n";
        out << "    push eax\n";

        if (type.IsSigned())
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

        if (instruction.Binary == Exir::BinaryOp::Rem)
        {
            out << "    mov eax, dword [__emux_rem64]\n";
            out << "    mov edx, dword [__emux_rem64+4]\n";
            StoreResult(out, instruction.Dst, type, slots);
        }

        return;
    }

    if (instruction.Binary == Exir::BinaryOp::Shl ||
        instruction.Binary == Exir::BinaryOp::Shr ||
        instruction.Binary == Exir::BinaryOp::Sar)
    {
        const std::string b = WideSource(out, instruction.B, slots, "__wide1");
        out << "    mov ecx, dword " << MemoryOperand(b, slots) << '\n';
        // O x86 mascara a contagem com 31; para 64 bits a semântica é a
        // mesma do x86_64 (contagem modulo 64).
        out << "    and ecx, 63\n";

        const std::string a = WideSource(out, instruction.A, slots, "__wide0");
        out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
        out << "    mov edx, dword " << MemoryOperand(a, slots, 4) << '\n';

        const std::string low = NewLabel("shift_low");
        const std::string equal = NewLabel("shift_eq");
        const std::string done = NewLabel("shift_done");

        out << "    cmp ecx, 32\n";
        out << "    jb " << low << '\n';
        out << "    je " << equal << '\n';

        // Contagens de 33 a 63.
        out << "    sub ecx, 32\n";

        switch (instruction.Binary)
        {
            case Exir::BinaryOp::Shl:
                out << "    mov edx, eax\n";
                out << "    shl edx, cl\n";
                out << "    xor eax, eax\n";
                break;
            case Exir::BinaryOp::Shr:
                out << "    mov eax, edx\n";
                out << "    shr eax, cl\n";
                out << "    xor edx, edx\n";
                break;
            case Exir::BinaryOp::Sar:
                out << "    mov eax, edx\n";
                out << "    sar eax, cl\n";
                out << "    sar edx, 31\n";
                break;
            default:
                break;
        }

        out << "    jmp " << done << '\n';
        out << equal << ":\n";

        // Contagem exatamente 32.
        switch (instruction.Binary)
        {
            case Exir::BinaryOp::Shl:
                out << "    mov edx, eax\n";
                out << "    xor eax, eax\n";
                break;
            case Exir::BinaryOp::Shr:
                out << "    mov eax, edx\n";
                out << "    xor edx, edx\n";
                break;
            case Exir::BinaryOp::Sar:
                out << "    mov eax, edx\n";
                out << "    sar edx, 31\n";
                break;
            default:
                break;
        }

        out << "    jmp " << done << '\n';
        out << low << ":\n";

        // Contagens de 0 a 31.
        switch (instruction.Binary)
        {
            case Exir::BinaryOp::Shl:
                out << "    shld edx, eax, cl\n";
                out << "    shl eax, cl\n";
                break;
            case Exir::BinaryOp::Shr:
                out << "    shrd eax, edx, cl\n";
                out << "    shr edx, cl\n";
                break;
            case Exir::BinaryOp::Sar:
                out << "    shrd eax, edx, cl\n";
                out << "    sar edx, cl\n";
                break;
            default:
                break;
        }

        out << done << ":\n";

        StoreResult(out, instruction.Dst, type, slots);
        return;
    }

    if (instruction.Binary == Exir::BinaryOp::Mul)
    {
        const std::string a = WideSource(out, instruction.A, slots, "__wide0");
        const std::string b = WideSource(out, instruction.B, slots, "__wide1");

        int carry = 0;
        slots.Find("__wide2", carry);
        int low = 0;
        slots.Find("__wide3", low);

        out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
        out << "    mov ecx, dword " << MemoryOperand(b, slots) << '\n';
        out << "    mul ecx\n";
        out << "    mov dword [ebp" << FormatOffset(carry) << "], edx\n";
        out << "    mov dword [ebp" << FormatOffset(low) << "], eax\n";

        out << "    mov eax, dword " << MemoryOperand(a, slots, 4) << '\n';
        out << "    mul ecx\n";
        out << "    add eax, dword [ebp" << FormatOffset(carry) << "]\n";
        out << "    mov dword [ebp" << FormatOffset(carry) << "], eax\n";

        out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
        out << "    mov ecx, dword " << MemoryOperand(b, slots, 4) << '\n';
        out << "    mul ecx\n";
        out << "    add eax, dword [ebp" << FormatOffset(carry) << "]\n";

        out << "    mov edx, eax\n";
        out << "    mov eax, dword [ebp" << FormatOffset(low) << "]\n";

        StoreResult(out, instruction.Dst, type, slots);
        return;
    }

    const std::string a = WideSource(out, instruction.A, slots, "__wide0");
    const std::string b = WideSource(out, instruction.B, slots, "__wide1");

    switch (instruction.Binary)
    {
        case Exir::BinaryOp::Add:
            out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
            out << "    mov edx, dword " << MemoryOperand(a, slots, 4) << '\n';
            out << "    add eax, dword " << MemoryOperand(b, slots) << '\n';
            out << "    adc edx, dword " << MemoryOperand(b, slots, 4) << '\n';
            break;
        case Exir::BinaryOp::Sub:
            out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
            out << "    mov edx, dword " << MemoryOperand(a, slots, 4) << '\n';
            out << "    sub eax, dword " << MemoryOperand(b, slots) << '\n';
            out << "    sbb edx, dword " << MemoryOperand(b, slots, 4) << '\n';
            break;
        case Exir::BinaryOp::And:
            out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
            out << "    mov edx, dword " << MemoryOperand(a, slots, 4) << '\n';
            out << "    and eax, dword " << MemoryOperand(b, slots) << '\n';
            out << "    and edx, dword " << MemoryOperand(b, slots, 4) << '\n';
            break;
        case Exir::BinaryOp::Or:
            out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
            out << "    mov edx, dword " << MemoryOperand(a, slots, 4) << '\n';
            out << "    or eax, dword " << MemoryOperand(b, slots) << '\n';
            out << "    or edx, dword " << MemoryOperand(b, slots, 4) << '\n';
            break;
        case Exir::BinaryOp::Xor:
            out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
            out << "    mov edx, dword " << MemoryOperand(a, slots, 4) << '\n';
            out << "    xor eax, dword " << MemoryOperand(b, slots) << '\n';
            out << "    xor edx, dword " << MemoryOperand(b, slots, 4) << '\n';
            break;
        default:
            break;
    }

    StoreResult(out, instruction.Dst, type, slots);
}

void X86Generator::EmitUnary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeUnsigned(32);

    LoadValue(out, instruction.A, type, "eax", slots);

    switch (instruction.Unary)
    {
        case Exir::UnaryOp::Neg:
            out << "    neg eax\n";
            break;
        case Exir::UnaryOp::Not:
            out << "    not eax\n";
            break;
        case Exir::UnaryOp::LogicalNot:
            out << "    test eax, eax\n";
            out << "    sete al\n";
            out << "    movzx eax, al\n";
            break;
    }

    StoreResult(out, instruction.Dst, type, slots);
}

void X86Generator::EmitWideUnary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeUnsigned(64);

    const std::string a = WideSource(out, instruction.A, slots, "__wide0");

    switch (instruction.Unary)
    {
        case Exir::UnaryOp::Neg:
            out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
            out << "    mov edx, dword " << MemoryOperand(a, slots, 4) << '\n';
            out << "    not edx\n";
            out << "    neg eax\n";
            out << "    sbb edx, -1\n";
            StoreResult(out, instruction.Dst, type, slots);
            break;

        case Exir::UnaryOp::Not:
            out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
            out << "    mov edx, dword " << MemoryOperand(a, slots, 4) << '\n';
            out << "    not eax\n";
            out << "    not edx\n";
            StoreResult(out, instruction.Dst, type, slots);
            break;

        case Exir::UnaryOp::LogicalNot:
            out << "    mov eax, dword " << MemoryOperand(a, slots) << '\n';
            out << "    or eax, dword " << MemoryOperand(a, slots, 4) << '\n';
            out << "    test eax, eax\n";
            out << "    sete al\n";
            out << "    movzx eax, al\n";
            StoreResult(out, instruction.Dst, type, slots);
            break;
    }
}

void X86Generator::EmitCall(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Abi abi = instruction.Convention;

    std::vector<std::size_t> registerArgs;
    std::vector<std::size_t> stackArgs;

    int registerSlots = 0;
    for (std::size_t i = 0; i < instruction.Args.size(); ++i)
    {
        const Type& type = instruction.Args[i].Ty;
        const int size = type.IsValid() ? static_cast<int>(type.GetByteSize()) : 4;

        if (abi == Abi::Fastcall && registerSlots < 2 && size <= 4)
        {
            registerArgs.push_back(i);
            ++registerSlots;
            continue;
        }

        stackArgs.push_back(i);
    }

    int pushedBytes = 0;
    for (auto it = stackArgs.rbegin(); it != stackArgs.rend(); ++it)
    {
        PushValue(
            out,
            instruction.Args[*it].Value,
            instruction.Args[*it].Ty,
            slots,
            pushedBytes
        );
    }

    for (std::size_t i = 0; i < registerArgs.size(); ++i)
    {
        const Exir::CallArg& argument = instruction.Args[registerArgs[i]];
        LoadValue(out, argument.Value, argument.Ty, i == 0 ? "ecx" : "edx", slots);
    }

    const std::string symbol = CallSymbol(instruction.Target);
    if (!IsDefined(symbol))
    {
        AddExtern(symbol);
    }

    out << "    call " << symbol << '\n';

    if (abi == Abi::Cdecl && pushedBytes > 0)
    {
        out << "    add esp, " << pushedBytes << '\n';
    }

    StoreResult(out, instruction.Dst, instruction.Ty, slots);
}

void X86Generator::EmitInstruction(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    switch (instruction.Op)
    {
        case Exir::Instruction::Kind::Const:
        {
            const Type type = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(32);

            if (type.Bits > 32)
            {
                const auto [lo, hi] = WideLiteralParts(instruction.Literal);
                out << "    mov eax, " << lo << '\n';
                out << "    mov edx, " << hi << '\n';
            }
            else
            {
                LoadValue(out, instruction.Literal, type, "eax", slots);
            }

            StoreResult(out, instruction.Dst, type, slots);
            break;
        }

        case Exir::Instruction::Kind::Cast:
        {
            const Type target = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(32);
            const Type source = instruction.SourceTy.IsValid()
                ? instruction.SourceTy
                : target;

            if (source.Bits <= 32 && target.Bits <= 32)
            {
                LoadValue(out, instruction.A, source, "eax", slots);
            }
            else if (source.Bits <= 32)
            {
                LoadValue(out, instruction.A, source, "eax", slots);
                if (source.IsSigned())
                {
                    out << "    cdq\n";
                }
                else
                {
                    out << "    xor edx, edx\n";
                }
            }
            else if (target.Bits <= 32)
            {
                LoadWide(out, instruction.A, slots);
            }
            else
            {
                LoadWide(out, instruction.A, slots);
            }

            StoreResult(out, instruction.Dst, target, slots);
            break;
        }

        case Exir::Instruction::Kind::Addr:
        {
            int offset = 0;
            if (slots.Find(StripTemp(instruction.A), offset))
            {
                out << "    lea eax, [ebp" << FormatOffset(offset) << "]\n";
            }
            else
            {
                out << "    lea eax, [" << DataSymbol(instruction.A) << "]\n";
            }

            const Type pointerType = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakePointer(Type::MakeUnsigned(8), 32);
            StoreResult(out, instruction.Dst, pointerType, slots);
            break;
        }

        case Exir::Instruction::Kind::Load:
        {
            const Type type = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(32);

            if (IsParameter(instruction.A))
            {
                if (type.Bits > 32)
                {
                    LoadWide(out, instruction.A, slots);
                }
                else
                {
                    LoadValue(out, instruction.A, type, "eax", slots);
                }

                StoreResult(out, instruction.Dst, type, slots);
                break;
            }

            LoadAddress(out, instruction.A, slots);

            const int bits = static_cast<int>(type.Bits);

            if (bits > 32)
            {
                out << "    mov eax, dword [ecx]\n";
                out << "    mov edx, dword [ecx+4]\n";
            }
            else if (bits <= 8)
            {
                out << "    " << (type.IsSigned() ? "movsx " : "movzx ")
                    << "eax, byte [ecx]\n";
            }
            else if (bits <= 16)
            {
                out << "    " << (type.IsSigned() ? "movsx " : "movzx ")
                    << "eax, word [ecx]\n";
            }
            else
            {
                out << "    mov eax, dword [ecx]\n";
            }

            StoreResult(out, instruction.Dst, type, slots);
            break;
        }

        case Exir::Instruction::Kind::Store:
        {
            LoadAddress(out, instruction.A, slots);

            const Type type = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(32);

            if (type.Bits > 32)
            {
                const std::string value = WideSource(out, instruction.B, slots, "__wide0");
                out << "    mov eax, dword " << MemoryOperand(value, slots) << '\n';
                out << "    mov edx, dword " << MemoryOperand(value, slots, 4) << '\n';
                out << "    mov dword [ecx], eax\n";
                out << "    mov dword [ecx+4], edx\n";
            }
            else
            {
                LoadValue(out, instruction.B, type, "eax", slots);
                out << "    mov " << NasmSize(type.Bits) << " [ecx], "
                    << RegName("eax", type.Bits) << '\n';
            }
            break;
        }

        case Exir::Instruction::Kind::StoreVar:
        {
            const Type type = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(32);

            if (type.Bits > 32)
            {
                const std::string value = WideSource(out, instruction.B, slots, "__wide0");
                out << "    mov eax, dword " << MemoryOperand(value, slots) << '\n';
                out << "    mov edx, dword " << MemoryOperand(value, slots, 4) << '\n';
                StoreToName(out, instruction.A, type, slots);
            }
            else
            {
                LoadValue(out, instruction.B, type, "eax", slots);
                StoreToName(out, instruction.A, type, slots);
            }
            break;
        }

        case Exir::Instruction::Kind::Binary:
            if (instruction.Ty.IsValid() && instruction.Ty.Bits > 32)
            {
                EmitWideBinary(out, instruction, slots);
            }
            else
            {
                EmitBinary(out, instruction, slots);
            }
            break;

        case Exir::Instruction::Kind::Unary:
            if (instruction.Ty.IsValid() && instruction.Ty.Bits > 32)
            {
                EmitWideUnary(out, instruction, slots);
            }
            else
            {
                EmitUnary(out, instruction, slots);
            }
            break;

        case Exir::Instruction::Kind::Call:
            EmitCall(out, instruction, slots);
            break;

        case Exir::Instruction::Kind::Return:
        {
            if (!instruction.A.empty() && instruction.Ty.IsValid() && !instruction.Ty.IsVoid())
            {
                if (instruction.Ty.Bits > 32)
                {
                    LoadWide(out, instruction.A, slots);
                }
                else
                {
                    LoadValue(out, instruction.A, instruction.Ty, "eax", slots);
                }
            }

            out << "    leave\n";
            break;
        }

        case Exir::Instruction::Kind::Asm:
            out << instruction.Literal << '\n';
            break;
    }
}

void X86Generator::EmitFunction(
    std::ostringstream& text,
    const Exir::Function& function
)
{
    StackSlots slots;

    for (const Exir::Parameter& parameter : function.Params)
    {
        slots.Allocate(parameter.Name);
    }

    slots.Allocate("__wide0");
    slots.Allocate("__wide1");
    slots.Allocate("__wide2");
    slots.Allocate("__wide3");

    for (const Exir::Instruction& instruction : function.Body)
    {
        if (!instruction.Dst.empty())
        {
            slots.Allocate(StripTemp(instruction.Dst));
        }
    }

    const int frame = slots.Bytes();
    const Abi abi = function.Convention;

    text << FunctionSymbol(function) << ":\n";
    for (const std::string& alias : ExportAliases(function.Name))
    {
        text << GlobalSymbol(alias) << ":\n";
    }

    text << "    push ebp\n";
    text << "    mov ebp, esp\n";
    if (frame > 0)
    {
        text << "    sub esp, " << frame << '\n';
    }

    int registerSlots = 0;
    int stackOffset = 8;

    for (const Exir::Parameter& parameter : function.Params)
    {
        int offset = 0;
        slots.Find(parameter.Name, offset);

        const Type type = parameter.Ty;
        const int size = type.IsValid() ? static_cast<int>(type.GetByteSize()) : 4;
        const bool inRegister = abi == Abi::Fastcall && registerSlots < 2 && size <= 4;

        if (inRegister)
        {
            text << "    mov dword [ebp" << FormatOffset(offset) << "], "
                << (registerSlots == 0 ? "ecx" : "edx") << '\n';
            ++registerSlots;
            continue;
        }

        if (type.IsValid() && type.Bits > 32)
        {
            text << "    mov eax, dword [ebp+" << stackOffset << "]\n";
            text << "    mov dword [ebp" << FormatOffset(offset) << "], eax\n";
            text << "    mov eax, dword [ebp+" << (stackOffset + 4) << "]\n";
            text << "    mov dword [ebp" << FormatOffset(offset + 4) << "], eax\n";
            stackOffset += 8;
        }
        else
        {
            text << "    mov eax, dword [ebp+" << stackOffset << "]\n";
            text << "    mov dword [ebp" << FormatOffset(offset) << "], eax\n";
            stackOffset += 4;
        }
    }

    for (const Exir::Instruction& instruction : function.Body)
    {
        EmitInstruction(text, instruction, slots);

        if (instruction.Op == Exir::Instruction::Kind::Return)
        {
            if (abi != Abi::Cdecl)
            {
                // 'leave' já foi emitido; completa com a limpeza da pilha.
                text << "    ret " << function.StackBytes << '\n';
            }
            else
            {
                text << "    ret\n";
            }
        }
    }

    if (function.Body.empty() ||
        function.Body.back().Op != Exir::Instruction::Kind::Return)
    {
        text << "    leave\n";
        if (abi != Abi::Cdecl)
        {
            text << "    ret " << function.StackBytes << '\n';
        }
        else
        {
            text << "    ret\n";
        }
    }

    text << '\n';
}

} // namespace Emux
