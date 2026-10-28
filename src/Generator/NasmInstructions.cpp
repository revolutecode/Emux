#include "Emux/Generator/NasmInstructions.hpp"
#include "Emux/Generator/NasmRegisters.hpp"
#include "Emux/Generator/NasmUtils.hpp"
#include "Emux/Core/Logger.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <functional>
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
        auto [name, type] = ParseDecl(decls[i]);
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
        auto [name, type] = ParseDecl(decl);
        ctx.variablesEmitted[name] = type;
        ctx.bodies["bss"] << name << ": res" << NasmSize(type.Bits)[0] << " 1\n";
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

    for (std::size_t i = 0; i < decls.size(); ++i)
    {
        auto [name, type] = ParseDecl(decls[i]);
        int bits = static_cast<int>(type.Bits);

        if (bits > 64)
        {
            throw std::runtime_error("Argument '" + name + "' larger than 64 bits is not supported by the nasm backend yet");
        }

        if (ctx.arch == Architecture::X86)
        {
            std::string memText = NasmSize(bits) + " [ebp+" + std::to_string(8 + 4 * i) + "]";
            ctx.currentRegMap[name] = { true, memText, 0, bits, type.IsSigned() };
        }
        else if (i < 4)
        {
            int sizeIdx = bits <= 8 ? 0 : bits <= 16 ? 1 : bits <= 32 ? 2 : 3;
            ctx.currentRegMap[name] = { true, x64ArgRegs[i][sizeIdx], 0, bits, type.IsSigned() };
        }
        else
        {
            // Acima de 4 args vao na pilha: [rsp+32+8*(i-4)] antes do call -> [rbp+48+8*(i-4)] depois do frame
            std::string memText = NasmSize(bits) + " [rbp+" + std::to_string(16 + 8 * i) + "]";
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

bool SizeFixedWidthReg(std::string_view text, int bits, std::string& out, NasmContext& ctx)
{
    for (const auto& forms : ArgRegForms())
    {
        if (std::find(forms.begin(), forms.end(), text) == forms.end()) continue;

        int sizeIdx = bits <= 8 ? 0 : bits <= 16 ? 1 : bits <= 32 ? 2 : 3;
        if (ctx.arch == Architecture::X86 && sizeIdx > 2) return false;

        out = forms.at(sizeIdx);
        return true;
    }
    return false;
}

bool SizeRegisterTo(const OperandInfo& info, int bits, std::string& out, NasmContext& ctx)
{
    if (info.kind != OperandKind::Register) return false;

    if (info.fixedWidthReg)
    {
        return SizeFixedWidthReg(info.baseName, bits, out, ctx);
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
    auto [stype, newPos] = SplitOne(correctLine.substr(pos + 1), ',');
    auto [ssize, _] = SplitOne(correctLine.substr(pos + newPos + 2), ',');

    ExternType type = (stype == "cdecl") ? ExternType::CDECL : ExternType::STDCALL;
    std::size_t size = ConvertString<std::size_t>(ssize);

    if (!ctx.externsEmitted.contains(externName))
    {
        ctx.externsEmitted[externName] = { type, size };
        std::string target = Mangled(externName, size, type == ExternType::STDCALL, ctx.arch);
        ctx.header << "extern " << target << '\n';
    }
}

void HandleCall(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(5u));
    auto [callName, callPos] = SplitOne(correctLine, ',');

    std::string resolved = ResolveOperand(callName, ctx);
    bool isSolved = (resolved != callName);

    bool isExtern = ctx.externsEmitted.contains(callName);
    std::size_t extSize = isExtern ? ctx.externsEmitted[callName].second : 0;
    bool isStdcall = isExtern && (ctx.externsEmitted[callName].first == ExternType::STDCALL);

    std::vector<std::string> args;
    if (correctLine.size() > callName.size() && callPos != std::string::npos)
    {
        args = Split(correctLine.substr(callPos + 1), ',');
    }

    if (ctx.arch == Architecture::X86)
    {
        auto pushArgs = args;
        std::reverse(pushArgs.begin(), pushArgs.end());

        for (const std::string& arg : pushArgs)
        {
            ctx.bodies["text"] << "push " << ResolveOperand(arg, ctx) << '\n';
        }

        std::string target;
        if (isSolved)
        {
            target = resolved;
        }
        else if (isExtern)
        {
            target = Mangled(callName, extSize, isStdcall, ctx.arch);
        }
        else
        {
            target = callName;
        }

        ctx.bodies["text"] << "call " << target << '\n';

        if (isExtern && !isStdcall && !args.empty())
        {
            ctx.bodies["text"] << "add esp, " << (4 * args.size()) << '\n';
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
        if (stackSpace % 16 != 0)
        {
            stackSpace += (16 - (stackSpace % 16));
        }

        ctx.bodies["text"] << "sub rsp, " << stackSpace << '\n';

        const std::vector<std::string> x64ArgRegs64 = {"rcx", "rdx", "r8", "r9"};
        const std::vector<std::string> x64ArgRegs32 = {"ecx", "edx", "r8d", "r9d"};

        auto emitArg = [&](const std::string& arg, const std::string& reg64, const std::string& reg32, size_t stackOffset)
        {
            std::string cleanArg = arg;
            bool isDataLabel = ctx.dataLabelsEmitted.contains(cleanArg);
            bool isVar = ctx.variablesEmitted.contains(cleanArg);

            if (stackOffset == 0)
            {
                if (isDataLabel || isVar)
                {
                    ctx.bodies["text"] << "lea " << reg64 << ", [" << cleanArg << "]\n";
                }
                else if (cleanArg == "rr0")
                {
                    ctx.bodies["text"] << "mov " << reg64 << ", rax\n";
                }
                else
                {
                    std::string val = ResolveOperand(cleanArg, ctx);
                    ctx.bodies["text"] << "mov " << reg32 << ", " << val << '\n';
                }
            }
            else
            {
                if (isDataLabel || isVar)
                {
                    ctx.bodies["text"] << "lea rax, [" << cleanArg << "]\n";
                    ctx.bodies["text"] << "mov [rsp + " << stackOffset << "], rax\n";
                }
                else if (cleanArg == "rr0")
                {
                    ctx.bodies["text"] << "mov [rsp + " << stackOffset << "], rax\n";
                }
                else
                {
                    std::string val = ResolveOperand(cleanArg, ctx);
                    ctx.bodies["text"] << "mov dword [rsp + " << stackOffset << "], " << val << '\n';
                }
            }
        };

        for (size_t i = 0; i < numArgs; ++i)
        {
            if (i < 4)
            {
                emitArg(args[i], x64ArgRegs64[i], x64ArgRegs32[i], 0);
            }
            else
            {
                size_t offset = 32 + 8 * (i - 4);
                emitArg(args[i], "", "", offset);
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

    if (ctx.arch == Architecture::X86 && (name == "main_Start" || name == "main__Start"))
    {
        ctx.bodies["text"] << "_" << name << ":\n";
    }
    ctx.bodies["text"] << name << ":\n";
}

void HandleRet(std::string_view, NasmContext& ctx)
{
    if (ctx.frameEmitted)
    {
        if (ctx.arch == Architecture::X86)
        {
            ctx.bodies["text"] << "mov esp, ebp\npop ebp\n";
        }
        else
        {
            ctx.bodies["text"] << "mov rsp, rbp\npop rbp\n";
        }
    }
    ctx.bodies["text"] << "ret\n";
}

void HandleExit(std::string_view line, NasmContext& ctx)
{
    std::string externName = "ExitProcess";
    std::string codeArg = ResolveOperand(line.substr(5u), ctx);

    if (!ctx.externsEmitted.contains(externName))
    {
        ctx.externsEmitted[externName] = { ExternType::STDCALL, 4 };
        std::string extTarget = Mangled(externName, 4, true, ctx.arch);
        ctx.header << "extern " << extTarget << '\n';
    }

    auto& body = ctx.bodies["text"];
    if (ctx.arch == Architecture::X86)
    {
        body << "push " << codeArg << '\n';
        body << "call " << Mangled(externName, 4, true, ctx.arch) << '\n';
    }
    else
    {
        body << "sub rsp, 40\n";
        body << "mov ecx, " << codeArg << '\n';
        body << "call " << externName << '\n';
        body << "add rsp, 40\n";
    }
}

bool Dispatch(std::string_view line, NasmContext& ctx)
{
    auto [opcode, _] = SplitOne(line);

    using HandlerFunc = void(*)(std::string_view, NasmContext&);
    static const std::unordered_map<std::string_view, HandlerFunc> handlers = {
        { "regs",    &HandleRegs },
        { "vars",    &HandleVars },
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
        { "call",    &HandleCall },
        { "func",    &HandleFunc },
        { "ret",     &HandleRet },
        { "exit",    &HandleExit }
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
