#include "Emux/Generator/X64Generator.hpp"
#include "Emux/Utilities/BigInt.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Emux
{

namespace
{

bool IsWideInteger(const Type& type)
{
    return type.IsInteger() && type.Bits > 64;
}

int WideBytes(const Type& type)
{
    return static_cast<int>(type.GetByteSize());
}

std::string RegName(const std::string& reg64, int bits)
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
        { "rbx", "ebx", "bx", "bl" },
        { "rcx", "ecx", "cx", "cl" },
        { "rdx", "edx", "dx", "dl" },
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

    return reg64;
}

}

X64Generator::X64Generator()
    : NasmGeneratorBase(Architecture::X86_64)
{
}

std::string X64Generator::FunctionSymbol(const Exir::Function& function) const
{
    return function.Name;
}

std::string X64Generator::CallSymbol(const std::string& target) const
{
    // x64 usa o símbolo nativo diretamente (sem decoração por convenção).
    if (const Exir::Import* import = FindImport(target))
    {
        return import->Symbol;
    }

    return target;
}

std::string X64Generator::DataSymbol(std::string_view name)
{
    const std::string symbol = GlobalSymbol(name);
    if (!IsDefined(symbol))
    {
        AddExtern(symbol);
    }

    return symbol;
}

std::string X64Generator::NewLabel(std::string_view prefix)
{
    return ".L" + std::string(prefix) + "_" + std::to_string(m_LabelCounter++);
}

std::string X64Generator::WideAddress(
    const std::string& operand,
    StackSlots& slots,
    const std::string& reg
)
{
    if (IsLiteralOperand(operand))
    {
        throw std::runtime_error(
            "Literal largo nao materializado na Exir: '" + operand + "'"
        );
    }

    int offset = 0;
    std::ostringstream line;

    if (slots.Find(StripTemp(operand), offset))
    {
        line << "    lea " << reg << ", [rbp" << FormatOffset(offset) << "]\n";
    }
    else
    {
        line << "    lea " << reg << ", [rel " << DataSymbol(operand) << "]\n";
    }

    return line.str();
}

std::string X64Generator::MemoryOperand(const std::string& operand, StackSlots& slots)
{
    int offset = 0;
    if (slots.Find(StripTemp(operand), offset))
    {
        return "[rbp" + FormatOffset(offset) + "]";
    }

    return "[rel " + DataSymbol(operand) + "]";
}

void X64Generator::EmitImmediate(
    std::ostringstream& out,
    const std::string& literal,
    const std::string& reg64
)
{
    bool wide = reg64 != "rax";

    if (wide)
    {
        try
        {
            const long long value = std::stoll(literal, nullptr, 0);
            wide = value < std::numeric_limits<int>::min() ||
                   value > std::numeric_limits<int>::max();
        }
        catch (...)
        {
            wide = true;
        }
    }

    if (wide)
    {
        out << "    mov rax, " << literal << '\n';
        if (reg64 != "rax")
        {
            out << "    mov " << reg64 << ", rax\n";
        }
        return;
    }

    out << "    mov " << reg64 << ", " << literal << '\n';
}

void X64Generator::LoadValue(
    std::ostringstream& out,
    const std::string& operand,
    const Type& type,
    const std::string& reg64,
    StackSlots& slots
)
{
    if (IsLiteralOperand(operand))
    {
        EmitImmediate(out, operand, reg64);
        return;
    }

    const int bits = type.IsValid() ? static_cast<int>(type.Bits) : 32;
    const std::string mem = MemoryOperand(operand, slots);

    if (bits <= 8)
    {
        out << "    " << (type.IsSigned() ? "movsx " : "movzx ")
            << reg64 << ", byte " << mem << '\n';
        return;
    }

    if (bits <= 16)
    {
        out << "    " << (type.IsSigned() ? "movsx " : "movzx ")
            << reg64 << ", word " << mem << '\n';
        return;
    }

    if (bits <= 32)
    {
        if (type.IsSigned())
        {
            out << "    movsxd " << reg64 << ", dword " << mem << '\n';
        }
        else
        {
            out << "    mov " << RegName(reg64, 32) << ", dword " << mem << '\n';
        }
        return;
    }

    out << "    mov " << reg64 << ", qword " << mem << '\n';
}

