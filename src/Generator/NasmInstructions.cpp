#include "Emux/Generator/NasmInstructions.hpp"
#include "Emux/Generator/NasmRegisters.hpp"
#include "Emux/Generator/NasmUtils.hpp"
#include "Emux/Core/Logger.hpp"
#include <algorithm>
#include <cctype>
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

void HandleRegs(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(5u));
    auto decls = Split(correctLine, ',');

    const auto& physRegForms = GetPhysicalRegForms();
    std::size_t spillCount = 0;

    for (std::size_t i = 0; i < decls.size(); ++i)
    {
        auto [name, type] = ParseDecl(decls[i]);
        if (i < physRegForms.size())
        {
            ctx.currentRegMap[name] = { false, "", i, static_cast<int>(type.Bits) };
        }
        else
        {
            int slotBytes = (ctx.arch == Architecture::X86_64) ? 8 : 4;
            std::string baseReg = (ctx.arch == Architecture::X86_64) ? "rbp" : "ebp";
            std::string memText = NasmSize(type.Bits) + " [" + baseReg + "-" + std::to_string(slotBytes * (++spillCount)) + "]";
            ctx.currentRegMap[name] = { true, memText, 0, static_cast<int>(type.Bits) };
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

void HandleArgs(std::string_view line, NasmContext& ctx)
{
    std::string correctLine(line.substr(5u));
    auto decls = Split(correctLine, ',');

    for (std::size_t i = 0; i < decls.size(); ++i)
    {
        auto [name, type] = ParseDecl(decls[i]);

        if (ctx.arch == Architecture::X86)
        {
            std::string memText = NasmSize(type.Bits) + " [ebp+" + std::to_string(8 + 4 * i) + "]";
            ctx.currentRegMap[name] = { true, memText, 0, static_cast<int>(type.Bits) };
        }
        else
        {
            // x86_64: argumentos 0..3 passados em rcx, rdx, r8, r9
            std::string memText = NasmSize(type.Bits) + " [rbp+" + std::to_string(16 + 32 + 8 * i) + "]";
            ctx.currentRegMap[name] = { true, memText, 0, static_cast<int>(type.Bits) };
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

    if (ctx.arch == Architecture::X86 && (name == "main_Start" || name == "Main_Start"))
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
        { "mov",     &HandleDoubleMath },
        { "add",     &HandleDoubleMath },
        { "sub",     &HandleDoubleMath },
        { "mul",     &HandleMul },
        { "div",     &HandleDiv },
        { "neg",     &HandleNeg },
        { "not",     &HandleNot },
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
