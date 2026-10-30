#include "Emux/Generator/NasmInstructions.hpp"
#include "Emux/Generator/NasmRegisters.hpp"
#include "Emux/Generator/NasmUtils.hpp"
#include "Emux/Compiler/Type/TypeParser.hpp"
#include "Emux/Core/Logger.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Emux
{

namespace NasmInstructions
{

using namespace NasmUtils;
using namespace NasmRegisters;

std::string Trim(std::string_view input)
{
    std::string text(input);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return text;
}

void HandleRegs(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(5u));
    auto decls = Split(correctLine, ',');

    const auto& physRegForms = GetPhysicalRegForms(ctx.arch);
    std::size_t spillCount = 0;

    for (std::size_t i = 0; i < decls.size(); ++i)
    {
        auto [name, type] = ParseDecl(decls[i], PointerBits(ctx.arch));
        if (i < physRegForms.size())
        {
            ctx.currentRegMap[name] = { false, "", i, static_cast<int>(type.Bits), type.IsSigned() };
        }
        else
        {
            int slotBytes = (ctx.arch == Architecture::X86_64) ? 8 : 4;
            std::string baseReg = (ctx.arch == Architecture::X86_64) ? "rbp" : "ebp";
            std::string memText = NasmSize(type.Bits) + " [" + baseReg + "-" + std::to_string(slotBytes * (++spillCount)) + "]";
            ctx.currentRegMap[name] = { true, memText, 0, static_cast<int>(type.Bits), type.IsSigned() };
        }
    }
    ctx.currentSpillCount = spillCount;

    if (spillCount > 0)
    {
        ctx.EnsureFrame();
        if (ctx.arch == Architecture::X86)
        {
            ctx.bodies["text"] << "sub esp, " << (4 * spillCount) << "\n";
        }
        else
        {
            size_t bytes = 8 * spillCount;
            if (bytes % 16 != 0) bytes += (16 - (bytes % 16));
            ctx.bodies["text"] << "sub rsp, " << bytes << "\n";
        }
    }
}

void HandleVars(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(5u));
    auto decls = Split(correctLine, ',');

    for (auto& decl : decls)
    {
        auto [name, type] = ParseDecl(decl, PointerBits(ctx.arch));
        ctx.variablesEmitted[name] = type;

        const std::string exported = Exported(name, ctx.arch);

        if (auto aliases = ctx.exportAliases.find(name); aliases != ctx.exportAliases.end())
        {
            for (const std::string& alias : aliases->second)
            {
                const std::string symbol = Exported(alias, ctx.arch);
                if (symbol != name && symbol != exported)
                {
                    ctx.bodies["bss"] << symbol << ":\n";
                }
            }
        }

        if (exported != name)
        {
            ctx.bodies["bss"] << exported << ":\n";
        }
        ctx.bodies["bss"] << name << ": res" << NasmSize(type.Bits)[0] << " 1\n";
    }
}

void HandleExtVars(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(8u));
    auto decls = Split(correctLine, ',');

    for (auto& decl : decls)
    {
        auto [name, type] = ParseDecl(decl, PointerBits(ctx.arch));
        ctx.externalVariables[name] = type;
    }
}

// Registers args by name; memoryText is used as the operand text, so it holds
// either a memory reference or the incoming argument register.
void HandleArgs(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(5u));
    auto decls = Split(correctLine, ',');

    static const char* x64ArgRegs[4][4] = {
        {"cl",  "cx",  "ecx",  "rcx"},
        {"dl",  "dx",  "edx",  "rdx"},
        {"r8b", "r8w", "r8d",  "r8"},
        {"r9b", "r9w", "r9d",  "r9"}
    };

    if (ctx.arch == Architecture::X86)
    {
        // Layout x86: stdcall/cdecl recebem tudo na pilha; fastcall usa
        // ecx/edx para os dois primeiros argumentos que cabem em 32 bits.
        static const char* fastcallRegs[2] = { "ecx", "edx" };

        std::size_t stackOffset = 8;
        int fastIndex = 0;

        for (std::size_t i = 0; i < decls.size(); ++i)
        {
            auto [name, type] = ParseDecl(decls[i], PointerBits(ctx.arch));
            int bits = static_cast<int>(type.Bits);

            if (bits > 64)
            {
                throw std::runtime_error("Argument '" + name + "' larger than 64 bits is not supported by the nasm backend yet");
            }

            if (ctx.currentFuncAbi == Abi::Fastcall && fastIndex < 2 && bits <= 32)
            {
                ctx.currentRegMap[name] = { true, fastcallRegs[fastIndex], 0, bits, type.IsSigned() };
                ++fastIndex;
                continue;
            }

            // Slots de pilha seguem o tamanho da palavra: 4 bytes ate 32 bits
            // (o chamador estende o valor) e 8 bytes acima disso.
            std::string memText = std::string(bits > 32 ? "qword" : "dword")
                + " [ebp+" + std::to_string(stackOffset) + "]";
            ctx.currentRegMap[name] = { true, memText, 0, bits, type.IsSigned() };
            stackOffset += (bits > 32) ? 8 : 4;
        }

        ctx.currentFuncStack = stackOffset - 8;
        ctx.EnsureFrame();
        return;
    }

    for (std::size_t i = 0; i < decls.size(); ++i)
    {
        auto [name, type] = ParseDecl(decls[i], PointerBits(ctx.arch));
        int bits = static_cast<int>(type.Bits);

        if (bits > 64)
        {
            throw std::runtime_error("Argument '" + name + "' larger than 64 bits is not supported by the nasm backend yet");
        }

        if (i < 4)
        {
            // Forma de 32 bits para tudo ate 32 bits: os handlers aritmeticos
            // operam em 32 bits e o chamador estende valores estreitos.
            int sizeIdx = bits <= 32 ? 2 : 3;
            ctx.currentRegMap[name] = { true, x64ArgRegs[i][sizeIdx], 0, bits, type.IsSigned() };
        }
        else
        {
            // Acima de 4 args vao na pilha: [rsp+32+8*(i-4)] antes do call -> [rbp+48+8*(i-4)] depois do frame
            std::string memText = NasmSize(bits) + " [rbp+" + std::to_string(16 + 8 * (i - 4)) + "]";
            ctx.currentRegMap[name] = { true, memText, 0, bits, type.IsSigned() };
        }
    }

    ctx.EnsureFrame();
}

void HandleBytes(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(6u));
    auto args = Split(correctLine, ',', true);

    if (args.empty()) return;

    std::string label = args[0];
    ctx.dataLabelsEmitted.insert(label);

    std::string message;
    for (auto it = ++args.begin(); it != args.end(); ++it)
    {
        message += (*it);
        message += ',';
    }
    if (!message.empty()) message.pop_back();

    std::string exported = Exported(label, ctx.arch);
    if (exported != label)
    {
        ctx.bodies["data"] << exported << ":\n";
    }
    ctx.bodies["data"] << label << " db " << message << '\n';
    ctx.bodies["data"] << label << "_len equ $ - " << label << '\n';
}

void HandleDoubleMath(std::string_view line, NasmContext& ctx)
{
    auto spacePos = line.find_first_of(' ');
    std::string correctLine(line.substr(spacePos + 1));
    auto args = Split(correctLine, ',');

    std::string strInst(line.substr(0, spacePos));

    if (args.size() < 2)
    {
        Logger::Error("Instruction received quantity of args less than 2: " + std::string(line));
        return;
    }

    ctx.bodies["text"] << strInst << " " << ResolveOperand(args[0], ctx) << ", " << ResolveOperand(args[1], ctx) << '\n';
}