void X64Generator::LoadAddress(
    std::ostringstream& out,
    const std::string& operand,
    StackSlots& slots
)
{
    int offset = 0;

    if (IsLiteralOperand(operand))
    {
        EmitImmediate(out, operand, "r11");
    }
    else if (slots.Find(StripTemp(operand), offset))
    {
        out << "    mov r11, [rbp" << FormatOffset(offset) << "]\n";
    }
    else
    {
        out << "    lea r11, [rel " << DataSymbol(operand) << "]\n";
    }
}

void X64Generator::StoreResult(
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
    out << "    mov " << NasmSize(bits) << " [rbp" << FormatOffset(offset) << "], "
        << RegName("rax", bits) << '\n';
}

void X64Generator::StoreToName(
    std::ostringstream& out,
    const std::string& name,
    const Type& type,
    StackSlots& slots
)
{
    const int bits = type.IsValid() ? static_cast<int>(type.Bits) : 32;

    int offset = 0;
    const std::string mem = slots.Find(StripTemp(name), offset)
        ? "[rbp" + FormatOffset(offset) + "]"
        : "[rel " + DataSymbol(name) + "]";

    out << "    mov " << NasmSize(bits) << " " << mem << ", "
        << RegName("rax", bits) << '\n';
}

void X64Generator::EmitWideConst(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty;
    const int bytes = WideBytes(type);
    const std::string& text = instruction.Literal;

    const bool isHex =
        text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X');

    auto value = BigInt::Parse(text, isHex ? 16 : 10);

    if (!value.has_value())
    {
        throw std::runtime_error("Literal invalido na Exir: '" + text + "'");
    }

    const std::vector<std::uint8_t> data = value
        ->WrappedTo(type.Bits, type.IsSigned())
        .ToLittleEndianBytes(static_cast<std::size_t>(bytes));

    out << WideAddress(instruction.Dst, slots, "r11");

    int index = 0;

    while (index + 8 <= bytes)
    {
        std::uint64_t chunk = 0;
        for (int b = 0; b < 8; ++b)
        {
            chunk |= static_cast<std::uint64_t>(data[static_cast<std::size_t>(index + b)])
                << (8 * b);
        }

        out << "    mov rax, " << chunk << '\n';
        out << "    mov [r11+" << index << "], rax\n";
        index += 8;
    }

    while (index < bytes)
    {
        out << "    mov byte [r11+" << index << "], "
            << static_cast<unsigned>(data[static_cast<std::size_t>(index)]) << '\n';
        ++index;
    }
}

