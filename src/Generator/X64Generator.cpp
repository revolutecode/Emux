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

// Numerico que nao cabe num registador: inteiro, float ou ponto fixo.
bool IsWideNumeric(const Type& type)
{
    return type.IsNumeric() && type.GetByteSize() > 8;
}

// O ponto fixo tem a representacao de um inteiro com sinal.
bool SignedLike(const Type& type)
{
    const Type repr = type.Representation();
    return repr.IsSigned() || repr.IsFixed();
}

int WideBytes(const Type& type)
{
    return static_cast<int>(type.GetByteSize());
}

int NumericBytes(const Type& type)
{
    return static_cast<int>(type.Representation().GetByteSize());
}

bool IsStandardIntegerWidth(int bits)
{
    return bits == 8 || bits == 16 || bits == 32 || bits == 64;
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

    std::string out = "0x";
    std::string body(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        body[static_cast<std::size_t>(i)] = digits[value & 0xFu];
        value >>= 4;
    }

    // Remove zeros a esquerda mantendo pelo menos um digito.
    std::size_t start = 0;
    while (start + 1 < body.size() && body[start] == '0')
    {
        ++start;
    }

    return out + body.substr(start);
}

bool IsHexLiteral(const std::string& text)
{
    std::size_t index = 0;
    if (!text.empty() && (text[0] == '-' || text[0] == '+'))
    {
        index = 1;
    }

    return text.size() > index + 2 && text[index] == '0' &&
        (text[index + 1] == 'x' || text[index + 1] == 'X');
}

// Bytes de armazenamento (complemento a dois) de um literal numerico.
std::uint64_t LiteralChunk(const std::string& text)
{
    auto value = BigInt::Parse(text, IsHexLiteral(text) ? 16 : 10);
    if (!value.has_value())
    {
        throw std::runtime_error("Literal invalido na Exir: '" + text + "'");
    }

    // ToUInt64 devolve a magnitude; os bytes little-endian ja carregam o
    // complemento a dois para valores negativos.
    const std::vector<std::uint8_t> bytes =
        value->WrappedTo(64, true).ToLittleEndianBytes(8);

    std::uint64_t chunk = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        chunk |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    }

    return chunk;
}

// Acrescenta um deslocamento a um operando de memoria ja entre parenteses.
std::string MemWithOffset(const std::string& mem, int offset)
{
    if (offset == 0 || mem.empty() || mem.back() != ']')
    {
        return mem;
    }

    return mem.substr(0, mem.size() - 1) + "+" + std::to_string(offset) + "]";
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
    // O texto e tratado como padrao de bits: valores negativos (ponto fixo)
    // viram complemento a dois e os floats chegam aqui ja como bits.
    const std::uint64_t chunk = LiteralChunk(literal);
    const std::string text = Hex64(chunk);

    if (reg64 == "rax")
    {
        out << "    mov rax, " << text << '\n';
        return;
    }

    if (chunk <= 0xFFFFFFFFull)
    {
        out << "    mov " << RegName(reg64, 32) << ", " << text << '\n';
        return;
    }

    out << "    mov rax, " << text << '\n';
    out << "    mov " << reg64 << ", rax\n";
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

    LoadFromMemory(out, MemoryOperand(operand, slots), type, reg64);
}

