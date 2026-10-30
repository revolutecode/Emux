#include "Emux/Generator/NasmGenerator.hpp"
#include "Emux/Generator/NasmContext.hpp"
#include "Emux/Generator/NasmInstructions.hpp"
#include "Emux/Generator/NasmUtils.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>

namespace Emux
{

namespace
{

const char* const kUdiv64Helper = R"(
; __emux_udiv64(dst, a_lo, a_hi, b_lo, b_hi) -> *dst = a / b (sem sinal)
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
    jmp .done

.store_zero:
    mov edi, [ebp+8]
    mov dword [edi], 0
    mov dword [edi+4], 0

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
; __emux_idiv64(dst, a_lo, a_hi, b_lo, b_hi) -> *dst = a / b (com sinal)
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

} // namespace

NasmGenerator::NasmGenerator(Architecture arch)
    : m_Arch(arch)
{
}

std::string NasmGenerator::Translate(std::string_view code)
{
    NasmContext ctx(m_Arch);

    std::istringstream ir(code.data());
    std::string line;
    while (std::getline(ir, line))
    {
        if (line.empty()) continue;

        if (!NasmInstructions::Dispatch(line, ctx))
        {
            ctx.bodies["text"] << line << '\n';
        }
    }

    if (m_Arch == Architecture::X86)
    {
        if (ctx.needsUId64Helper)
        {
            ctx.bodies["text"] << kUdiv64Helper;
        }
        if (ctx.needsId64Helper)
        {
            ctx.bodies["text"] << kIdiv64Helper;
        }
    }

    std::unordered_set<std::string> defined;
    std::set<std::string> globals;

    for (const std::string& function : ctx.functionsEmitted)
    {
        std::string symbol = NasmUtils::Exported(function, m_Arch);
        defined.insert(symbol);
        globals.insert(symbol);
    }

    for (const auto& [variable, _] : ctx.variablesEmitted)
    {
        std::string symbol = NasmUtils::Exported(variable, m_Arch);
        defined.insert(symbol);

        // Temporarios ocultos do backend nao sao exportados.
        if (!variable.starts_with("__emux_"))
        {
            globals.insert(symbol);
        }
    }

    for (const std::string& label : ctx.dataLabelsEmitted)
    {
        std::string symbol = NasmUtils::Exported(label, m_Arch);
        defined.insert(symbol);
        globals.insert(symbol);
    }

    for (const auto& [name, aliases] : ctx.exportAliases)
    {
        for (const std::string& alias : aliases)
        {
            std::string symbol = NasmUtils::Exported(alias, m_Arch);
            defined.insert(symbol);
            globals.insert(symbol);
        }
    }

    for (const auto& [name, _] : ctx.externsEmitted)
    {
        defined.insert(NasmUtils::Exported(name, m_Arch));
    }

    std::set<std::string> externs;
    auto addExtern = [&](const std::string& symbol)
    {
        if (!defined.contains(symbol))
        {
            externs.insert(symbol);
        }
    };

    for (const auto& [variable, _] : ctx.externalVariables)
    {
        addExtern(NasmUtils::Exported(variable, m_Arch));
    }

    for (const std::string& target : ctx.callTargets)
    {
        addExtern(target);
    }

    for (const std::string& symbol : ctx.referencedSymbols)
    {
        addExtern(symbol);
    }

    std::ostringstream out;
    if (m_Arch == Architecture::X86_64)
    {
        out << "default rel\n";
    }

    for (const std::string& symbol : globals)
    {
        out << "global " << symbol << '\n';
    }

    for (const std::string& symbol : externs)
    {
        out << "extern " << symbol << '\n';
    }

    out << ctx.header.str() << '\n';

    for (auto& [sectionName, sectionBody] : ctx.bodies)
    {
        out << "section ." << sectionName << '\n';
        out << sectionBody.str() << '\n';
    }

    return out.str();
}

int NasmGenerator::Assemble(
    const std::filesystem::path& asmPath,
    const std::filesystem::path& objPath,
    Architecture arch,
    const std::filesystem::path& logPath
)
{
    const std::string format = (arch == Architecture::X86) ? "win32" : "win64";

    std::string command =
        "nasm -f " + format +
        " -o \"" + objPath.string() + "\"" +
        " \"" + asmPath.string() + "\"" +
        " > \"" + logPath.string() + "\" 2>&1";

    return std::system(command.c_str());
}

int NasmGenerator::Link(
    const std::vector<std::filesystem::path>& objects,
    const std::filesystem::path& output,
    Architecture arch,
    Subsystem subsystem,
    const std::vector<std::string>& libraries,
    const std::filesystem::path& logPath
)
{
    std::string command = "gcc ";
    if (arch == Architecture::X86)
    {
        command += "-m32 ";
    }

    for (const std::filesystem::path& object : objects)
    {
        command += "\"" + object.string() + "\" ";
    }

    const std::string entry = (arch == Architecture::X86) ? "_emux_entry" : "emux_entry";

    command +=
        "-o \"" + output.string() + "\" " +
        "-nostdlib -e " + entry + " -Wl,--entry=" + entry +
        " -Wl,--subsystem," + SubsystemName(subsystem) +
        " -lkernel32";

    std::unordered_set<std::string> linked = { "kernel32" };
    for (const std::string& library : libraries)
    {
        std::string name = library;
        for (const std::string& suffix : { ".dll", ".lib", ".a" })
        {
            if (name.size() > suffix.size() && name.ends_with(suffix))
            {
                name.resize(name.size() - suffix.size());
            }
        }

        if (name.empty() || !linked.insert(name).second)
        {
            continue;
        }

        command += " -l" + name;
    }

    command += " > \"" + logPath.string() + "\" 2>&1";

    return std::system(command.c_str());
}

bool NasmGenerator::WriteEntryStub(
    const std::filesystem::path& asmPath,
    Architecture arch,
    std::string_view entry
)
{
    std::ofstream file(asmPath);
    if (!file)
    {
        return false;
    }

    if (arch == Architecture::X86)
    {
        file
            << "global _emux_entry\n"
            << "extern _" << entry << "\n"
            << "extern _ExitProcess@4\n"
            << "section .text\n"
            << "_emux_entry:\n"
            << "call _" << entry << "\n"
            << "push eax\n"
            << "call _ExitProcess@4\n";
    }
    else
    {
        file
            << "default rel\n"
            << "global emux_entry\n"
            << "extern " << entry << "\n"
            << "extern ExitProcess\n"
            << "section .text\n"
            << "emux_entry:\n"
            << "and rsp, -16\n"
            << "sub rsp, 32\n"
            << "call " << entry << "\n"
            << "mov ecx, eax\n"
            << "call ExitProcess\n";
    }

    return static_cast<bool>(file);
}

}