namespace
{

enum class OperandKind
{
    Unknown,
    Register,
    Memory,
    Immediate
};

struct OperandInfo
{
    OperandKind kind = OperandKind::Unknown;
    std::string baseName;
    int bits = 0;
    bool isSigned = false;
    bool fixedWidthReg = false;
};

bool IsSizeKeyword(std::string_view text)
{
    return text == "byte" || text == "word" || text == "dword" || text == "qword";
}

OperandInfo InspectOperand(std::string_view raw, NasmContext& ctx){
    OperandInfo info;
    std::string text = Trim(raw);

    auto colon = text.find(':');
    info.baseName = (colon == std::string::npos) ? text : text.substr(0, colon);
    int overrideBits = (colon == std::string::npos) ? 0 : std::atoi(text.c_str() + colon + 1);

    const std::string& name = info.baseName;

    if (name == "rr0" || name == "rr1")
    {
        info.kind = OperandKind::Register;
        info.bits = overrideBits > 0 ? overrideBits : (ctx.arch == Architecture::X86_64 ? 64 : 32);
        return info;
    }

    if (auto it = ctx.currentRegMap.find(name); it != ctx.currentRegMap.end())
    {
        const SlotInfo& slot = it->second;
        info.bits = overrideBits > 0 ? overrideBits : slot.bits;
        info.isSigned = slot.isSigned;

        if (!slot.isMemory)
        {
            info.kind = OperandKind::Register;
        }
        else
        {
            std::string first = slot.memoryText.substr(0, slot.memoryText.find(' '));
            if (IsSizeKeyword(first))
            {
                info.kind = OperandKind::Memory;
            }
            else
            {
                // Argumento recebido em registrador (x64): texto fixo como "ecx".
                info.kind = OperandKind::Register;
                info.fixedWidthReg = true;
                info.bits = slot.bits;
            }
        }
        return info;
    }

    if (auto it = ctx.variablesEmitted.find(name); it != ctx.variablesEmitted.end())
    {
        info.kind = OperandKind::Memory;
        info.bits = static_cast<int>(it->second.Bits);
        info.isSigned = it->second.IsSigned();
        return info;
    }

    if (auto it = ctx.externalVariables.find(name); it != ctx.externalVariables.end())
    {
        info.kind = OperandKind::Memory;
        info.bits = static_cast<int>(it->second.Bits);
        info.isSigned = it->second.IsSigned();
        return info;
    }

    if (!text.empty())
    {
        char c = text.front();
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+')
        {
            info.kind = OperandKind::Immediate;
        }
    }

    return info;
}

const std::vector<std::vector<std::string>>& ArgRegForms()
{
    static const std::vector<std::vector<std::string>> forms = {
        {"cl", "cx", "ecx", "rcx"},
        {"dl", "dx", "edx", "rdx"},
        {"r8b", "r8w", "r8d", "r8"},
        {"r9b", "r9w", "r9d", "r9"}
    };
    return forms;
}

const std::vector<std::vector<std::string>>& FastcallRegForms()
{
    static const std::vector<std::vector<std::string>> forms = {
        {"cl", "cx", "ecx", "ecx"},
        {"dl", "dx", "edx", "edx"}
    };
    return forms;
}

bool SizeFixedWidthReg(std::string_view text, int bits, std::string& out, NasmContext& ctx)
{
    auto search = [&](const std::vector<std::vector<std::string>>& table) -> bool
    {
        for (const auto& forms : table)
        {
            if (std::find(forms.begin(), forms.end(), text) == forms.end()) continue;

            int sizeIdx = bits <= 8 ? 0 : bits <= 16 ? 1 : bits <= 32 ? 2 : 3;
            if (ctx.arch == Architecture::X86 && sizeIdx > 2) return false;

            out = forms.at(sizeIdx);
            return true;
        }
        return false;
    };

    if (search(ArgRegForms())) return true;
    if (ctx.arch == Architecture::X86 && search(FastcallRegForms())) return true;
    return false;
}

bool SizeRegisterTo(const OperandInfo& info, int bits, std::string& out, NasmContext& ctx)
{
    if (info.kind != OperandKind::Register) return false;

    if (info.fixedWidthReg)
    {
        // O texto do operando e o nome do parametro ("x"); a forma real do
        // registrador argumento ("ecx") esta guardada no slot.
        std::string regName = info.baseName;
        if (auto it = ctx.currentRegMap.find(info.baseName);
            it != ctx.currentRegMap.end() && it->second.isMemory)
        {
            regName = it->second.memoryText;
        }

        return SizeFixedWidthReg(regName, bits, out, ctx);
    }

    out = ResolveOperand(info.baseName + ":" + std::to_string(bits), ctx);
    return true;
}

bool SizeMemoryTo(const OperandInfo& info, int bits, std::string& out, NasmContext& ctx)
{
    if (info.kind != OperandKind::Memory) return false;

    if (auto it = ctx.currentRegMap.find(info.baseName);
        it != ctx.currentRegMap.end() && it->second.isMemory)
    {
        const std::string& text = it->second.memoryText;
        auto space = text.find(' ');
        std::string rest = (space == std::string::npos) ? std::string() : text.substr(space);
        out = NasmSize(bits) + rest;
        return true;
    }

    std::ostringstream sstr;
    sstr << NasmSize(bits) << " [";
    if (ctx.arch == Architecture::X86_64)
    {
        sstr << "rel ";
    }
    sstr << info.baseName << "]";
    out = sstr.str();
    return true;
}

bool SizeOperandTo(const OperandInfo& info, int bits, std::string& out, NasmContext& ctx)
{
    return SizeRegisterTo(info, bits, out, ctx) || SizeMemoryTo(info, bits, out, ctx);
}

std::string ScratchRegAt(int bits, NasmContext& ctx)
{
    if (ctx.arch == Architecture::X86)
    {
        if (bits <= 8) return "al";
        if (bits <= 16) return "ax";
        return "eax";
    }

    if (bits <= 8) return "r15b";
    if (bits <= 16) return "r15w";
    if (bits <= 32) return "r15d";
    return "r15";
}

bool ImmediateFitsSigned32(std::string_view text)
{
    std::string value = Trim(text);
    if (value.empty()) return true;

    char* end = nullptr;
    errno = 0;
    long long parsed = std::strtoll(value.c_str(), &end, 0);

    if (errno == ERANGE) return false;
    if (end == value.c_str() || *end != '\0') return true; // nao numerico: deixa o nasm decidir

    return parsed >= INT32_MIN && parsed <= INT32_MAX;
}

bool IsNumericLiteral(std::string_view text)
{
    std::string value = Trim(text);
    if (value.empty()) return false;

    const char first = value.front();
    return std::isdigit(static_cast<unsigned char>(first)) || first == '-' || first == '+';
}

bool ParseImmediate(std::string_view text, long long& out)
{
    std::string value = Trim(text);
    if (value.empty()) return false;

    char* end = nullptr;
    errno = 0;
    out = std::strtoll(value.c_str(), &end, 0);

    if (end == value.c_str() || *end != '\0') return false;
    return errno != ERANGE;
}

// Texto de memoria sem o prefixo de tamanho, ex: "[ebp+8]" ou "[rel g]".
std::string MemoryBase(const OperandInfo& info, NasmContext& ctx)
{
    if (auto it = ctx.currentRegMap.find(info.baseName);
        it != ctx.currentRegMap.end() && it->second.isMemory)
    {
        const std::string& text = it->second.memoryText;
        auto space = text.find(' ');
        return (space == std::string::npos) ? text : text.substr(space + 1);
    }

    std::string base = "[";
    if (ctx.arch == Architecture::X86_64) base += "rel ";
    base += info.baseName;
    base += "]";
    return base;
}

std::string OffsetMemory(std::string base, int offset)
{
    if (offset == 0) return base;

    if (!base.empty() && base.back() == ']') base.pop_back();
    base += '+';
    base += std::to_string(offset);
    base += ']';
    return base;
}

std::string Hex32(std::uint32_t value)
{
    std::ostringstream sstr;
    sstr << "0x" << std::hex << value;
    return sstr.str();
}

struct HalfValue
{
    std::string lo;
    std::string hi;
};

// Separa um operando de 64 bits em duas metades utilizaveis no x86.
HalfValue Split64(std::string_view text, NasmContext& ctx)
{
    OperandInfo info = InspectOperand(text, ctx);
    HalfValue result;
    long long immediate = 0;

    if (IsNumericLiteral(text) && ParseImmediate(text, immediate))
    {
        const std::uint64_t bits = static_cast<std::uint64_t>(immediate);
        result.lo = Hex32(static_cast<std::uint32_t>(bits));
        result.hi = Hex32(static_cast<std::uint32_t>(bits >> 32));
        return result;
    }

    if (info.kind == OperandKind::Memory)
    {
        std::string base = MemoryBase(info, ctx);
        result.lo = "dword " + base;
        result.hi = info.bits > 32 ? "dword " + OffsetMemory(base, 4) : std::string("0");
        return result;
    }

    if (info.kind == OperandKind::Register)
    {
        std::string sized;
        if (SizeRegisterTo(info, 32, sized, ctx))
        {
            result.lo = sized;
            result.hi = "0";
            return result;
        }
    }

    result.lo = ResolveOperand(Trim(text), ctx);
    result.hi = "0";
    return result;
}

std::string Reg32Of(const std::string& reg)
{
    if (reg == "rax") return "eax";
    if (reg == "r14") return "r14d";
    if (reg == "r15") return "r15d";
    return reg + "d";
}

// Carrega um valor (de 32 ou 64 bits) num registrador de 64 bits, estendendo
// conforme o tipo de origem quando necessario.
void Load64IntoX64(
    std::string_view text,
    const std::string& reg,
    bool destSigned,
    NasmContext& ctx,
    const Type* knownType = nullptr
)
{
    auto& body = ctx.bodies["text"];
    OperandInfo info = InspectOperand(text, ctx);
    const std::string value = Trim(text);
    long long immediate = 0;

    int srcBits = (knownType && knownType->IsValid())
        ? static_cast<int>(knownType->Bits)
        : (info.bits > 0 ? info.bits : 32);
    const bool srcSigned = (knownType && knownType->IsValid())
        ? knownType->IsSigned()
        : info.isSigned;

    if (IsNumericLiteral(text) && ParseImmediate(text, immediate))
    {
        body << "mov " << reg << ", " << value << '\n';
        return;
    }

    if (value == "rr0")
    {
        if (srcBits > 32)
        {
            if (reg != "rax") body << "mov " << reg << ", rax\n";
            return;
        }

        if (srcBits < 32)
            body << (srcSigned ? "movsx " : "movzx ") << "eax, " << NasmSize(srcBits) << " eax\n";

        if (reg != "eax")
        {
            if (destSigned) body << "movsxd " << reg << ", eax\n";
            else body << "mov " << Reg32Of(reg) << ", eax\n";
        }
        return;
    }

    if (info.kind == OperandKind::Memory)
    {
        std::string base = MemoryBase(info, ctx);
        if (srcBits > 32)
        {
            body << "mov " << reg << ", qword " << base << '\n';
            return;
        }

        const std::string reg32 = Reg32Of(reg);
        if (srcBits >= 32)
            body << "mov " << reg32 << ", dword " << base << '\n';
        else
            body << (srcSigned ? "movsx " : "movzx ") << reg32 << ", " << NasmSize(srcBits) << " " << base << '\n';

        if (destSigned) body << "movsxd " << reg << ", " << reg32 << '\n';
        return;
    }

    if (info.kind == OperandKind::Register)
    {
        const std::string reg32 = Reg32Of(reg);
        std::string sized;

        if (!SizeRegisterTo(info, srcBits > 32 ? 64 : 32, sized, ctx))
        {
            body << "mov " << reg << ", " << ResolveOperand(value, ctx) << '\n';
            return;
        }

        if (srcBits > 32)
        {
            if (sized != reg) body << "mov " << reg << ", " << sized << '\n';
            return;
        }

        if (srcBits >= 32)
            body << "mov " << reg32 << ", " << sized << '\n';
        else
            body << (srcSigned ? "movsx " : "movzx ") << reg32 << ", " << sized << '\n';

        if (destSigned) body << "movsxd " << reg << ", " << reg32 << '\n';
        return;
    }

    body << "mov " << reg << ", " << value << '\n';
}

// Carrega um valor em edx:eax (convencao interna do x86 para 64 bits).
void LoadPairEaxEdx(
    std::string_view text,
    NasmContext& ctx,
    bool extendSigned,
    const Type* knownType = nullptr
)
{
    auto& body = ctx.bodies["text"];
    OperandInfo info = InspectOperand(text, ctx);
    const std::string value = Trim(text);
    long long immediate = 0;

    const int srcBits = (knownType && knownType->IsValid())
        ? static_cast<int>(knownType->Bits)
        : (info.bits > 0 ? info.bits : 32);
    const bool srcSigned = (knownType && knownType->IsValid())
        ? knownType->IsSigned()
        : info.isSigned;

    if (IsNumericLiteral(text) && ParseImmediate(text, immediate))
    {
        const std::uint64_t bits = static_cast<std::uint64_t>(immediate);
        body << "mov eax, " << Hex32(static_cast<std::uint32_t>(bits)) << '\n';
        body << "mov edx, " << Hex32(static_cast<std::uint32_t>(bits >> 32)) << '\n';
        return;
    }

    if (value == "rr0")
    {
        if (srcBits > 32) return;

        if (extendSigned) body << "cdq\n";
        else body << "xor edx, edx\n";
        return;
    }

    if (info.kind == OperandKind::Memory)
    {
        std::string base = MemoryBase(info, ctx);
        if (srcBits > 32)
        {
            body << "mov eax, dword " << base << '\n';
            body << "mov edx, dword " << OffsetMemory(base, 4) << '\n';
            return;
        }

        if (srcBits >= 32)
            body << "mov eax, dword " << base << '\n';
        else
            body << (srcSigned ? "movsx " : "movzx ") << "eax, " << NasmSize(srcBits) << " " << base << '\n';
    }
    else if (info.kind == OperandKind::Register)
    {
        std::string reg;
        if (srcBits >= 32)
        {
            if (SizeRegisterTo(info, 32, reg, ctx)) body << "mov eax, " << reg << '\n';
        }
        else if (SizeRegisterTo(info, srcBits, reg, ctx))
        {
            body << (srcSigned ? "movsx " : "movzx ") << "eax, " << reg << '\n';
        }
    }
    else
    {
        body << "mov eax, " << ResolveOperand(value, ctx) << '\n';
    }

    if (extendSigned)
        body << "cdq\n";
    else
        body << "xor edx, edx\n";
}

void EmitEpilogue(NasmContext& ctx)
{
    if (!ctx.frameEmitted) return;

    if (ctx.arch == Architecture::X86)
        ctx.bodies["text"] << "mov esp, ebp\npop ebp\n";
    else
        ctx.bodies["text"] << "mov rsp, rbp\npop rbp\n";
}

void StorePairTo(
    std::string_view dstText,
    const std::string& loReg,
    const std::string& hiReg,
    NasmContext& ctx
)
{
    OperandInfo info = InspectOperand(dstText, ctx);
    auto& body = ctx.bodies["text"];

    std::string base = (info.kind == OperandKind::Memory)
        ? MemoryBase(info, ctx)
        : "[" + std::string(dstText) + "]";

    body << "mov dword " << base << ", " << loReg << '\n';
    body << "mov dword " << OffsetMemory(base, 4) << ", " << hiReg << '\n';
}

void StoreRaxTo(std::string_view dstText, NasmContext& ctx)
{
    OperandInfo info = InspectOperand(dstText, ctx);
    auto& body = ctx.bodies["text"];

    if (info.kind == OperandKind::Memory)
    {
        body << "mov qword " << MemoryBase(info, ctx) << ", rax\n";
        return;
    }

    std::string sized;
    if (SizeRegisterTo(info, 64, sized, ctx))
        body << "mov " << sized << ", rax\n";
}

int ArgWidth(std::string_view text, NasmContext& ctx)
{
    OperandInfo info = InspectOperand(text, ctx);
    return info.bits > 0 ? info.bits : 32;
}

// Operando de 64 bits valido no x64 (imediato, qword [mem] ou registrador auxiliar).
std::string WideOperandX64(std::string_view text, NasmContext& ctx, const std::string& scratch)
{
    OperandInfo info = InspectOperand(text, ctx);
    long long immediate = 0;

    if (IsNumericLiteral(text) && ParseImmediate(text, immediate))
    {
        return Trim(text);
    }

    if (info.kind == OperandKind::Memory)
    {
        std::string base = MemoryBase(info, ctx);
        if (info.bits > 32 || info.bits <= 0) return "qword " + base;
    }

    Load64IntoX64(text, scratch, false, ctx);
    return scratch;
}

void LoadPointer(std::string_view text, const std::string& reg, NasmContext& ctx)
{
    auto& body = ctx.bodies["text"];
    OperandInfo info = InspectOperand(text, ctx);
    const std::string value = Trim(text);
    long long immediate = 0;

    if (IsNumericLiteral(text) && ParseImmediate(text, immediate))
    {
        body << "mov " << reg << ", " << value << '\n';
        return;
    }

    if (info.kind == OperandKind::Memory)
    {
        std::string base = MemoryBase(info, ctx);
        int bits = info.bits > 0 ? info.bits : 32;
        if (ctx.arch == Architecture::X86_64 && bits > 32)
            body << "mov " << reg << ", qword " << base << '\n';
        else
            body << "mov " << reg << ", dword " << base << '\n';
        return;
    }

    if (info.kind == OperandKind::Register)
    {
        std::string sized;
        const int width = (ctx.arch == Architecture::X86_64) ? 64 : 32;
        if (SizeRegisterTo(info, width, sized, ctx))
        {
            if (sized != reg) body << "mov " << reg << ", " << sized << '\n';
            return;
        }
    }

    const std::string resolved = ResolveOperand(value, ctx);
    if (resolved != reg) body << "mov " << reg << ", " << resolved << '\n';
}

} // namespace

