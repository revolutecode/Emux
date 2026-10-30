#include "Emux/Generator/X64Generator.hpp"

#include <limits>

namespace Emux
{

namespace
{

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

void X64Generator::EmitBinary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeUnsigned(32);
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

void X64Generator::EmitInstruction(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    switch (instruction.Op)
    {
        case Exir::Instruction::Kind::Const:
            EmitImmediate(out, instruction.Literal, "rax");
            StoreResult(out, instruction.Dst, instruction.Ty, slots);
            break;

        case Exir::Instruction::Kind::Cast:
            LoadValue(out, instruction.A, instruction.SourceTy, "rax", slots);
            StoreResult(out, instruction.Dst, instruction.Ty, slots);
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
            LoadAddress(out, instruction.A, slots);

            const Type type = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(32);
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

    for (const Exir::Parameter& parameter : function.Params)
    {
        slots.Allocate(parameter.Name);
    }

    for (const Exir::Instruction& instruction : function.Body)
    {
        if (!instruction.Dst.empty())
        {
            slots.Allocate(StripTemp(instruction.Dst));
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

} // namespace Emux