void X64Generator::EmitWideCast(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type destination = instruction.Ty;
    const Type source = instruction.SourceTy.IsValid()
        ? instruction.SourceTy
        : Type::MakeUnsigned(64);

    const bool sourceWide = IsWideInteger(source);

    if (!IsWideInteger(destination))
    {
        // Largo -> pequeno: truncar aos bytes baixos do valor.
        out << WideAddress(instruction.A, slots, "r11");

        const int bits = destination.IsValid()
            ? static_cast<int>(destination.Bits)
            : 32;

        if (bits <= 8)
        {
            out << "    " << (destination.IsSigned() ? "movsx " : "movzx ")
                << "eax, byte [r11]\n";
        }
        else if (bits <= 16)
        {
            out << "    " << (destination.IsSigned() ? "movsx " : "movzx ")
                << "eax, word [r11]\n";
        }
        else if (bits <= 32)
        {
            out << "    mov eax, dword [r11]\n";
        }
        else
        {
            out << "    mov rax, qword [r11]\n";
        }

        StoreResult(out, instruction.Dst, destination, slots);
        return;
    }

    const int destinationBytes = WideBytes(destination);

    if (!sourceWide)
    {
        // Pequeno -> largo: guardar o valor estendido a 64 bits e preencher
        // o resto com 0 ou com o bit de sinal.
        LoadValue(out, instruction.A, source, "rax", slots);
        out << WideAddress(instruction.Dst, slots, "r11");
        out << "    mov [r11], rax\n";
        out << "    mov rdx, rax\n";
        out << (source.IsSigned() ? "    sar rdx, 63\n" : "    xor edx, edx\n");

        const std::string loop = NewLabel("wext");
        const std::string done = NewLabel("wextd");

        out << "    mov ecx, 8\n";
        out << loop << ":\n";
        out << "    cmp ecx, " << destinationBytes << '\n';
        out << "    jae " << done << '\n';
        out << "    mov [r11+rcx], dl\n";
        out << "    inc ecx\n";
        out << "    jmp " << loop << '\n';
        out << done << ":\n";
        return;
    }

    // Largo -> largo: copiar o menor numero de bytes e estender o sinal
    // quando o destino for mais largo.
    const int sourceBytes = WideBytes(source);
    const int copyBytes = sourceBytes < destinationBytes
        ? sourceBytes
        : destinationBytes;

    out << WideAddress(instruction.A, slots, "rdx");
    out << WideAddress(instruction.Dst, slots, "rcx");
    out << "    mov r8d, " << copyBytes << '\n';
    out << "    call __emux_wcopy\n";

    if (sourceBytes < destinationBytes)
    {
        out << WideAddress(instruction.A, slots, "r10");
        out << "    mov al, [r10+" << (sourceBytes - 1) << "]\n";
        out << (source.IsSigned() ? "    sar al, 7\n" : "    xor al, al\n");

        out << WideAddress(instruction.Dst, slots, "r11");

        const std::string loop = NewLabel("wsen");
        const std::string done = NewLabel("wsend");

        out << "    mov ecx, " << sourceBytes << '\n';
        out << loop << ":\n";
        out << "    cmp ecx, " << destinationBytes << '\n';
        out << "    jae " << done << '\n';
        out << "    mov [r11+rcx], al\n";
        out << "    inc ecx\n";
        out << "    jmp " << loop << '\n';
        out << done << ":\n";
    }
}

void X64Generator::EmitShiftCount(
    std::ostringstream& out,
    const std::string& operand,
    int bytes,
    StackSlots& slots
)
{
    // A contagem é um valor largo; só os 32 bits baixos contam. Se houver
    // bytes altos não nulos, a contagem excede a largura total.
    out << WideAddress(operand, slots, "r11");
    out << "    mov r8d, [r11]\n";

    const std::string check = NewLabel("wcnt");
    const std::string huge = NewLabel("wcnth");
    const std::string done = NewLabel("wcntd");

    out << "    mov r10d, 4\n";
    out << check << ":\n";
    out << "    cmp r10d, " << bytes << '\n';
    out << "    jae " << done << '\n';
    out << "    cmp byte [r11+r10], 0\n";
    out << "    jne " << huge << '\n';
    out << "    inc r10d\n";
    out << "    jmp " << check << '\n';
    out << huge << ":\n";
    out << "    mov r8d, -1\n";
    out << done << ":\n";
}