void HandleMov(std::string_view line, NasmContext& ctx)
{
    auto args = Split(line.substr(4u), ',');

    if (args.size() < 2)
    {
        ctx.bodies["text"] << "mov " << Trim(line.substr(4u)) << '\n';
        return;
    }

    auto& body = ctx.bodies["text"];
    auto emitPlain = [&]()
    {
        body << "mov " << ResolveOperand(Trim(args[0]), ctx) << ", " << ResolveOperand(Trim(args[1]), ctx) << '\n';
    };

    OperandInfo dst = InspectOperand(args[0], ctx);
    OperandInfo src = InspectOperand(args[1], ctx);

    // Imediato que nao cabe em 32 bits assinados indo para memoria de 64 bits:
    // 'mov qword [m], imm32' truncaria o valor, entao passa por um registrador.
    if (src.kind == OperandKind::Immediate && dst.kind == OperandKind::Memory
        && dst.bits > 32 && !ImmediateFitsSigned32(args[1]))
    {
        std::string dstSized;
        if (SizeMemoryTo(dst, dst.bits, dstSized, ctx))
        {
            std::string scratch = ScratchRegAt(dst.bits, ctx);
            body << "mov " << scratch << ", " << Trim(args[1]) << '\n';
            body << "mov " << dstSized << ", " << scratch << '\n';
            return;
        }
    }

    // Memoria <- memoria nao existe em x86, mesmo com tamanhos iguais.
    if (dst.kind == OperandKind::Memory && src.kind == OperandKind::Memory
        && dst.bits > 0 && dst.bits == src.bits)
    {
        std::string dstSized;
        std::string srcSized;
        if (SizeMemoryTo(dst, dst.bits, dstSized, ctx) && SizeMemoryTo(src, src.bits, srcSized, ctx))
        {
            std::string scratch = ScratchRegAt(dst.bits, ctx);
            body << "mov " << scratch << ", " << srcSized << '\n';
            body << "mov " << dstSized << ", " << scratch << '\n';
            return;
        }
    }

    bool comparable = dst.kind != OperandKind::Unknown && src.kind != OperandKind::Unknown
        && dst.kind != OperandKind::Immediate && src.kind != OperandKind::Immediate
        && dst.bits > 0 && src.bits > 0 && dst.bits != src.bits;

    if (!comparable)
    {
        emitPlain();
        return;
    }

    std::string dstSized;

    if (src.bits < dst.bits)
    {
        // Alarga a origem para o tamanho do destino (movsx/movzx).
        std::string srcSized;
        if (!SizeOperandTo(src, src.bits, srcSized, ctx))
        {
            emitPlain();
            return;
        }

        std::string ext = src.isSigned ? "movsx " : "movzx ";

        if (dst.kind == OperandKind::Register && SizeRegisterTo(dst, dst.bits, dstSized, ctx))
        {
            body << ext << dstSized << ", " << srcSized << '\n';
            return;
        }

        std::string scratch = ScratchRegAt(dst.bits, ctx);
        if (!SizeMemoryTo(dst, dst.bits, dstSized, ctx))
        {
            emitPlain();
            return;
        }
        body << ext << scratch << ", " << srcSized << '\n';
        body << "mov " << dstSized << ", " << scratch << '\n';
        return;
    }

    // Truncamento: move apenas os dst.bits menos significativos.
    std::string srcSized;
    if (!SizeOperandTo(src, dst.bits, srcSized, ctx))
    {
        emitPlain();
        return;
    }

    if (SizeOperandTo(dst, dst.bits, dstSized, ctx))
    {
        bool dstIsMemory = dst.kind == OperandKind::Memory;
        bool srcIsMemory = src.kind == OperandKind::Memory;

        if (!(dstIsMemory && srcIsMemory))
        {
            body << "mov " << dstSized << ", " << srcSized << '\n';
            return;
        }
    }

    // memoria <- memoria nao existe: passa por um registrador temporario.
    std::string scratch = ScratchRegAt(dst.bits, ctx);
    if (!SizeMemoryTo(dst, dst.bits, dstSized, ctx))
    {
        emitPlain();
        return;
    }
    body << "mov " << scratch << ", " << srcSized << '\n';
    body << "mov " << dstSized << ", " << scratch << '\n';
}