void X64Generator::LoadFromMemory(
    std::ostringstream& out,
    const std::string& mem,
    const Type& type,
    const std::string& reg64
)
{
    const Type repr = type.IsValid() ? type.Representation() : Type::MakeUnsigned(32);
    const int bits = repr.IsValid() ? static_cast<int>(repr.Bits) : 32;
    const int bytes = repr.IsValid() ? static_cast<int>(repr.GetByteSize()) : 4;

    // Floats sao padroes de bits: nunca ha extensao de sinal.
    if (repr.IsFloat())
    {
        if (bytes == 2)
        {
            out << "    movzx " << RegName(reg64, 32) << ", word " << mem << '\n';
        }
        else if (bytes == 4)
        {
            out << "    mov " << RegName(reg64, 32) << ", dword " << mem << '\n';
        }
        else if (bytes == 8)
        {
            out << "    mov " << reg64 << ", qword " << mem << '\n';
        }
        else
        {
            EmitExactLoad(out, mem, bytes, reg64);
        }
        return;
    }

    const bool signedLike = SignedLike(repr);

    // Larguras fora dos tamanhos de registador (i5, u24, q40, ...) precisam de
    // carga byte a byte para nao ler alem do valor e para estender o sinal a
    // partir do bit correto.
    if (!IsStandardIntegerWidth(bits))
    {
        EmitExactLoad(out, mem, bytes, reg64);
        EmitSignExtend(out, reg64, bits, signedLike);
        return;
    }

    if (bits <= 8)
    {
        out << "    " << (signedLike ? "movsx " : "movzx ")
            << reg64 << ", byte " << mem << '\n';
        return;
    }

    if (bits <= 16)
    {
        out << "    " << (signedLike ? "movsx " : "movzx ")
            << reg64 << ", word " << mem << '\n';
        return;
    }

    if (bits <= 32)
    {
        if (signedLike)
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

void X64Generator::EmitExactLoad(
    std::ostringstream& out,
    const std::string& mem,
    int bytes,
    const std::string& reg64
)
{
    if (bytes >= 8)
    {
        out << "    mov " << reg64 << ", qword " << mem << '\n';
        return;
    }

    if (bytes <= 0)
    {
        return;
    }

    const std::string scratch = reg64 == "r11" ? "r10" : "r11";

    out << "    xor " << RegName(reg64, 32) << ", " << RegName(reg64, 32) << '\n';

    int offset = 0;
    while (bytes > 0)
    {
        const int size = bytes >= 4 ? 4 : bytes >= 2 ? 2 : 1;

        // movzx so existe para 8 e 16 bits; o pedaco de 4 bytes cabe em 32
        // bits e ja zera a metade alta do registador.
        if (size == 4)
        {
            out << "    mov " << RegName(scratch, 32) << ", dword "
                << MemWithOffset(mem, offset) << '\n';
        }
        else
        {
            out << "    movzx " << RegName(scratch, 32) << ", "
                << NasmSize(size * 8) << " " << MemWithOffset(mem, offset) << '\n';
        }

        if (offset > 0)
        {
            out << "    shl " << scratch << ", " << (offset * 8) << '\n';
        }

        out << "    or " << reg64 << ", " << scratch << '\n';

        offset += size;
        bytes -= size;
    }
}

void X64Generator::EmitSignExtend(
    std::ostringstream& out,
    const std::string& reg64,
    int bits,
    bool signedLike
)
{
    if (!signedLike || bits <= 0 || bits >= 64)
    {
        return;
    }

    const int shift = 64 - bits;
    out << "    shl " << reg64 << ", " << shift << '\n';
    out << "    sar " << reg64 << ", " << shift << '\n';
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

    EmitScalarStore(out, "[rbp" + FormatOffset(offset) + "]", type, "rax");
}

void X64Generator::StoreToName(
    std::ostringstream& out,
    const std::string& name,
    const Type& type,
    StackSlots& slots
)
{
    int offset = 0;
    const std::string mem = slots.Find(StripTemp(name), offset)
        ? "[rbp" + FormatOffset(offset) + "]"
        : "[rel " + DataSymbol(name) + "]";

    EmitScalarStore(out, mem, type, "rax");
}

// Guarda os bytes de armazenamento do tipo. Larguras que nao coincidem com um
// tamanho de registador (u24, q40, f24, ...) sao escritas byte a byte para nao
// invadir o simbolo vizinho.
void X64Generator::EmitScalarStore(
    std::ostringstream& out,
    const std::string& mem,
    const Type& type,
    const std::string& reg64
)
{
    const Type repr = type.IsValid() ? type.Representation() : Type::MakeUnsigned(32);
    const int bytes = repr.IsValid() ? static_cast<int>(repr.GetByteSize()) : 4;

    if (bytes == 0)
    {
        return;
    }

    if (bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8)
    {
        out << "    mov " << NasmSize(bytes * 8) << " " << mem << ", "
            << RegName(reg64, bytes * 8) << '\n';
        return;
    }

    EmitExactStore(out, mem, bytes, reg64);
}

void X64Generator::EmitExactStore(
    std::ostringstream& out,
    const std::string& mem,
    int bytes,
    const std::string& reg64
)
{
    if (bytes >= 8)
    {
        out << "    mov qword " << mem << ", " << reg64 << '\n';
        return;
    }

    if (bytes <= 0)
    {
        return;
    }

    const std::string scratch = reg64 == "r10" ? "r11" : "r10";
    bool shifted = false;
    int offset = 0;

    while (bytes > 0)
    {
        const int size = bytes >= 4 ? 4 : bytes >= 2 ? 2 : 1;
        const std::string source = shifted
            ? RegName(scratch, size * 8)
            : RegName(reg64, size * 8);

        out << "    mov " << NasmSize(size * 8) << " "
            << MemWithOffset(mem, offset) << ", " << source << '\n';

        offset += size;
        bytes -= size;

        if (bytes > 0)
        {
            out << "    mov " << scratch << ", " << reg64 << '\n';
            out << "    shr " << scratch << ", " << (offset * 8) << '\n';
            shifted = true;
        }
    }
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
        ->WrappedTo(type.Bits, SignedLike(type))
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

void X64Generator::EmitWideCopy(
    std::ostringstream& out,
    const std::string& source,
    const std::string& destination,
    const Type& sourceType,
    int bytes,
    StackSlots& slots
)
{
    if (IsLiteralOperand(source))
    {
        MaterializeLiteral(out, source, sourceType, slots, "$numa");
        out << WideAddress("$numa", slots, "rdx");
    }
    else
    {
        out << WideAddress(source, slots, "rdx");
    }

    out << WideAddress(destination, slots, "rcx");
    out << "    mov r8d, " << bytes << '\n';
    out << "    call __emux_wcopy\n";
}

void X64Generator::MaterializeLiteral(
    std::ostringstream& out,
    const std::string& literal,
    const Type& type,
    StackSlots& slots,
    const std::string& slot
)
{
    int offset = 0;
    if (!slots.Find(slot, offset))
    {
        throw std::runtime_error(
            "Slot temporario '" + slot + "' ausente na frame (literal numerico)."
        );
    }

    const Type repr = type.IsValid() ? type.Representation() : Type::MakeUnsigned(32);

    if (IsWideNumeric(repr))
    {
        Exir::Instruction constant;
        constant.Op = Exir::Instruction::Kind::Const;
        constant.Dst = slot;
        constant.Ty = type;
        constant.Literal = literal;
        EmitWideConst(out, constant, slots);
        return;
    }

    out << "    mov rax, " << Hex64(LiteralChunk(literal)) << '\n';
    EmitScalarStore(out, "[rbp" + FormatOffset(offset) + "]", type, "rax");
}

void X64Generator::LoadNumericPointer(
    std::ostringstream& out,
    const std::string& operand,
    const Type& type,
    StackSlots& slots,
    const std::string& reg,
    const std::string& scratch
)
{
    if (IsLiteralOperand(operand))
    {
        MaterializeLiteral(out, operand, type, slots, scratch);

        int offset = 0;
        slots.Find(scratch, offset);
        out << "    lea " << reg << ", [rbp" << FormatOffset(offset) << "]\n";
        return;
    }

    out << WideAddress(operand, slots, reg);
}

void X64Generator::EmitFloatBinary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeFloat(32);
    const Type repr = type.Representation();

    const char* helper = nullptr;

    switch (instruction.Binary)
    {
        case Exir::BinaryOp::Add: helper = "__emux_f_add"; break;
        case Exir::BinaryOp::Sub: helper = "__emux_f_sub"; break;
        case Exir::BinaryOp::Mul: helper = "__emux_f_mul"; break;
        case Exir::BinaryOp::Div: helper = "__emux_f_div"; break;
        default:
            throw std::runtime_error(
                "Operacao binaria nao suportada em float: '" + repr.ToString() + "'."
            );
    }

    AddExtern(helper);

    LoadNumericPointer(out, instruction.A, type, slots, "rdx", "$numa");
    LoadNumericPointer(out, instruction.B, type, slots, "r8", "$numb");
    out << WideAddress(instruction.Dst, slots, "rcx");
    out << "    mov r9d, " << FloatSpec(repr) << '\n';
    out << "    call " << helper << '\n';
}

void X64Generator::EmitFixedBinary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeFixed(64, 0);
    const Type repr = type.Representation();
    const int bytes = NumericBytes(repr);
    const int bits = static_cast<int>(repr.Bits);

    switch (instruction.Binary)
    {
        case Exir::BinaryOp::Add:
        case Exir::BinaryOp::Sub:
        {
            const bool subtract = instruction.Binary == Exir::BinaryOp::Sub;

            if (IsWideNumeric(repr))
            {
                LoadNumericPointer(out, instruction.A, type, slots, "rdx", "$numa");
                LoadNumericPointer(out, instruction.B, type, slots, "r8", "$numb");
                out << WideAddress(instruction.Dst, slots, "rcx");
                out << "    mov r9d, " << bytes << '\n';
                AddExtern(subtract ? "__emux_wsub" : "__emux_wadd");
                out << "    call " << (subtract ? "__emux_wsub" : "__emux_wadd") << '\n';
                break;
            }

            LoadValue(out, instruction.A, type, "rax", slots);
            LoadValue(out, instruction.B, type, "rcx", slots);
            out << "    " << (subtract ? "sub" : "add") << " rax, rcx\n";
            EmitSignExtend(out, "rax", bits, true);
            StoreResult(out, instruction.Dst, type, slots);
            break;
        }

        case Exir::BinaryOp::Mul:
        case Exir::BinaryOp::Div:
        {
            const bool divide = instruction.Binary == Exir::BinaryOp::Div;

            LoadNumericPointer(out, instruction.A, type, slots, "rdx", "$numa");
            LoadNumericPointer(out, instruction.B, type, slots, "r8", "$numb");
            out << WideAddress(instruction.Dst, slots, "rcx");

            const std::uint64_t packed = static_cast<std::uint64_t>(bytes) |
                (static_cast<std::uint64_t>(repr.Scale) << 32);

            out << "    mov r9, " << packed << '\n';
            AddExtern(divide ? "__emux_q_div" : "__emux_q_mul");
            out << "    call " << (divide ? "__emux_q_div" : "__emux_q_mul") << '\n';
            break;
        }

        case Exir::BinaryOp::Rem:
            LoadNumericPointer(out, instruction.A, type, slots, "rdx", "$numa");
            LoadNumericPointer(out, instruction.B, type, slots, "r8", "$numb");
            out << WideAddress(instruction.Dst, slots, "rcx");
            out << "    mov r9d, " << bytes << '\n';
            AddExtern("__emux_wsrem");
            out << "    call __emux_wsrem\n";
            break;

        default:
            throw std::runtime_error(
                "Operacao binaria nao suportada em ponto fixo: '" + repr.ToString() + "'."
            );
    }
}

void X64Generator::EmitFloatUnary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeFloat(32);
    const Type repr = type.Representation();
    const int bits = static_cast<int>(repr.Bits);
    const int bytes = NumericBytes(repr);

    if (instruction.Unary != Exir::UnaryOp::Neg)
    {
        throw std::runtime_error(
            "Operacao unaria nao suportada em float: '" + repr.ToString() + "'."
        );
    }

    // Negar basta inverter o bit de sinal; os restantes bits nao mudam.
    if (IsWideNumeric(repr))
    {
        EmitWideCopy(out, instruction.A, instruction.Dst, type, bytes, slots);

        out << WideAddress(instruction.Dst, slots, "r11");
        const int mask = 1 << ((bits - 1) % 8);
        out << "    xor byte [r11+" << (bytes - 1) << "], " << mask << '\n';
        return;
    }

    LoadValue(out, instruction.A, type, "rax", slots);
    out << "    mov r11, " << Hex64(std::uint64_t(1) << (bits - 1)) << '\n';
    out << "    xor rax, r11\n";
    StoreResult(out, instruction.Dst, type, slots);
}