void X64Generator::EmitWideBinary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty;
    const int bytes = WideBytes(type);

    switch (instruction.Binary)
    {
        case Exir::BinaryOp::Shl:
        case Exir::BinaryOp::Shr:
        case Exir::BinaryOp::Sar:
            out << WideAddress(instruction.A, slots, "rdx");
            out << WideAddress(instruction.Dst, slots, "rcx");
            EmitShiftCount(out, instruction.B, bytes, slots);
            out << "    mov r9d, " << bytes << '\n';
            out << "    call "
                << (instruction.Binary == Exir::BinaryOp::Shl
                        ? "__emux_wshl"
                        : instruction.Binary == Exir::BinaryOp::Shr
                            ? "__emux_wshr"
                            : "__emux_wsar")
                << '\n';
            break;

        case Exir::BinaryOp::Div:
            out << WideAddress(instruction.A, slots, "rdx");
            out << WideAddress(instruction.B, slots, "r8");
            out << WideAddress(instruction.Dst, slots, "rcx");
            out << "    mov r9d, " << bytes << '\n';
            out << "    call "
                << (type.IsSigned() ? "__emux_wsdiv" : "__emux_wudiv")
                << '\n';
            break;

        case Exir::BinaryOp::Rem:
            if (type.IsSigned())
            {
                out << WideAddress(instruction.A, slots, "rdx");
                out << WideAddress(instruction.B, slots, "r8");
                out << WideAddress(instruction.Dst, slots, "rcx");
                out << "    mov r9d, " << bytes << '\n';
                out << "    call __emux_wsrem\n";
            }
            else
            {
                // __emux_wudivmod escreve o quociente e depois o resto nos
                // ponteiros indicados; usar Dst nos dois e seguro porque a
                // copia do resto acontece por ultimo.
                out << WideAddress(instruction.A, slots, "r8");
                out << WideAddress(instruction.B, slots, "r9");
                out << WideAddress(instruction.Dst, slots, "rcx");
                out << "    mov rdx, rcx\n";
                out << "    mov r10d, " << bytes << '\n';
                out << "    call __emux_wudivmod\n";
            }
            break;

        default:
        {
            const char* helper = "__emux_wadd";

            switch (instruction.Binary)
            {
                case Exir::BinaryOp::Add: helper = "__emux_wadd"; break;
                case Exir::BinaryOp::Sub: helper = "__emux_wsub"; break;
                case Exir::BinaryOp::Mul: helper = "__emux_wmul"; break;
                case Exir::BinaryOp::And: helper = "__emux_wand"; break;
                case Exir::BinaryOp::Or:  helper = "__emux_wor"; break;
                case Exir::BinaryOp::Xor: helper = "__emux_wxor"; break;
                default:
                    throw std::runtime_error(
                        "Operacao binaria larga nao suportada pelo backend x64");
            }

            out << WideAddress(instruction.A, slots, "rdx");
            out << WideAddress(instruction.B, slots, "r8");
            out << WideAddress(instruction.Dst, slots, "rcx");
            out << "    mov r9d, " << bytes << '\n';
            out << "    call " << helper << '\n';
            break;
        }
    }
}

void X64Generator::EmitWideUnary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty;
    const int bytes = WideBytes(type);

    const char* helper = nullptr;

    switch (instruction.Unary)
    {
        case Exir::UnaryOp::Neg: helper = "__emux_wneg"; break;
        case Exir::UnaryOp::Not: helper = "__emux_wnot"; break;
        case Exir::UnaryOp::LogicalNot: helper = "__emux_wlnot"; break;
    }

    out << WideAddress(instruction.A, slots, "rdx");
    out << WideAddress(instruction.Dst, slots, "rcx");
    out << "    mov r8d, " << bytes << '\n';
    out << "    call " << helper << '\n';
}

void X64Generator::EmitBinary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeUnsigned(32);

    if (IsWideInteger(type))
    {
        EmitWideBinary(out, instruction, slots);
        return;
    }

    const int bits = type.Bits > 32 ? 64 : 32;
    const std::string acc = bits == 64 ? "rax" : "eax";
    const std::string rhs = bits == 64 ? "rcx" : "ecx";

    LoadValue(out, instruction.A, type, "rax", slots);
    LoadValue(out, instruction.B, type, "rcx", slots);

    switch (instruction.Binary)
    {
        case Exir::BinaryOp::Add: out << "    add " << acc << ", " << rhs << '\n'; break;
        case Exir::BinaryOp::Sub: out << "    sub " << acc << ", " << rhs << '\n'; break;
        case Exir::BinaryOp::Mul: out << "    imul " << acc << ", " << rhs << '\n'; break;
        case Exir::BinaryOp::And: out << "    and " << acc << ", " << rhs << '\n'; break;
        case Exir::BinaryOp::Or:  out << "    or " << acc << ", " << rhs << '\n'; break;
        case Exir::BinaryOp::Xor: out << "    xor " << acc << ", " << rhs << '\n'; break;
        case Exir::BinaryOp::Shl: out << "    shl " << acc << ", cl\n"; break;
        case Exir::BinaryOp::Shr: out << "    shr " << acc << ", cl\n"; break;
        case Exir::BinaryOp::Sar: out << "    sar " << acc << ", cl\n"; break;
        case Exir::BinaryOp::Div:
        case Exir::BinaryOp::Rem:
        {
            if (type.IsSigned())
            {
                out << "    " << (bits == 64 ? "cqo" : "cdq") << '\n';
            }
            else
            {
                out << "    xor edx, edx\n";
            }

            out << "    " << (type.IsSigned() ? "idiv " : "div ") << rhs << '\n';

            if (instruction.Binary == Exir::BinaryOp::Rem)
            {
                out << "    mov " << acc << ", " << (bits == 64 ? "rdx" : "edx") << '\n';
            }
            break;
        }
    }

    StoreResult(out, instruction.Dst, type, slots);
}