void HandleMul(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(4u));
    auto args = Split(correctLine, ',');

    if (args.size() < 2) throw std::runtime_error("Invalid mul");
    ctx.bodies["text"] << "imul " << ResolveOperand(args[0], ctx) << ", " << ResolveOperand(args[1], ctx) << '\n';
}

void HandleDiv(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(4u));
    auto args = Split(correctLine, ',');

    if (args.empty()) throw std::runtime_error("Invalid div");

    if (args.size() >= 2)
    {
        std::string dest = ResolveOperand(args[0], ctx);
        std::string src = ResolveOperand(args[1], ctx);

        bool is64 = (ctx.arch == Architecture::X86_64) &&
                    ((dest.size() >= 3 && dest[0] == 'r' && std::isalpha(static_cast<unsigned char>(dest[1]))) ||
                     dest.starts_with("qword"));

        std::string ax = is64 ? "rax" : "eax";
        std::string cx = is64 ? "rcx" : "ecx";
        std::string ext = is64 ? "cqo" : "cdq";

        ctx.bodies["text"] << "mov " << ax << ", " << dest << '\n';
        ctx.bodies["text"] << ext << '\n';

        if (!src.empty() && (std::isdigit(static_cast<unsigned char>(src[0])) || src[0] == '-'))
        {
            ctx.bodies["text"] << "mov " << cx << ", " << src << '\n';
            ctx.bodies["text"] << "idiv " << cx << '\n';
        }
        else
        {
            ctx.bodies["text"] << "idiv " << src << '\n';
        }
        ctx.bodies["text"] << "mov " << dest << ", " << ax << '\n';
    }
    else
    {
        std::string src = ResolveOperand(args[0], ctx);
        if (ctx.arch == Architecture::X86)
        {
            ctx.bodies["text"] << "cdq\n";
            ctx.bodies["text"] << "idiv " << src << '\n';
        }
        else
        {
            ctx.bodies["text"] << "cqo\n";
            ctx.bodies["text"] << "idiv " << src << '\n';
        }
    }
}

void HandleNeg(std::string_view line, NasmContext& ctx)
{
    std::string target = std::string(line.substr(4u));
    while (!target.empty() && std::isspace(static_cast<unsigned char>(target.front())))
        target.erase(target.begin());
    while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back())))
        target.pop_back();

    ctx.bodies["text"] << "neg " << ResolveOperand(target, ctx) << '\n';
}

void HandleNot(std::string_view line, NasmContext& ctx)
{
    std::string target = std::string(line.substr(4u));
    while (!target.empty() && std::isspace(static_cast<unsigned char>(target.front())))
        target.erase(target.begin());
    while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back())))
        target.pop_back();

    ctx.bodies["text"] << "not " << ResolveOperand(target, ctx) << '\n';
}

void HandleLNot(std::string_view line, NasmContext& ctx)
{
    std::string target = Trim(line.substr(4u));
    std::string operand = ResolveOperand(target, ctx);

    int bits = (ctx.arch == Architecture::X86) ? 32 : 64;
    if (auto it = ctx.currentRegMap.find(target); it != ctx.currentRegMap.end())
    {
        bits = it->second.bits;
    }
    else if (auto it = ctx.variablesEmitted.find(target); it != ctx.variablesEmitted.end())
    {
        bits = static_cast<int>(it->second.Bits);
    }
    else if (auto it = ctx.externalVariables.find(target); it != ctx.externalVariables.end())
    {
        bits = static_cast<int>(it->second.Bits);
    }

    std::string byteReg = (ctx.arch == Architecture::X86) ? "al" : "r14b";
    std::string wordReg = (ctx.arch == Architecture::X86) ? "ax" : "r14w";
    std::string dwordReg = (ctx.arch == Architecture::X86) ? "eax" : "r14d";
    std::string qwordReg = (ctx.arch == Architecture::X86) ? "eax" : "r14";

    auto& body = ctx.bodies["text"];
    body << "cmp " << operand << ", 0\n";
    body << "sete " << byteReg << '\n';

    if (bits <= 8)
    {
        body << "mov " << operand << ", " << byteReg << '\n';
    }
    else if (bits <= 16)
    {
        body << "movzx " << dwordReg << ", " << byteReg << '\n';
        body << "mov " << operand << ", " << wordReg << '\n';
    }
    else if (bits <= 32)
    {
        body << "movzx " << dwordReg << ", " << byteReg << '\n';
        body << "mov " << operand << ", " << dwordReg << '\n';
    }
    else
    {
        body << "movzx " << qwordReg << ", " << byteReg << '\n';
        body << "mov " << operand << ", " << qwordReg << '\n';
    }
}

