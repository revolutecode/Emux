#include "Emux/Generator/NasmPeephole.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Emux::NasmPeephole
{

namespace
{

std::string Trim(std::string_view s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string Lower(std::string_view s)
{
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Familia de registo independente da largura: rax/eax/ax/al -> "A". Permite
// rastrear o mesmo registo fisico em x64 e x86 sem confundir sub-registos.
std::string RegFamily(std::string_view token)
{
    static const std::map<std::string, std::string> kFamilies = {
        { "rax", "A" }, { "eax", "A" }, { "ax", "A" }, { "al", "A" }, { "ah", "A" },
        { "rbx", "B" }, { "ebx", "B" }, { "bx", "B" }, { "bl", "B" }, { "bh", "B" },
        { "rcx", "C" }, { "ecx", "C" }, { "cx", "C" }, { "cl", "C" }, { "ch", "C" },
        { "rdx", "D" }, { "edx", "D" }, { "dx", "D" }, { "dl", "D" }, { "dh", "D" },
        { "rsi", "SI" }, { "esi", "SI" }, { "si", "SI" }, { "sil", "SI" },
        { "rdi", "DI" }, { "edi", "DI" }, { "di", "DI" }, { "dil", "DI" },
        { "rbp", "BP" }, { "ebp", "BP" }, { "bp", "BP" }, { "bpl", "BP" },
        { "rsp", "SP" }, { "esp", "SP" }, { "sp", "SP" }, { "spl", "SP" },
        { "r8", "R8" }, { "r8d", "R8" }, { "r8w", "R8" }, { "r8b", "R8" },
        { "r9", "R9" }, { "r9d", "R9" }, { "r9w", "R9" }, { "r9b", "R9" },
        { "r10", "R10" }, { "r10d", "R10" }, { "r10w", "R10" }, { "r10b", "R10" },
        { "r11", "R11" }, { "r11d", "R11" }, { "r11w", "R11" }, { "r11b", "R11" },
        { "r12", "R12" }, { "r12d", "R12" }, { "r12w", "R12" }, { "r12b", "R12" },
        { "r13", "R13" }, { "r13d", "R13" }, { "r13w", "R13" }, { "r13b", "R13" },
        { "r14", "R14" }, { "r14d", "R14" }, { "r14w", "R14" }, { "r14b", "R14" },
        { "r15", "R15" }, { "r15d", "R15" }, { "r15w", "R15" }, { "r15b", "R15" }
    };

    const auto it = kFamilies.find(Lower(token));
    return it == kFamilies.end() ? std::string() : it->second;
}

// Separa o prefixo de tamanho (qword/dword/word/byte) de um operando de
// memoria e normaliza os espacos: "qword [rbp - 8]" -> size="qword",
// mem="[rbp-8]"). Devolve false quando o operando nao e memoria.
bool ParseMem(std::string_view operand, std::string& size, std::string& mem)
{
    std::string t = Trim(operand);
    const std::string lower = Lower(t);
    size.clear();

    for (const char* sz : { "qword", "dword", "word", "byte" })
    {
        const std::string prefix = std::string(sz) + " ";
        if (lower.rfind(prefix, 0) == 0)
        {
            size = sz;
            t = Trim(t.substr(prefix.size()));
            break;
        }
    }

    if (t.size() < 2 || t.front() != '[' || t.back() != ']')
    {
        return false;
    }

    mem.clear();
    for (const char c : t)
    {
        if (!std::isspace(static_cast<unsigned char>(c))) mem += c;
    }
    return true;
}

// So os slots da frame (rbp/ebp) sao rastreados para forwarding; memoria
// apontada por registos pode aliasar e e tratada de forma conservadora.
bool IsFrameSlot(const std::string& mem)
{
    return mem.rfind("[rbp", 0) == 0 || mem.rfind("[ebp", 0) == 0;
}

std::vector<std::string> SplitOperands(std::string_view rest)
{
    std::vector<std::string> ops;
    std::string cur;
    for (const char c : rest)
    {
        if (c == ',')
        {
            ops.push_back(Trim(cur));
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    if (!Trim(cur).empty()) ops.push_back(Trim(cur));
    return ops;
}

bool IsLabel(const std::string& t)
{
    if (t.empty() || t.back() != ':') return false;
    if (t.find(' ') != std::string::npos || t.find('\t') != std::string::npos) return false;
    return true;
}

} // namespace

std::string Optimize(std::string_view asmText)
{
    // slotToReg: memoria da frame -> {token do registo que a espelha, tamanho}.
    // regToSlot: familia do registo -> memoria que espelha (validade do facto).
    // regLea:    familia do registo -> endereco que ja contem (dedup de lea).
    std::map<std::string, std::pair<std::string, std::string>> slotToReg;
    std::map<std::string, std::string> regToSlot;
    std::map<std::string, std::string> regLea;

    auto clobberReg = [&](const std::string& fam)
    {
        if (fam.empty()) return;
        const auto it = regToSlot.find(fam);
        if (it != regToSlot.end())
        {
            const auto s = slotToReg.find(it->second);
            if (s != slotToReg.end() && RegFamily(s->second.first) == fam)
            {
                slotToReg.erase(s);
            }
            regToSlot.erase(it);
        }
        regLea.erase(fam);
    };

    auto clobberSlot = [&](const std::string& mem)
    {
        const auto s = slotToReg.find(mem);
        if (s == slotToReg.end()) return;
        const std::string fam = RegFamily(s->second.first);
        const auto r = regToSlot.find(fam);
        if (r != regToSlot.end() && r->second == mem) regToSlot.erase(r);
        slotToReg.erase(s);
    };

    auto resetAll = [&]()
    {
        slotToReg.clear();
        regToSlot.clear();
        regLea.clear();
    };

    std::vector<std::string> output;
    std::size_t pos = 0;

    while (pos <= asmText.size())
    {
        const std::size_t nl = asmText.find('\n', pos);
        const std::string_view raw =
            asmText.substr(pos, (nl == std::string_view::npos ? asmText.size() : nl) - pos);

        const std::string line(raw);
        const std::string t = Trim(line);

        bool drop = false;
        std::string replacement;

        if (t.empty() || t.front() == ';')
        {
            output.push_back(line);
            if (nl == std::string_view::npos) break;
            pos = nl + 1;
            continue;
        }

        if (IsLabel(t))
        {
            resetAll();
            output.push_back(line);
            if (nl == std::string_view::npos) break;
            pos = nl + 1;
            continue;
        }

        const std::size_t sp = t.find_first_of(" \t");
        const std::string mnem = Lower(sp == std::string::npos ? t : t.substr(0, sp));
        const std::string rest = sp == std::string::npos ? std::string() : Trim(t.substr(sp));
        const std::vector<std::string> ops = SplitOperands(rest);

        if (mnem == "mov" && ops.size() == 2)
        {
            std::string dstSize, dstMem, srcSize, srcMem;
            const bool dstIsMem = ParseMem(ops[0], dstSize, dstMem);
            const bool srcIsMem = ParseMem(ops[1], srcSize, srcMem);
            const std::string dstFam = RegFamily(ops[0]);
            const std::string srcFam = RegFamily(ops[1]);

            if (dstIsMem)
            {
                if (IsFrameSlot(dstMem) && !srcFam.empty())
                {
                    clobberSlot(dstMem);
                    slotToReg[dstMem] = { ops[1], dstSize };
                    regToSlot[srcFam] = dstMem;
                }
                else
                {
                    resetAll();
                }
            }
            else if (!dstFam.empty())
            {
                if (srcIsMem && IsFrameSlot(srcMem))
                {
                    const auto s = slotToReg.find(srcMem);
                    const bool valid =
                        s != slotToReg.end() && s->second.second == srcSize &&
                        [&]()
                        {
                            const auto r = regToSlot.find(RegFamily(s->second.first));
                            return r != regToSlot.end() && r->second == srcMem;
                        }();

                    if (valid)
                    {
                        const std::string tok = s->second.first;
                        if (RegFamily(tok) == dstFam)
                        {
                            drop = true;
                        }
                        else
                        {
                            replacement = "    mov " + ops[0] + ", " + tok;
                        }
                        regToSlot[dstFam] = srcMem;
                    }
                    else
                    {
                        clobberReg(dstFam);
                        slotToReg[srcMem] = { ops[0], srcSize };
                        regToSlot[dstFam] = srcMem;
                    }
                }
                else if (srcIsMem)
                {
                    clobberReg(dstFam);
                }
                else if (!srcFam.empty())
                {
                    if (ops[0] == ops[1])
                    {
                        drop = true;
                    }
                    else
                    {
                        clobberReg(dstFam);
                        const auto r = regToSlot.find(srcFam);
                        if (r != regToSlot.end()) regToSlot[dstFam] = r->second;
                        const auto l = regLea.find(srcFam);
                        if (l != regLea.end()) regLea[dstFam] = l->second;
                    }
                }
                else
                {
                    clobberReg(dstFam);
                }
            }
            else
            {
                resetAll();
            }
        }
        else if (mnem == "lea" && ops.size() == 2)
        {
            std::string sz, mem;
            const std::string dstFam = RegFamily(ops[0]);
            if (!dstFam.empty() && ParseMem(ops[1], sz, mem))
            {
                const auto l = regLea.find(dstFam);
                if (l != regLea.end() && l->second == mem)
                {
                    drop = true;
                }
                else
                {
                    clobberReg(dstFam);
                    regLea[dstFam] = mem;
                }
            }
            else if (!dstFam.empty())
            {
                clobberReg(dstFam);
            }
            else
            {
                resetAll();
            }
        }
        else if (mnem == "call" || mnem == "ret" || mnem == "ud2" || mnem == "leave" ||
                 mnem == "syscall" || mnem == "int" || mnem == "loop" ||
                 mnem == "loope" || mnem == "loopne" || mnem == "rep" ||
                 mnem == "repe" || mnem == "repne" || mnem == "repz" || mnem == "repnz" ||
                 (!mnem.empty() && mnem.front() == 'j'))
        {
            resetAll();
        }
        else if (mnem == "mul" || mnem == "div" || mnem == "idiv")
        {
            clobberReg("A");
            clobberReg("D");
        }
        else if (mnem == "imul")
        {
            if (ops.size() >= 2)
            {
                const std::string fam = RegFamily(ops[0]);
                if (!fam.empty()) clobberReg(fam); else resetAll();
            }
            else
            {
                clobberReg("A");
                clobberReg("D");
            }
        }
        else if (mnem == "cqo" || mnem == "cdq")
        {
            clobberReg("D");
        }
        else if (mnem == "xchg")
        {
            for (const std::string& o : ops)
            {
                const std::string fam = RegFamily(o);
                if (!fam.empty()) clobberReg(fam);
            }
        }
        else if (mnem == "cmp" || mnem == "test" || mnem == "push" || mnem == "nop")
        {
            // Apenas leem: os factos de registo/memoria mantem-se.
        }
        else if (mnem == "add" || mnem == "sub" || mnem == "and" || mnem == "or" ||
                 mnem == "xor" || mnem == "adc" || mnem == "sbb" || mnem == "shl" ||
                 mnem == "shr" || mnem == "sar" || mnem == "neg" || mnem == "not" ||
                 mnem == "inc" || mnem == "dec" || mnem == "movzx" || mnem == "movsx" ||
                 mnem == "pop" || mnem.rfind("cmov", 0) == 0 || mnem.rfind("set", 0) == 0)
        {
            if (!ops.empty())
            {
                std::string sz, mem;
                if (ParseMem(ops[0], sz, mem))
                {
                    if (IsFrameSlot(mem)) clobberSlot(mem); else resetAll();
                }
                else
                {
                    const std::string fam = RegFamily(ops[0]);
                    if (!fam.empty()) clobberReg(fam); else resetAll();
                }
            }
        }
        else
        {
            // Instrucao desconhecida (diretivas, dados, etc.): conservador.
            resetAll();
        }

        if (drop)
        {
            // linha eliminada
        }
        else if (!replacement.empty())
        {
            output.push_back(replacement);
        }
        else
        {
            output.push_back(line);
        }

        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }

    std::string result;
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        if (i > 0) result += '\n';
        result += output[i];
    }
    return result;
}

} // namespace Emux::NasmPeephole