void X64Generator::EmitNumericCast(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type dst = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeUnsigned(32);
    const Type src = instruction.SourceTy.IsValid()
        ? instruction.SourceTy
        : Type::MakeUnsigned(64);

    const Type dstRepr = dst.Representation();
    const Type srcRepr = src.Representation();

    if (!dstRepr.IsFloat() && !dstRepr.IsFixed() &&
        !srcRepr.IsFloat() && !srcRepr.IsFixed())
    {
        LoadValue(out, instruction.A, src, "rax", slots);
        StoreResult(out, instruction.Dst, dst, slots);
        return;
    }

    const int dstBytes = NumericBytes(dstRepr);
    const int srcBytes = NumericBytes(srcRepr);

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

        LoadNumericPointer(out, instruction.A, src, slots, "rdx", "$numa");
        out << WideAddress(instruction.Dst, slots, "rcx");
        AddExtern(helper);

        if (srcRepr.IsFloat())
        {
            out << "    mov r8d, " << FloatSpec(dstRepr) << '\n';
            out << "    mov r9d, " << FloatSpec(srcRepr) << '\n';
        }
        else if (srcRepr.IsFixed())
        {
            const std::uint64_t packed = static_cast<std::uint64_t>(srcBytes) |
                (static_cast<std::uint64_t>(srcRepr.Scale) << 16);

            out << "    mov r8, " << packed << '\n';
            out << "    mov r9d, " << FloatSpec(dstRepr) << '\n';
        }
        else
        {
            const std::uint64_t signedFlag = srcRepr.IsSigned() ? 1u : 0u;
            const std::uint64_t packed = static_cast<std::uint64_t>(srcBytes) |
                (signedFlag << 16);

            out << "    mov r8, " << packed << '\n';
            out << "    mov r9d, " << FloatSpec(dstRepr) << '\n';
        }

        out << "    call " << helper << '\n';
        return;
    }

    if (dstRepr.IsFixed())
    {
        LoadNumericPointer(out, instruction.A, src, slots, "rdx", "$numa");
        out << WideAddress(instruction.Dst, slots, "rcx");

        if (srcRepr.IsFloat())
        {
            const std::uint64_t packed = static_cast<std::uint64_t>(dstBytes) |
                (static_cast<std::uint64_t>(dstRepr.Scale) << 16);

            out << "    mov r8, " << packed << '\n';
            out << "    mov r9d, " << FloatSpec(srcRepr) << '\n';
            AddExtern("__emux_f_to_fixed");
            out << "    call __emux_f_to_fixed\n";
            return;
        }

        const int sourceScale = srcRepr.IsFixed()
            ? static_cast<int>(srcRepr.Scale)
            : 0;
        const int shift = static_cast<int>(dstRepr.Scale) - sourceScale;
        const std::uint64_t signedFlag = SignedLike(srcRepr) ? 1u : 0u;

        const std::uint64_t packed = static_cast<std::uint64_t>(dstBytes) |
            (static_cast<std::uint64_t>(srcBytes) << 16) |
            (signedFlag << 32);

        out << "    mov r8, " << packed << '\n';
        out << "    mov r9d, " << shift << '\n';
        AddExtern("__emux_q_rescale");
        out << "    call __emux_q_rescale\n";
        return;
    }

    // Destino inteiro ou ponteiro.
    if (srcRepr.IsFloat())
    {
        LoadNumericPointer(out, instruction.A, src, slots, "r8", "$numa");
        out << WideAddress(instruction.Dst, slots, "rcx");
        out << "    mov edx, " << dstBytes << '\n';
        out << "    mov r9d, " << FloatSpec(srcRepr) << '\n';
        AddExtern("__emux_f_to_int");
        out << "    call __emux_f_to_int\n";
        return;
    }

    // Ponto fixo -> inteiro: desfaz a escala.
    LoadNumericPointer(out, instruction.A, src, slots, "rdx", "$numa");
    out << WideAddress(instruction.Dst, slots, "rcx");

    const std::uint64_t packed = static_cast<std::uint64_t>(dstBytes) |
        (static_cast<std::uint64_t>(srcBytes) << 16) |
        (static_cast<std::uint64_t>(1) << 32);

    out << "    mov r8, " << packed << '\n';
    out << "    mov r9d, " << (-static_cast<int>(srcRepr.Scale)) << '\n';
    AddExtern("__emux_q_rescale");
    out << "    call __emux_q_rescale\n";
}