void HandleImport(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(7u));
    auto [externName, pos] = SplitOne(correctLine, ',');

    if (pos == std::string::npos)
    {
        Logger::Error("import expects 'import <symbol>,<abi>,<size>': " + std::string(line));
        return;
    }

    const std::string rest = correctLine.substr(pos + 1);
    auto [abiText, abiPos] = SplitOne(rest, ',');
    const std::string sizeText = (abiPos == std::string::npos)
        ? std::string("0")
        : rest.substr(abiPos + 1);

    const Abi abi = AbiFromString(Trim(abiText)).value_or(Abi::Cdecl);
    const std::size_t size = ConvertString<std::size_t>(Trim(sizeText));

    if (!ctx.externsEmitted.contains(externName))
    {
        ctx.externsEmitted[externName] = ExternInfo{ abi, size };
        std::string target = Mangled(externName, static_cast<std::uint32_t>(size), abi, ctx.arch);
        ctx.header << "extern " << target << '\n';
    }
}

void HandleAbi(std::string_view line, NasmContext& ctx)
{
    // 'abi <funcao>,<convencao>,<bytes>' registra a ABI de funcoes conhecidas.
    std::string rest(line.substr(4u));
    auto [name, pos] = SplitOne(rest, ',');

    if (pos == std::string::npos)
    {
        return;
    }

    const std::string tail = rest.substr(pos + 1);
    auto [abiText, abiPos] = SplitOne(tail, ',');
    const std::string sizeText = (abiPos == std::string::npos) ? std::string("0") : tail.substr(abiPos + 1);

    ctx.callAbis[Trim(name)] = ExternInfo{
        AbiFromString(Trim(abiText)).value_or(Abi::Cdecl),
        ConvertString<std::size_t>(Trim(sizeText))
    };
}

void HandleFuncAbi(std::string_view line, NasmContext& ctx)
{
    // 'funcabi <funcao>,<convencao>,<bytes>' define a ABI da funcao atual.
    std::string rest(line.substr(8u));
    auto [name, pos] = SplitOne(rest, ',');

    if (pos == std::string::npos)
    {
        return;
    }

    const std::string tail = rest.substr(pos + 1);
    auto [abiText, abiPos] = SplitOne(tail, ',');
    const std::string sizeText = (abiPos == std::string::npos) ? std::string("0") : tail.substr(abiPos + 1);

    ctx.currentFuncAbi = AbiFromString(Trim(abiText)).value_or(Abi::Cdecl);
    ctx.currentFuncStack = ConvertString<std::size_t>(Trim(sizeText));
    ctx.callAbis[Trim(name)] = ExternInfo{ ctx.currentFuncAbi, ctx.currentFuncStack };
}

void HandleAddr(std::string_view line, NasmContext& ctx)
{
    // 'addr <dst>,<simbolo>' carrega o endereco de um global.
    std::string rest(line.substr(4u));
    auto [dstRaw, pos] = SplitOne(rest, ',');

    if (pos == std::string::npos)
    {
        Logger::Error("addr expects 'addr <dst>,<symbol>': " + std::string(line));
        return;
    }

    const std::string dst = Trim(dstRaw);
    const std::string symbol = Trim(rest.substr(pos + 1));
    OperandInfo info = InspectOperand(dst, ctx);
    auto& body = ctx.bodies["text"];

    if (info.kind == OperandKind::Memory)
    {
        std::string base = MemoryBase(info, ctx);
        if (ctx.arch == Architecture::X86_64)
        {
            body << "lea rax, [rel " << symbol << "]\n";
            body << "mov qword " << base << ", rax\n";
        }
        else
        {
            body << "lea eax, [" << symbol << "]\n";
            body << "mov dword " << base << ", eax\n";
            body << "mov dword " << OffsetMemory(base, 4) << ", 0\n";
        }
        return;
    }

    std::string sized;
    if (ctx.arch == Architecture::X86_64)
    {
        if (SizeRegisterTo(info, 64, sized, ctx))
            body << "lea " << sized << ", [rel " << symbol << "]\n";
    }
    else if (SizeRegisterTo(info, 32, sized, ctx))
    {
        body << "lea " << sized << ", [" << symbol << "]\n";
    }
}

void HandleDLoad(std::string_view line, NasmContext& ctx)
{
    // 'dload <dst>,<ponteiro>,<tipo>' le *ponteiro.
    std::string rest(line.substr(6u));
    auto args = Split(rest, ',');

    if (args.size() < 3) return;

    const std::string dst = Trim(args[0]);
    const std::string pointer = Trim(args[1]);
    auto type = TypeParser::Parse(Trim(args[2]), PointerBits(ctx.arch));

    if (!type.has_value())
    {
        Logger::Error("dload: tipo invalido em '" + std::string(line) + "'");
        return;
    }

    const int bits = static_cast<int>(type->Bits);
    const int width = std::min(bits, 64);
    const bool isSigned = type->IsSigned();
    auto& body = ctx.bodies["text"];

    const std::string ptrReg = (ctx.arch == Architecture::X86_64) ? "r15" : "eax";
    LoadPointer(pointer, ptrReg, ctx);

    OperandInfo dstInfo = InspectOperand(dst, ctx);

    if (width > 32)
    {
        std::string base = (dstInfo.kind == OperandKind::Memory)
            ? MemoryBase(dstInfo, ctx)
            : "[" + dst + "]";

        if (ctx.arch == Architecture::X86_64)
        {
            body << "mov rax, qword [" << ptrReg << "]\n";
            body << "mov qword " << base << ", rax\n";
        }
        else
        {
            body << "mov edi, dword [" << ptrReg << "]\n";
            body << "mov dword " << base << ", edi\n";
            body << "mov edi, dword [" << ptrReg << "+4]\n";
            body << "mov dword " << OffsetMemory(base, 4) << ", edi\n";
        }
        return;
    }

    std::string dstReg;
    if (!SizeRegisterTo(dstInfo, 32, dstReg, ctx))
    {
        Logger::Error("dload: destino invalido em '" + std::string(line) + "'");
        return;
    }

    if (width >= 32)
        body << "mov " << dstReg << ", dword [" << ptrReg << "]\n";
    else
        body << (isSigned ? "movsx " : "movzx ") << dstReg << ", "
             << NasmSize(width) << " [" << ptrReg << "]\n";
}

void HandleDStore(std::string_view line, NasmContext& ctx)
{
    // 'dstore <ponteiro>,<valor>,<tipo>' escreve *ponteiro.
    std::string rest(line.substr(7u));
    auto args = Split(rest, ',');

    if (args.size() < 3) return;

    const std::string pointer = Trim(args[0]);
    const std::string value = Trim(args[1]);
    auto type = TypeParser::Parse(Trim(args[2]), PointerBits(ctx.arch));

    if (!type.has_value())
    {
        Logger::Error("dstore: tipo invalido em '" + std::string(line) + "'");
        return;
    }

    const int bits = std::min<int>(static_cast<int>(type->Bits), 64);
    auto& body = ctx.bodies["text"];

    const std::string ptrReg = (ctx.arch == Architecture::X86_64) ? "r15" : "eax";
    LoadPointer(pointer, ptrReg, ctx);

    if (bits > 32)
    {
        if (ctx.arch == Architecture::X86_64)
        {
            Load64IntoX64(value, "rax", false, ctx);
            body << "mov qword [" << ptrReg << "], rax\n";
        }
        else
        {
            LoadPairEaxEdx(value, ctx, false);
            body << "mov dword [" << ptrReg << "], eax\n";
            body << "mov dword [" << ptrReg << "+4], edx\n";
        }
        return;
    }

    long long immediate = 0;
    if (IsNumericLiteral(value) && ParseImmediate(value, immediate))
    {
        body << "mov " << NasmSize(bits) << " [" << ptrReg << "], " << value << '\n';
        return;
    }

    OperandInfo src = InspectOperand(value, ctx);
    std::string source;

    if (src.kind == OperandKind::Memory)
    {
        const std::string scratch = (ctx.arch == Architecture::X86_64) ? "r14d" : "edi";
        std::string base = MemoryBase(src, ctx);
        const int srcBits = (src.bits > 0) ? std::min(src.bits, bits) : bits;
        body << "mov " << scratch << ", " << NasmSize(srcBits) << " " << base << '\n';
        source = scratch;
    }
    else if (!SizeRegisterTo(src, bits, source, ctx))
    {
        source = ResolveOperand(value, ctx);
    }

    body << "mov " << NasmSize(bits) << " [" << ptrReg << "], " << source << '\n';
}

