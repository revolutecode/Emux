#include "Emux/Generator/NasmRegisters.hpp"
#include "Emux/Generator/NasmUtils.hpp"
#include "Emux/Core/Logger.hpp"
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace Emux
{

namespace NasmRegisters
{

static const std::vector<std::vector<std::string>> s_PhysRegFormsX86 = {
    {"bl", "bx", "ebx"},
    {"cl", "cx", "ecx"},
    {"dl", "dx", "edx"},
    {"sil", "si", "esi"}
};

// x64: evita rcx/rdx/r8/r9 (argumentos) e r14 (scratch de lnot)
static const std::vector<std::vector<std::string>> s_PhysRegFormsX64 = {
    {"r10b", "r10w", "r10d", "r10"},
    {"r11b", "r11w", "r11d", "r11"},
    {"r12b", "r12w", "r12d", "r12"},
    {"r13b", "r13w", "r13d", "r13"}
};

static const std::unordered_map<std::string, std::vector<std::string>> s_ReservedRegForms = {
    { "rr0", {"al", "ax", "eax", "rax"} },
    { "rr1", {"dl", "dx", "edx", "rdx"} },
};

const std::vector<std::vector<std::string>>& GetPhysicalRegForms(Architecture arch)
{
    return arch == Architecture::X86 ? s_PhysRegFormsX86 : s_PhysRegFormsX64;
}

bool LooksLikeReg(std::string_view s)
{
    return s.size() >= 2 && s[0] == 'r' && std::isdigit(static_cast<unsigned char>(s[1]));
}

std::string SizedRegName(const std::vector<std::string>& forms, int bits, std::string_view baseName, Architecture arch)
{
    if (arch == Architecture::X86)
    {
        if (bits > 32)
        {
            throw std::runtime_error(
                "'" + std::string(baseName) + "': backend x86-32 nao suporta registrador de " +
                std::to_string(bits) + " bits");
        }
        int sizeIdx = (bits <= 8) ? 0 : (bits <= 16) ? 1 : 2;
        return forms.at(sizeIdx);
    }
    else
    {
        int sizeIdx = (bits <= 8) ? 0 : (bits <= 16) ? 1 : (bits <= 32) ? 2 : 3;
        return forms.at(sizeIdx);
    }
}

std::string ResolveOperand(std::string_view input, NasmContext& ctx)
{
    std::string str(input);

    auto colonPos = str.find(':');
    bool hasOverride = (colonPos != std::string::npos);
    std::string name = hasOverride ? str.substr(0, colonPos) : str;
    int overrideBits = hasOverride ? std::stoi(str.substr(colonPos + 1)) : 0;

    int defaultBits = (ctx.arch == Architecture::X86_64) ? 64 : 32;

    if (auto it = s_ReservedRegForms.find(name); it != s_ReservedRegForms.end())
    {
        return SizedRegName(it->second, hasOverride ? overrideBits : defaultBits, name, ctx.arch);
    }

    if (auto it = ctx.currentRegMap.find(name); it != ctx.currentRegMap.end())
    {
        const SlotInfo& slot = it->second;
        if (slot.isMemory)
        {
            if (hasOverride)
            {
                Logger::Error("'" + name + "' e memoria - nao aceita sufixo ':N'");
            }
            return slot.memoryText;
        }
        return SizedRegName(GetPhysicalRegForms(ctx.arch).at(slot.physIndex), hasOverride ? overrideBits : slot.bits, name, ctx.arch);
    }

    if (auto it = ctx.variablesEmitted.find(name); it != ctx.variablesEmitted.end())
    {
        if (hasOverride)
        {
            Logger::Error("'" + name + "' e variavel global - nao aceita sufixo ':N'");
        }
        std::stringstream sstr;
        sstr << NasmUtils::NasmSize(it->second.Bits);
        sstr << " [";
        if (ctx.arch == Architecture::X86_64)
        {
            sstr << "rel ";
        }
        sstr << name;
        sstr << "]";
        return sstr.str();
    }

    if (auto it = ctx.externalVariables.find(name); it != ctx.externalVariables.end())
    {
        if (hasOverride)
        {
            Logger::Error("'" + name + "' e variavel externa - nao aceita sufixo ':N'");
        }
        std::stringstream sstr;
        sstr << NasmUtils::NasmSize(it->second.Bits);
        sstr << " [";
        if (ctx.arch == Architecture::X86_64)
        {
            sstr << "rel ";
        }
        sstr << name;
        sstr << "]";
        return sstr.str();
    }

    if (LooksLikeReg(name))
    {
        Logger::Error("Registrador '" + name + "' usado sem ter sido declarado em 'regs' ou 'args'");
        return str;
    }

    // Identificador que não está declarado neste módulo: pode vir de outro
    // objeto. Guarda para emitir 'extern' no fim da tradução.
    if (!name.empty())
    {
        const unsigned char first = static_cast<unsigned char>(name.front());
        if (std::isalpha(first) || name.front() == '_')
        {
            if (ctx.arch == Architecture::X86 && name.front() != '_')
            {
                ctx.referencedSymbols.insert("_" + name);
            }
            else
            {
                ctx.referencedSymbols.insert(name);
            }
        }
    }

    return str;
}

} // namespace NasmRegisters

} // namespace Emux