void X64Generator::EmitBitCast(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type dst = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeUnsigned(64);
    const Type src = instruction.SourceTy.IsValid()
        ? instruction.SourceTy
        : Type::MakeUnsigned(64);

    const int dstBytes = NumericBytes(dst);
    const int srcBytes = NumericBytes(src);

    if (dstBytes != srcBytes)
    {
        throw std::runtime_error(
            "bitcast entre larguras diferentes ('" + src.ToString() +
            "' -> '" + dst.ToString() + "')."
        );
    }

    if (dstBytes > 8)
    {
        EmitWideCopy(out, instruction.A, instruction.Dst, src, dstBytes, slots);
        return;
    }

    LoadValue(out, instruction.A, src, "rax", slots);
    StoreResult(out, instruction.Dst, dst, slots);
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

// Mnemónico setcc para uma comparação, escolhendo a variante com ou sem sinal.
static const char* ComparisonSetcc(Exir::BinaryOp op, bool isSigned)
{
    switch (op)
    {
        case Exir::BinaryOp::Eq: return "sete";
        case Exir::BinaryOp::Ne: return "setne";
        case Exir::BinaryOp::Lt: return isSigned ? "setl"  : "setb";
        case Exir::BinaryOp::Gt: return isSigned ? "setg"  : "seta";
        case Exir::BinaryOp::Le: return isSigned ? "setle" : "setbe";
        case Exir::BinaryOp::Ge: return isSigned ? "setge" : "setae";
        default:                 return "sete";
    }
}

void X64Generator::EmitBinary(
    std::ostringstream& out,
    const Exir::Instruction& instruction,
    StackSlots& slots
)
{
    const Type type = instruction.Ty.IsValid() ? instruction.Ty : Type::MakeUnsigned(32);

    if (type.IsFloat())
    {
        EmitFloatBinary(out, instruction, slots);
        return;
    }

    if (type.IsFixed())
    {
        EmitFixedBinary(out, instruction, slots);
        return;
    }

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

    // Comparações: cmp + setcc + movzx; o destino é 0/1 em u8.
    if (Exir::IsComparison(instruction.Binary))
    {
        out << "    cmp " << acc << ", " << rhs << '\n';
        out << "    " << ComparisonSetcc(instruction.Binary, type.IsSigned()) << " al\n";
        out << "    movzx eax, al\n";
        StoreResult(out, instruction.Dst, Type::MakeUnsigned(8), slots);
        return;
    }

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

    if (type.IsFloat())
    {
        EmitFloatUnary(out, instruction, slots);
        return;
    }

    // Ponto fixo tem representacao de inteiro com sinal; so a negacao larga
    // precisa do helper multi-limb.
    if (IsWideNumeric(type))
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
    const Type repr = type.Representation();
    const int bits = static_cast<int>(repr.Bits);

    // Floats tem sempre largura multipla de 8: normalizar e so copiar os
    // bytes de armazenamento.
    if (repr.IsFloat())
    {
        if (IsWideNumeric(repr))
        {
            EmitWideCopy(out, instruction.A, instruction.Dst, type, NumericBytes(repr), slots);
        }
        else
        {
            LoadValue(out, instruction.A, type, "rax", slots);
            StoreResult(out, instruction.Dst, type, slots);
        }

        return;
    }

    if (IsWideNumeric(repr))
    {
        const int bytes = WideBytes(type);

        EmitWideCopy(out, instruction.A, instruction.Dst, type, bytes, slots);

        const int top = bits % 8;
        if (top == 0)
        {
            return;
        }

        const int mask = (1 << top) - 1;
        out << WideAddress(instruction.Dst, slots, "r11");
        out << "    and byte [r11+" << (bytes - 1) << "], " << mask << '\n';

        if (SignedLike(repr))
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
        out << (SignedLike(repr) ? "    sar rax, " : "    shr rax, ") << shift << '\n';
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
            if (IsWideNumeric(instruction.Ty))
            {
                EmitWideConst(out, instruction, slots);
                break;
            }
            EmitImmediate(out, instruction.Literal, "rax");
            StoreResult(out, instruction.Dst, instruction.Ty, slots);
            break;

        case Exir::Instruction::Kind::Cast:
        {
            const Type destination = instruction.Ty;
            const Type source = instruction.SourceTy;

            if (destination.IsFloat() || destination.IsFixed() ||
                source.IsFloat() || source.IsFixed())
            {
                EmitNumericCast(out, instruction, slots);
                break;
            }

            if (IsWideInteger(destination) || IsWideInteger(source))
            {
                EmitWideCast(out, instruction, slots);
                break;
            }

            LoadValue(out, instruction.A, instruction.SourceTy, "rax", slots);
            StoreResult(out, instruction.Dst, instruction.Ty, slots);
            break;
        }

        case Exir::Instruction::Kind::BitCast:
            EmitBitCast(out, instruction, slots);
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

            if (IsWideNumeric(type))
            {
                LoadAddress(out, instruction.A, slots);
                out << "    mov rdx, r11\n";
                out << WideAddress(instruction.Dst, slots, "rcx");
                out << "    mov r8d, " << NumericBytes(type) << '\n';
                out << "    call __emux_wcopy\n";
                break;
            }

            LoadAddress(out, instruction.A, slots);

            // A carga byte a byte usa r11 como temporario: guardar o ponteiro
            // em rdx antes para nao o perder a meio.
            out << "    mov rdx, r11\n";
            LoadFromMemory(out, "[rdx]", type, "rax");
            StoreResult(out, instruction.Dst, type, slots);
            break;
        }

        case Exir::Instruction::Kind::Store:
        {
            const Type type = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(32);

            if (IsWideNumeric(type))
            {
                LoadAddress(out, instruction.A, slots);
                out << "    mov rcx, r11\n";
                LoadNumericPointer(out, instruction.B, type, slots, "rdx", "$numa");
                out << "    mov r8d, " << NumericBytes(type) << '\n';
                out << "    call __emux_wcopy\n";
                break;
            }

            // Carregar o valor antes do endereco: a carga exata usa r11.
            LoadValue(out, instruction.B, type, "rax", slots);
            LoadAddress(out, instruction.A, slots);
            EmitScalarStore(out, "[r11]", type, "rax");
            break;
        }

        case Exir::Instruction::Kind::StoreVar:
        {
            const Type type = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(32);

            if (IsWideNumeric(type))
            {
                out << WideAddress(instruction.A, slots, "rcx");
                LoadNumericPointer(out, instruction.B, type, slots, "rdx", "$numa");
                out << "    mov r8d, " << NumericBytes(type) << '\n';
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

        case Exir::Instruction::Kind::Label:
            out << instruction.A << ":\n";
            break;

        case Exir::Instruction::Kind::Jump:
            out << "    jmp " << instruction.A << '\n';
            break;

        case Exir::Instruction::Kind::BranchIf:
        case Exir::Instruction::Kind::BranchIfNot:
        {
            const Type type = instruction.Ty.IsValid()
                ? instruction.Ty
                : Type::MakeUnsigned(8);
            const int bits = type.Bits > 32 ? 64 : 32;
            const std::string acc = bits == 64 ? "rax" : "eax";

            LoadValue(out, instruction.A, type, "rax", slots);
            out << "    test " << acc << ", " << acc << '\n';
            out << "    "
                << (instruction.Op == Exir::Instruction::Kind::BranchIf ? "jne" : "je")
                << " " << instruction.B << '\n';
            break;
        }

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
        return type.IsValid() && IsWideNumeric(type) ? NumericBytes(type) : 8;
    };

    for (const Exir::Parameter& parameter : function.Params)
    {
        slots.Allocate(parameter.Name, SlotBytes(parameter.Ty));
    }

    // Literais float/ponto fixo chegam como padroes de bits e sao copiados
    // para slots temporarios antes de virarem ponteiros para o runtime.
    bool needsScratch = false;
    int scratchBytes = 8;

    const auto NoteScratch = [&](const Type& type)
    {
        if (!type.IsValid())
        {
            return;
        }

        if (type.IsFloat() || type.IsFixed() || IsWideNumeric(type))
        {
            needsScratch = true;

            if (NumericBytes(type) > scratchBytes)
            {
                scratchBytes = NumericBytes(type);
            }
        }
    };

    for (const Exir::Parameter& parameter : function.Params)
    {
        NoteScratch(parameter.Ty);
    }

    for (const Exir::Instruction& instruction : function.Body)
    {
        if (!instruction.Dst.empty())
        {
            slots.Allocate(StripTemp(instruction.Dst), SlotBytes(instruction.Ty));
        }

        NoteScratch(instruction.Ty);
        NoteScratch(instruction.SourceTy);

        for (const Exir::CallArg& argument : instruction.Args)
        {
            NoteScratch(argument.Ty);
        }
    }

    if (needsScratch)
    {
        slots.Allocate("$numa", scratchBytes);
        slots.Allocate("$numb", scratchBytes);
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