void HandleMov64(std::string_view line, NasmContext& ctx)
{
    // 'mov64 <dst>,<src>[,<tipo do src>]'
    std::string rest(line.substr(6u));
    auto args = Split(rest, ',');

    if (args.size() < 2) return;

    const std::string dst = Trim(args[0]);
    const std::string src = Trim(args[1]);

    std::optional<Type> srcType;
    if (args.size() >= 3)
    {
        const std::string typeText = Trim(args[2]);
        if (!typeText.empty())
        {
            srcType = TypeParser::Parse(typeText, PointerBits(ctx.arch));
        }
    }

    OperandInfo dstInfo = InspectOperand(dst, ctx);
    auto& body = ctx.bodies["text"];
    const Type* known = srcType ? &*srcType : nullptr;

    if (ctx.arch == Architecture::X86_64)
    {
        Load64IntoX64(src, "rax", dstInfo.isSigned, ctx, known);

        if (dstInfo.kind == OperandKind::Memory)
        {
            body << "mov qword " << MemoryBase(dstInfo, ctx) << ", rax\n";
        }
        else
        {
            std::string sized;
            if (SizeRegisterTo(dstInfo, 64, sized, ctx))
                body << "mov " << sized << ", rax\n";
        }
        return;
    }

    const bool extendSigned = known ? known->IsSigned() : dstInfo.isSigned;
    LoadPairEaxEdx(src, ctx, extendSigned, known);

    if (dstInfo.kind == OperandKind::Memory)
    {
        StorePairTo(dst, "eax", "edx", ctx);
    }
    else
    {
        std::string sized;
        if (SizeRegisterTo(dstInfo, 32, sized, ctx))
            body << "mov " << sized << ", eax\n";
    }
}

void HandleRet64(std::string_view line, NasmContext& ctx)
{
    // 'ret64 <valor>,<tipo>' devolve um valor de 64 bits (rax ou edx:eax).
    std::string rest(line.substr(6u));
    auto args = Split(rest, ',');

    if (args.size() < 2) return;

    const std::string value = Trim(args[0]);
    auto type = TypeParser::Parse(Trim(args[1]), PointerBits(ctx.arch));

    if (ctx.arch == Architecture::X86_64)
    {
        Load64IntoX64(value, "rax", false, ctx, type ? &*type : nullptr);
    }
    else
    {
        const Type* known = type ? &*type : nullptr;
        LoadPairEaxEdx(value, ctx, known ? known->IsSigned() : false, known);
    }

    EmitEpilogue(ctx);

    if (ctx.arch == Architecture::X86 &&
        ctx.currentFuncAbi != Abi::Cdecl &&
        ctx.currentFuncStack > 0)
    {
        ctx.bodies["text"] << "ret " << ctx.currentFuncStack << '\n';
        return;
    }

    ctx.bodies["text"] << "ret\n";
}

void HandleWideBinary(std::string_view line, NasmContext& ctx)
{
    const auto space = line.find(' ');
    if (space == std::string_view::npos) return;

    const std::string op(line.substr(0, space));
    const std::string rest(line.substr(space + 1));
    auto args = Split(rest, ',');

    if (args.size() < 3)
    {
        Logger::Error("wide op expects '<op>64 <dst>,<a>,<b>': " + std::string(line));
        return;
    }

    const std::string dst = Trim(args[0]);
    const std::string a = Trim(args[1]);
    const std::string b = Trim(args[2]);
    auto& body = ctx.bodies["text"];

    if (ctx.arch == Architecture::X86_64)
    {
        if (op == "div64" || op == "divu64")
        {
            std::string divisor = WideOperandX64(b, ctx, "r14");
            Load64IntoX64(a, "rax", false, ctx);
            body << "push rdx\n";

            // idiv/div nao aceitam imediato: passa por r14.
            if (IsNumericLiteral(divisor))
            {
                body << "mov r14, " << divisor << '\n';
                divisor = "r14";
            }

            body << (op == "div64" ? "cqo\n" : "xor edx, edx\n");
            body << (op == "div64" ? "idiv " : "div ") << divisor << '\n';
            body << "pop rdx\n";
            StoreRaxTo(dst, ctx);
            return;
        }

        Load64IntoX64(a, "rax", false, ctx);

        if (op == "add64" || op == "sub64" || op == "and64" || op == "or64" || op == "xor64")
        {
            const std::string mnemonic = op.substr(0, op.size() - 2);
            // WideOperandX64 pode emitir instrucoes; resolve antes de escrever a linha.
            const std::string rhs = WideOperandX64(b, ctx, "r14");
            body << mnemonic << " rax, " << rhs << '\n';
            StoreRaxTo(dst, ctx);
            return;
        }

        if (op == "mul64")
        {
            const std::string rhs = WideOperandX64(b, ctx, "r14");
            body << "imul rax, " << rhs << '\n';
            StoreRaxTo(dst, ctx);
            return;
        }

        if (op == "shl64" || op == "shr64" || op == "sar64")
        {
            long long count = 0;
            if (!ParseImmediate(b, count))
            {
                Logger::Error("wide shift requires a constant count: " + std::string(line));
                return;
            }

            if (count >= 64)
            {
                body << (op == "sar64" ? "sar rax, 63\n" : "xor eax, eax\n");
            }
            else if (count > 0)
            {
                body << op.substr(0, 3) << " rax, " << count << '\n';
            }

            StoreRaxTo(dst, ctx);
            return;
        }

        Logger::Error("unsupported wide op '" + op + "'");
        return;
    }

    // x86: pares de 32 bits.
    if (op == "div64" || op == "divu64")
    {
        const HalfValue halvesA = Split64(a, ctx);
        const HalfValue halvesB = Split64(b, ctx);

        body << "push " << halvesB.hi << '\n';
        body << "push " << halvesB.lo << '\n';
        body << "push " << halvesA.hi << '\n';
        body << "push " << halvesA.lo << '\n';

        OperandInfo dstInfo = InspectOperand(dst, ctx);
        body << "lea eax, " << MemoryBase(dstInfo, ctx) << '\n';
        body << "push eax\n";
        body << "call " << (op == "div64" ? "__emux_idiv64" : "__emux_udiv64") << '\n';
        body << "add esp, 20\n";

        ctx.needsUId64Helper = true;
        if (op == "div64") ctx.needsId64Helper = true;
        return;
    }

    if (op == "add64" || op == "sub64" || op == "and64" || op == "or64" || op == "xor64")
    {
        const HalfValue halvesA = Split64(a, ctx);
        const HalfValue halvesB = Split64(b, ctx);
        const std::string mnemonic = op.substr(0, op.size() - 2);

        body << "mov eax, " << halvesA.lo << '\n';
        body << "mov edi, " << halvesA.hi << '\n';
        body << mnemonic << " eax, " << halvesB.lo << '\n';
        body << mnemonic << " edi, " << halvesB.hi << '\n';
        StorePairTo(dst, "eax", "edi", ctx);
        return;
    }

    if (op == "mul64")
    {
        const HalfValue halvesA = Split64(a, ctx);
        const HalfValue halvesB = Split64(b, ctx);

        body << "push ebx\npush esi\n";
        body << "mov eax, " << halvesA.lo << '\n';
        body << "mov ebx, eax\n";
        body << "imul ebx, " << halvesB.lo << '\n';
        body << "mov esi, " << halvesA.hi << '\n';
        body << "imul esi, " << halvesB.lo << '\n';
        body << "mov eax, " << halvesA.lo << '\n';
        body << "imul eax, " << halvesB.hi << '\n';
        body << "add esi, eax\n";
        StorePairTo(dst, "ebx", "esi", ctx);
        body << "pop esi\npop ebx\n";
        return;
    }

    if (op == "shl64" || op == "shr64" || op == "sar64")
    {
        long long count = 0;
        if (!ParseImmediate(b, count))
        {
            Logger::Error("wide shift requires a constant count: " + std::string(line));
            return;
        }

        const HalfValue halves = Split64(a, ctx);
        body << "mov eax, " << halves.lo << '\n';
        body << "mov edi, " << halves.hi << '\n';

        if (count >= 64)
        {
            if (op == "sar64") body << "sar edi, 31\nmov eax, edi\n";
            else body << "xor eax, eax\nxor edi, edi\n";
        }
        else if (count > 0 && count < 32)
        {
            if (op == "shl64") body << "shld edi, eax, " << count << "\nshl eax, " << count << '\n';
            else if (op == "shr64") body << "shrd eax, edi, " << count << "\nshr edi, " << count << '\n';
            else body << "shrd eax, edi, " << count << "\nsar edi, " << count << '\n';
        }
        else if (count == 32)
        {
            if (op == "shl64") body << "mov edi, eax\nxor eax, eax\n";
            else if (op == "shr64") body << "mov eax, edi\nxor edi, edi\n";
            else body << "mov eax, edi\nsar edi, 31\n";
        }
        else if (count > 32)
        {
            const long long remainder = count - 32;
            if (op == "shl64") body << "mov edi, eax\nshl edi, " << remainder << "\nxor eax, eax\n";
            else if (op == "shr64") body << "mov eax, edi\nshr eax, " << remainder << "\nxor edi, edi\n";
            else body << "mov eax, edi\nsar eax, " << remainder << "\nsar edi, 31\n";
        }

        StorePairTo(dst, "eax", "edi", ctx);
        return;
    }

    Logger::Error("unsupported wide op '" + op + "'");
}