void X64Generator::EmitUnary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeUnsigned(32);

    if (IsWideInteger(type))
    {
        EmitWideUnary(out, instruction, slots);
        return;
    }

    const int bits = type.Bits > 32 ? 64 : 32;
    const std::string acc = bits == 64 ? "rax" : "eax";

    LoadValue(out, instruction.A, type, "rax", slots);

    switch (instruction.Unary)
    {
        case Exir::UnaryOp::Neg:
            out << "    neg " << acc << '\n';
            break;
        case Exir::UnaryOp::Not:
            out << "    not " << acc << '\n';
            break;
        case Exir::UnaryOp::LogicalNot:
            out << "    test " << acc << ", " << acc << '\n';
            out << "    sete al\n";
            out << "    movzx eax, al\n";
            break;
    }

    StoreResult(out, instruction.Dst, type, slots);
}

void X64Generator::EmitCall(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    static const char* const argRegisters[4] = { "rcx", "rdx", "r8", "r9" };

    const std::string symbol = CallSymbol(instruction.Target);
    if (!IsDefined(symbol))
    {
        AddExtern(symbol);
    }

    const std::size_t count = instruction.Args.size();
    const std::size_t registerCount = count < 4 ? count : 4;

    for (std::size_t i = 0; i < registerCount; ++i)
    {
        LoadValue(
            out,
            instruction.Args[i].Value,
            instruction.Args[i].Ty,
            argRegisters[i],
            slots
        );
    }

    const std::size_t stackCount = count > 4 ? count - 4 : 0;
    int reserved = 0;

    if (stackCount > 0)
    {
        // 32 bytes de shadow space + 8 por argumento, alinhado a 16.
        reserved = 32 + static_cast<int>(stackCount) * 8;
        reserved = (reserved + 15) & ~15;

        out << "    sub rsp, " << reserved << '\n';

        for (std::size_t i = 0; i < stackCount; ++i)
        {
            const Exir::CallArg& argument = instruction.Args[4 + i];
            LoadValue(out, argument.Value, argument.Ty, "rax", slots);
            out << "    mov [rsp+" << (32 + static_cast<int>(i) * 8) << "], rax\n";
        }
    }

    out << "    call " << symbol << '\n';

    if (reserved > 0)
    {
        out << "    add rsp, " << reserved << '\n';
    }

    StoreResult(out, instruction.Dst, instruction.Ty, slots);
}

void X64Generator::EmitTrunc(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid()
        ? instruction.Ty
        : Type::MakeUnsigned(32);
    const int bits = static_cast<int>(type.Bits);

    if (type.IsFloat() || type.IsFixed())
    {
        throw std::runtime_error(
            "trunc de '" + type.ToString() + "' ainda nao suportado em runtime."
        );
    }

    if (IsWideInteger(type))
    {
        const int bytes = WideBytes(type);

        if (IsLiteralOperand(instruction.A))
        {
            Exir::Instruction literal = instruction;
            literal.Literal = instruction.A;
            EmitWideConst(out, literal, slots);
        }
        else
        {
            out << WideAddress(instruction.A, slots, "rdx");
            out << WideAddress(instruction.Dst, slots, "rcx");
            out << "    mov r8d, " << bytes << '\n';
            out << "    call __emux_wcopy\n";
        }

        const int top = bits % 8;
        if (top == 0)
        {
            return;
        }

        const int mask = (1 << top) - 1;
        out << WideAddress(instruction.Dst, slots, "r11");
        out << "    and byte [r11+" << (bytes - 1) << "], " << mask << '\n';

        if (type.IsSigned())
        {
            const std::string done = NewLabel("truncd");
            out << "    test byte [r11+" << (bytes - 1) << "], "
                << (1 << (top - 1)) << '\n';
            out << "    jz " << done << '\n';
            out << "    or byte [r11+" << (bytes - 1) << "], "
                << (~mask & 0xFF) << '\n';
            out << done << ":\n";
        }

        return;
    }

    LoadValue(out, instruction.A, type, "rax", slots);

    // O vaivem shl/sar (ou shl/shr) isola os Bits uteis mesmo quando a
    // largura e multipla de 8 mas nao coincide com o registo (u24, u40, ...).
    if (bits < 64)
    {
        const int shift = 64 - bits;
        out << "    shl rax, " << shift << '\n';
        out << (type.IsSigned() ? "    sar rax, " : "    shr rax, ") << shift << '\n';
    }

    StoreResult(out, instruction.Dst, type, slots);
}