void HandleWideNeg(std::string_view line, NasmContext& ctx)
{
    auto [opcode, _] = SplitOne(line);
    const std::string op(opcode);
    std::string rest(line.substr(op.size()));
    auto args = Split(rest, ',');

    if (args.size() < 2) return;

    const std::string dst = Trim(args[0]);
    const std::string a = Trim(args[1]);
    auto& body = ctx.bodies["text"];

    if (ctx.arch == Architecture::X86_64)
    {
        Load64IntoX64(a, "rax", false, ctx);
        body << (op == "neg64" ? "neg rax\n" : "not rax\n");
        StoreRaxTo(dst, ctx);
        return;
    }

    const HalfValue halves = Split64(a, ctx);
    body << "mov eax, " << halves.lo << '\n';
    body << "mov edi, " << halves.hi << '\n';

    if (op == "neg64")
        body << "not edi\nneg eax\nsbb edi, -1\n";
    else
        body << "not eax\nnot edi\n";

    StorePairTo(dst, "eax", "edi", ctx);
}

void HandleLNot64(std::string_view line, NasmContext& ctx)
{
    std::string rest(line.substr(6u));
    auto args = Split(rest, ',');

    if (args.size() < 2) return;

    const std::string dst = Trim(args[0]);
    const std::string a = Trim(args[1]);
    OperandInfo dstInfo = InspectOperand(dst, ctx);
    auto& body = ctx.bodies["text"];

    std::string dstReg;
    if (!SizeRegisterTo(dstInfo, 32, dstReg, ctx)) return;

    if (ctx.arch == Architecture::X86_64)
    {
        Load64IntoX64(a, "rax", false, ctx);
        body << "cmp rax, 0\nsete al\nmovzx eax, al\n";
    }
    else
    {
        const HalfValue halves = Split64(a, ctx);
        body << "mov eax, " << halves.lo << '\n';
        body << "or eax, " << halves.hi << '\n';
        body << "sete al\nmovzx eax, al\n";
    }

    if (dstReg != "eax") body << "mov " << dstReg << ", eax\n";
}

void HandleCall(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(5u));
    auto [callName, callPos] = SplitOne(correctLine, ',');

    std::string resolved = ResolveOperand(callName, ctx);
    bool isSolved = (resolved != callName);

    bool isExtern = ctx.externsEmitted.contains(callName);
    ExternInfo externInfo;
    if (isExtern)
    {
        externInfo = ctx.externsEmitted[callName];
    }
    else if (auto it = ctx.callAbis.find(callName); it != ctx.callAbis.end())
    {
        externInfo = it->second;
    }

    if (!isSolved && !isExtern)
    {
        // Pode estar definida neste módulo ou em outro; o emissor decide
        // entre 'global' e 'extern' no fim da tradução.
        ctx.callTargets.insert(Exported(callName, ctx.arch));
    }

    std::vector<std::string> args;
    if (correctLine.size() > callName.size() && callPos != std::string::npos)
    {
        args = Split(correctLine.substr(callPos + 1), ',');
    }

    if (ctx.arch == Architecture::X86)
    {
        // x86: stdcall/cdecl recebem tudo na pilha; fastcall usa ecx/edx para
        // os dois primeiros argumentos que cabem em 32 bits.
        const Abi abi = externInfo.CallingConvention;

        std::vector<std::string> registerArgs;
        std::vector<std::string> stackArgs;

        for (const std::string& arg : args)
        {
            if (abi == Abi::Fastcall && registerArgs.size() < 2 && ArgWidth(arg, ctx) <= 32)
                registerArgs.push_back(arg);
            else
                stackArgs.push_back(arg);
        }

        std::size_t stackBytes = 0;
        for (auto it = stackArgs.rbegin(); it != stackArgs.rend(); ++it)
        {
            if (ArgWidth(*it, ctx) > 32)
            {
                const HalfValue halves = Split64(*it, ctx);
                ctx.bodies["text"] << "push " << halves.hi << '\n';
                ctx.bodies["text"] << "push " << halves.lo << '\n';
                stackBytes += 8;
            }
            else
            {
                ctx.bodies["text"] << "push " << ResolveOperand(*it, ctx) << '\n';
                stackBytes += 4;
            }
        }

        static const char* fastRegs[2] = { "ecx", "edx" };
        for (std::size_t i = 0; i < registerArgs.size(); ++i)
        {
            OperandInfo info = InspectOperand(registerArgs[i], ctx);
            const int width = ArgWidth(registerArgs[i], ctx);

            if (width >= 32)
            {
                ctx.bodies["text"] << "mov " << fastRegs[i] << ", "
                    << ResolveOperand(registerArgs[i], ctx) << '\n';
                continue;
            }

            std::string sized;
            if (info.kind == OperandKind::Memory) SizeMemoryTo(info, width, sized, ctx);
            else if (info.kind == OperandKind::Register) SizeRegisterTo(info, width, sized, ctx);
            else sized = registerArgs[i];

            ctx.bodies["text"] << (info.isSigned ? "movsx " : "movzx ") << fastRegs[i] << ", " << sized << '\n';
        }

        std::string target;
        if (isSolved)
        {
            target = resolved;
        }
        else if (isExtern)
        {
            target = Mangled(callName, static_cast<std::uint32_t>(externInfo.Size), abi, ctx.arch);
        }
        else
        {
            target = Exported(callName, ctx.arch);
        }

        ctx.bodies["text"] << "call " << target << '\n';

        if (abi == Abi::Cdecl && stackBytes > 0)
        {
            ctx.bodies["text"] << "add esp, " << stackBytes << '\n';
        }
    }
    else
    {
        size_t numArgs = args.size();
        size_t stackSpace = 32;
        if (numArgs > 4)
        {
            stackSpace += 8 * (numArgs - 4);
        }

        // Funcoes com frame entram alinhadas a 16; as sem frame (push rbp) ficam
        // deslocadas em 8, entao o espaco reservado precisa compensar.
        const size_t rspMisalignment = ctx.frameEmitted ? 0 : 8;
        if ((stackSpace + rspMisalignment) % 16 != 0)
        {
            stackSpace += 8;
        }

        ctx.bodies["text"] << "sub rsp, " << stackSpace << '\n';

        const std::vector<std::string> x64ArgRegs64 = {"rcx", "rdx", "r8", "r9"};
        const std::vector<std::string> x64ArgRegs32 = {"ecx", "edx", "r8d", "r9d"};

        auto emitArg = [&](const std::string& arg, std::size_t index, std::size_t stackOffset)
        {
            std::string cleanArg = arg;

            // Labels de dados (exir bytes) sao sempre passados por endereco.
            if (ctx.dataLabelsEmitted.contains(cleanArg))
            {
                if (stackOffset == 0)
                {
                    ctx.bodies["text"] << "lea " << x64ArgRegs64[index] << ", [" << cleanArg << "]\n";
                }
                else
                {
                    ctx.bodies["text"] << "lea rax, [" << cleanArg << "]\n";
                    ctx.bodies["text"] << "mov qword [rsp + " << stackOffset << "], rax\n";
                }
                return;
            }

            OperandInfo info = InspectOperand(cleanArg, ctx);
            long long immediate = 0;
            const bool isImm = IsNumericLiteral(cleanArg) && ParseImmediate(cleanArg, immediate);

            int argBits = info.bits > 0 ? info.bits : 32;
            if (isImm) argBits = 64;

            const bool use64 = argBits > 32;
            bool narrowExtend = false;
            std::string val;

            if (isImm)
            {
                val = cleanArg;
            }
            else if (info.kind == OperandKind::Memory)
            {
                const std::string base = MemoryBase(info, ctx);
                if (use64) val = "qword " + base;
                else if (argBits >= 32) val = "dword " + base;
                else { val = NasmSize(argBits) + " " + base; narrowExtend = true; }
            }
            else if (info.kind == OperandKind::Register)
            {
                narrowExtend = argBits < 32;
                if (info.fixedWidthReg)
                {
                    std::string regName = info.baseName;
                    if (auto it = ctx.currentRegMap.find(info.baseName);
                        it != ctx.currentRegMap.end() && it->second.isMemory)
                    {
                        regName = it->second.memoryText;
                    }

                    std::string sized;
                    val = SizeFixedWidthReg(regName, use64 ? 64 : 32, sized, ctx) ? sized : regName;
                }
                else
                {
                    val = ResolveOperand(cleanArg + (use64 ? ":64" : ":32"), ctx);
                }
            }
            else
            {
                val = ResolveOperand(cleanArg, ctx);
            }

            if (stackOffset == 0)
            {
                const std::string& reg = use64 ? x64ArgRegs64[index] : x64ArgRegs32[index];
                if (narrowExtend)
                {
                    ctx.bodies["text"] << (info.isSigned ? "movsx " : "movzx ")
                        << reg << ", " << val << '\n';
                }
                else if (val != reg)
                {
                    ctx.bodies["text"] << "mov " << reg << ", " << val << '\n';
                }
            }
            else
            {
                const std::string scratch = use64 ? "rax" : "eax";

                if (narrowExtend)
                {
                    ctx.bodies["text"] << (info.isSigned ? "movsx " : "movzx ")
                        << "eax, " << val << '\n';
                }
                else if (val != scratch)
                {
                    ctx.bodies["text"] << "mov " << scratch << ", " << val << '\n';
                }

                ctx.bodies["text"] << "mov " << (use64 ? "qword" : "dword")
                    << " [rsp + " << stackOffset << "], " << scratch << '\n';
            }
        };

        for (size_t i = 0; i < numArgs; ++i)
        {
            if (i < 4)
            {
                emitArg(args[i], i, 0);
            }
            else
            {
                emitArg(args[i], 0, 32 + 8 * (i - 4));
            }
        }

        std::string target = isExtern ? std::string(callName) : (isSolved ? resolved : std::string(callName));
        ctx.bodies["text"] << "call " << target << '\n';
        ctx.bodies["text"] << "add rsp, " << stackSpace << '\n';
    }
}

void HandleFunc(std::string_view line, NasmContext& ctx)
{
    std::string name(line.substr(5u));
    if (ctx.functionsEmitted.contains(name))
    {
        Logger::Error("Function redefined: " + name);
    }
    ctx.functionsEmitted.emplace(name);

    ctx.currentRegMap.clear();
    ctx.currentSpillCount = 0;
    ctx.frameEmitted = false;
    ctx.currentFuncAbi = Abi::Cdecl;
    ctx.currentFuncStack = 0;

    const std::string exported = Exported(name, ctx.arch);

    if (auto aliases = ctx.exportAliases.find(name); aliases != ctx.exportAliases.end())
    {
        for (const std::string& alias : aliases->second)
        {
            const std::string symbol = Exported(alias, ctx.arch);
            if (symbol != name && symbol != exported)
            {
                ctx.bodies["text"] << symbol << ":\n";
            }
        }
    }

    if (exported != name)
    {
        ctx.bodies["text"] << exported << ":\n";
    }
    ctx.bodies["text"] << name << ":\n";
}

void HandleExport(std::string_view line, NasmContext& ctx)
{
    std::string rest(line.substr(7u));
    auto [name, position] = SplitOne(rest, ',');

    if (position == std::string::npos)
    {
        Logger::Error("export expects 'export <symbol>,<alias>': " + std::string(line));
        return;
    }

    std::string alias = Trim(rest.substr(position + 1));
    if (name.empty() || alias.empty())
    {
        Logger::Error("export expects a symbol and an alias: " + std::string(line));
        return;
    }

    ctx.exportAliases[name].push_back(std::move(alias));
}

void HandleRet(std::string_view, NasmContext& ctx)
{
    EmitEpilogue(ctx);

    if (ctx.arch == Architecture::X86 &&
        ctx.currentFuncAbi != Abi::Cdecl &&
        ctx.currentFuncStack > 0)
    {
        ctx.bodies["text"] << "ret " << ctx.currentFuncStack << '\n';
        return;
    }

    ctx.bodies["text"] << "ret\n";
}

void HandleExit(std::string_view line, NasmContext& ctx)
{
    std::string externName = "ExitProcess";
    std::string codeArg = ResolveOperand(line.substr(5u), ctx);

    if (!ctx.externsEmitted.contains(externName))
    {
        ctx.externsEmitted[externName] = ExternInfo{ Abi::Stdcall, 4 };
        std::string extTarget = Mangled(externName, 4, Abi::Stdcall, ctx.arch);
        ctx.header << "extern " << extTarget << '\n';
    }

    auto& body = ctx.bodies["text"];
    if (ctx.arch == Architecture::X86)
    {
        body << "push " << codeArg << '\n';
        body << "call " << Mangled(externName, 4, Abi::Stdcall, ctx.arch) << '\n';
    }
    else
    {
        const size_t space = ctx.frameEmitted ? 32 : 40;
        body << "sub rsp, " << space << "\n";
        body << "mov ecx, " << codeArg << '\n';
        body << "call " << externName << '\n';
        body << "add rsp, " << space << "\n";
    }
}

bool Dispatch(std::string_view line, NasmContext& ctx)
{
    auto [opcode, _] = SplitOne(line);

    using HandlerFunc = void(*)(std::string_view, NasmContext&);
    static const std::unordered_map<std::string_view, HandlerFunc> handlers = {
        { "regs",    &HandleRegs },
        { "vars",    &HandleVars },
        { "extvars", &HandleExtVars },
        { "args",    &HandleArgs },
        { "bytes",   &HandleBytes },
        { "returns", [](std::string_view, NasmContext&) {} },
        { "shl",     &HandleDoubleMath },
        { "shr",     &HandleDoubleMath },
        { "or",      &HandleDoubleMath },
        { "xor",     &HandleDoubleMath },
        { "and",     &HandleDoubleMath },
        { "movzx",   &HandleDoubleMath },
        { "movsx",   &HandleDoubleMath },
        { "mov",     &HandleMov },
        { "add",     &HandleDoubleMath },
        { "sub",     &HandleDoubleMath },
        { "mul",     &HandleMul },
        { "div",     &HandleDiv },
        { "neg",     &HandleNeg },
        { "not",     &HandleNot },
        { "lnot",    &HandleLNot },
        { "import",  &HandleImport },
        { "export",  &HandleExport },
        { "abi",     &HandleAbi },
        { "funcabi", &HandleFuncAbi },
        { "call",    &HandleCall },
        { "func",    &HandleFunc },
        { "ret",     &HandleRet },
        { "ret64",   &HandleRet64 },
        { "exit",    &HandleExit },
        { "addr",    &HandleAddr },
        { "dload",   &HandleDLoad },
        { "dstore",  &HandleDStore },
        { "mov64",   &HandleMov64 },
        { "add64",   &HandleWideBinary },
        { "sub64",   &HandleWideBinary },
        { "mul64",   &HandleWideBinary },
        { "div64",   &HandleWideBinary },
        { "divu64",  &HandleWideBinary },
        { "and64",   &HandleWideBinary },
        { "or64",    &HandleWideBinary },
        { "xor64",   &HandleWideBinary },
        { "shl64",   &HandleWideBinary },
        { "shr64",   &HandleWideBinary },
        { "sar64",   &HandleWideBinary },
        { "neg64",   &HandleWideNeg },
        { "not64",   &HandleWideNeg },
        { "lnot64",  &HandleLNot64 }
    };

    auto it = handlers.find(opcode);
    if (it != handlers.end())
    {
        it->second(line, ctx);
        return true;
    }

    return false;
}

} // namespace NasmInstructions

} // namespace Emux