void X64Generator::EmitInstruction(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    switch (instruction.Op)
    {
        case Exir::Instruction::Kind::Const:
            if (IsWideInteger(instruction.Ty))
            {
                EmitWideConst(out, instruction, slots);
                break;
            }
            EmitImmediate(out, instruction.Literal, "rax");
            StoreResult(out, instruction.Dst, instruction.Ty, slots);
            break;

        case Exir::Instruction::Kind::Cast:
            if (IsWideInteger(instruction.Ty) ||
                IsWideInteger(instruction.SourceTy))
            {
                EmitWideCast(out, instruction, slots);
                break;
            }
            LoadValue(out, instruction.A, instruction.SourceTy, "rax", slots);
            StoreResult(out, instruction.Dst, instruction.Ty, slots);
            break;

        case Exir::Instruction::Kind::Trunc:
            EmitTrunc(out, instruction, slots);
            break;

        case Exir::Instruction::Kind::Addr:
        {
            int offset = 0;
            if (slots.Find(StripTemp(instruction.A), offset))
            {
                out << "    lea rax, [rbp" << FormatOffset(offset) << "]\n";
            }
            else
            {
                out << "    lea rax, [rel " << DataSymbol(instruction.A) << "]\n";
            }

            const Type pointerType = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakePointer(Type::MakeUnsigned(8), 64);
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
                LoadValue(out, instruction.A, type, "rax", slots);
                StoreResult(out, instruction.Dst, type, slots);
                break;
            }

            if (IsWideInteger(type))
            {
                LoadAddress(out, instruction.A, slots);
                out << "    mov rdx, r11\n";
                out << WideAddress(instruction.Dst, slots, "rcx");
                out << "    mov r8d, " << WideBytes(type) << '\n';
                out << "    call __emux_wcopy\n";
                break;
            }

            LoadAddress(out, instruction.A, slots);

            const int bits = static_cast<int>(type.Bits);

            if (bits <= 8)
            {
                out << "    " << (type.IsSigned() ? "movsx " : "movzx ")
                    << "eax, byte [r11]\n";
            }
            else if (bits <= 16)
            {
                out << "    " << (type.IsSigned() ? "movsx " : "movzx ")
                    << "eax, word [r11]\n";
            }
            else if (bits <= 32)
            {
                out << "    mov eax, dword [r11]\n";
            }
            else
            {
                out << "    mov rax, qword [r11]\n";
            }

            StoreResult(out, instruction.Dst, type, slots);
            break;
        }

        case Exir::Instruction::Kind::Store:
        {
            const Type type = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(32);

            LoadAddress(out, instruction.A, slots);

            if (IsWideInteger(type))
            {
                out << "    mov rcx, r11\n";
                out << WideAddress(instruction.B, slots, "rdx");
                out << "    mov r8d, " << WideBytes(type) << '\n';
                out << "    call __emux_wcopy\n";
                break;
            }

            LoadValue(out, instruction.B, type, "rax", slots);
            out << "    mov " << NasmSize(type.Bits) << " [r11], "
                << RegName("rax", type.Bits) << '\n';
            break;
        }

        case Exir::Instruction::Kind::StoreVar:
        {
            const Type type = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(32);

            if (IsWideInteger(type))
            {
                out << WideAddress(instruction.A, slots, "rcx");
                out << WideAddress(instruction.B, slots, "rdx");
                out << "    mov r8d, " << WideBytes(type) << '\n';
                out << "    call __emux_wcopy\n";
                break;
            }

            LoadValue(out, instruction.B, type, "rax", slots);
            StoreToName(out, instruction.A, type, slots);
            break;
        }

        case Exir::Instruction::Kind::Binary:
            EmitBinary(out, instruction, slots);
            break;

        case Exir::Instruction::Kind::Unary:
            EmitUnary(out, instruction, slots);
            break;

        case Exir::Instruction::Kind::Call:
            EmitCall(out, instruction, slots);
            break;

        case Exir::Instruction::Kind::Return:
            if (!instruction.A.empty() && instruction.Ty.IsValid() && !instruction.Ty.IsVoid())
            {
                LoadValue(out, instruction.A, instruction.Ty, "rax", slots);
            }
            out << "    leave\n    ret\n";
            break;

        case Exir::Instruction::Kind::Copy:
            // rsi/rdi sao nao-volateis no Win64: preservar em volta do rep.
            out << "    push rsi\n";
            out << "    push rdi\n";
            out << "    lea rdi, [rel " << DataSymbol(instruction.A) << "]\n";
            out << "    lea rsi, [rel " << DataSymbol(instruction.B) << "]\n";
            out << "    mov rcx, " << instruction.Bytes << '\n';
            out << "    rep movsb\n";
            out << "    pop rdi\n";
            out << "    pop rsi\n";
            break;

        case Exir::Instruction::Kind::Asm:
            out << instruction.Literal << '\n';
            break;
    }
}

void X64Generator::EmitFunction(
    std::ostringstream& text,
    const Exir::Function& function
)
{
    StackSlots slots;

    const auto SlotBytes = [](const Type& type)
    {
        return type.IsValid() && IsWideInteger(type) ? WideBytes(type) : 8;
    };

    for (const Exir::Parameter& parameter : function.Params)
    {
        slots.Allocate(parameter.Name, SlotBytes(parameter.Ty));
    }

    for (const Exir::Instruction& instruction : function.Body)
    {
        if (!instruction.Dst.empty())
        {
            slots.Allocate(StripTemp(instruction.Dst), SlotBytes(instruction.Ty));
        }
    }

    int frame = slots.Bytes() + 32;
    frame = (frame + 15) & ~15;

    text << FunctionSymbol(function) << ":\n";
    for (const std::string& alias : ExportAliases(function.Name))
    {
        text << GlobalSymbol(alias) << ":\n";
    }

    text << "    push rbp\n";
    text << "    mov rbp, rsp\n";
    if (frame > 0)
    {
        text << "    sub rsp, " << frame << '\n';
    }

    static const char* const argRegisters[4] = { "rcx", "rdx", "r8", "r9" };

    for (std::size_t i = 0; i < function.Params.size(); ++i)
    {
        int offset = 0;
        slots.Find(function.Params[i].Name, offset);

        if (i < 4)
        {
            text << "    mov [rbp" << FormatOffset(offset) << "], "
                << argRegisters[i] << '\n';
        }
        else
        {
            const int source = 48 + static_cast<int>(i - 4) * 8;
            text << "    mov rax, [rbp+" << source << "]\n";
            text << "    mov [rbp" << FormatOffset(offset) << "], rax\n";
        }
    }

    for (const Exir::Instruction& instruction : function.Body)
    {
        EmitInstruction(text, instruction, slots);
    }

    if (function.Body.empty() ||
        function.Body.back().Op != Exir::Instruction::Kind::Return)
    {
        text << "    leave\n    ret\n";
    }

    text << '\n';
}

// Rotinas multi-limb. Convencoes internas (sem ABI do sistema):
//   wcopy:            rcx=dst, rdx=origem, r8d=n
//   add/sub/and/or/xor/mul: rcx=dst, rdx=a, r8=b, r9d=n
//   neg/not/lnot:     rcx=dst, rdx=a, r8d=n
//   shl/shr/sar:      rcx=dst, rdx=a, r8d=contagem, r9d=n
//   wudiv/wudivmod:   ver abaixo; wsdiv e a variante com sinal.
// n e sempre o numero de bytes do tipo (multiplo de 8 ou nao), >= 9.
void X64Generator::EmitArchHelpers(std::ostringstream& text)
{
    text << R"ASM(
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
