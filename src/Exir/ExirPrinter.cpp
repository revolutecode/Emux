#include "Emux/Exir/ExirPrinter.hpp"

#include <iomanip>
#include <sstream>

namespace Emux::Exir
{

namespace
{

std::string_view OpCodeToString(OpCode op)
{
    switch (op)
    {
        case OpCode::Addr: return "addr";
        case OpCode::Load: return "load";
        case OpCode::Store: return "store";
        case OpCode::Gep: return "gep";
        case OpCode::Memcpy: return "memcpy";
        case OpCode::Memset: return "memset";
        case OpCode::Memcmp: return "memcmp";

        case OpCode::Add: return "add";
        case OpCode::Sub: return "sub";
        case OpCode::Mul: return "mul";
        case OpCode::UDiv: return "udiv";
        case OpCode::SDiv: return "sdiv";
        case OpCode::URem: return "urem";
        case OpCode::SRem: return "srem";
        case OpCode::And: return "and";
        case OpCode::Or: return "or";
        case OpCode::Xor: return "xor";
        case OpCode::Shl: return "shl";
        case OpCode::LShr: return "lshr";
        case OpCode::AShr: return "ashr";

        case OpCode::Neg: return "neg";
        case OpCode::Not: return "not";

        case OpCode::CmpEq: return "cmp eq";
        case OpCode::CmpNe: return "cmp ne";
        case OpCode::CmpUlt: return "cmp ult";
        case OpCode::CmpSlt: return "cmp slt";
        case OpCode::CmpUle: return "cmp ule";
        case OpCode::CmpSle: return "cmp sle";
        case OpCode::CmpUgt: return "cmp ugt";
        case OpCode::CmpSgt: return "cmp sgt";
        case OpCode::CmpUge: return "cmp uge";
        case OpCode::CmpSge: return "cmp sge";

        case OpCode::Trunc: return "trunc";
        case OpCode::ZExt: return "zext";
        case OpCode::SExt: return "sext";
        case OpCode::BitCast: return "bitcast";
        case OpCode::PtrToInt: return "ptrtoint";
        case OpCode::IntToPtr: return "inttoptr";

        case OpCode::Extract: return "extract";
        case OpCode::Insert: return "insert";

        case OpCode::AtomicLoad: return "atomic_load";
        case OpCode::AtomicStore: return "atomic_store";
        case OpCode::AtomicCas: return "atomic_cas";
        case OpCode::Fence: return "fence";

        case OpCode::Br: return "br";
        case OpCode::CBr: return "cbr";
        case OpCode::Switch: return "switch";
        case OpCode::Select: return "select";
        case OpCode::Ret: return "ret";
        case OpCode::Unreachable: return "unreachable";

        case OpCode::Call: return "call";
        case OpCode::CallPtr: return "call_ptr";
        case OpCode::Asm: return "asm";
        case OpCode::Trap: return "trap";
    }
    return "unknown";
}

} // namespace

void Printer::Print(const Value& val, std::ostream& out)
{
    switch (val.Kind)
    {
        case ValueKind::None:
            out << "none";
            break;
        case ValueKind::Temp:
            out << "%t" << val.Id;
            break;
        case ValueKind::Constant:
            if (val.Ty.IsSigned())
            {
                out << val.SImm;
            }
            else
            {
                out << val.Imm;
            }
            break;
        case ValueKind::GlobalRef:
            out << "@global_" << val.Id;
            break;
        case ValueKind::LocalSlot:
            out << "%local_" << val.Id;
            break;
        case ValueKind::BlockRef:
            out << "label %bb_" << val.Id;
            break;
    }
}

void Printer::Print(const Instruction& inst, std::ostream& out)
{
    // Se a instrucao produz um destino (%tN = ...)
    if (inst.ResultTy.IsValid() && inst.Op != OpCode::Store &&
        inst.Op != OpCode::Memcpy && inst.Op != OpCode::Memset &&
        !inst.IsTerminator())
    {
        out << "%t" << inst.DstId << " = ";
    }

    out << OpCodeToString(inst.Op);

    // Tipo de resultado quando aplicavel
    if (inst.ResultTy.IsValid())
    {
        out << ' ' << inst.ResultTy.ToString();
    }

    // Impressao de operandos por opcode
    switch (inst.Op)
    {
        case OpCode::Br:
            out << " label %bb_" << inst.Operands[0].Id;
            break;

        case OpCode::CBr:
            out << " u1 ";
            Print(inst.Operands[0], out);
            out << ", label %bb_" << inst.Operands[1].Id
                << ", label %bb_" << inst.Operands[2].Id;
            break;

        case OpCode::Ret:
            if (inst.Operands[0].Kind != ValueKind::None)
            {
                out << ' ';
                Print(inst.Operands[0], out);
            }
            break;

        case OpCode::Store:
            out << ' ';
            Print(inst.Operands[0], out); // valor
            out << ", ";
            Print(inst.Operands[1], out); // destino
            break;

        case OpCode::Load:
            out << ", ";
            Print(inst.Operands[0], out); // ponteiro
            break;

        case OpCode::Memcpy:
        case OpCode::Memcmp:
            out << ' ';
            Print(inst.Operands[0], out);
            out << ", ";
            Print(inst.Operands[1], out);
            out << ", ";
            Print(inst.Operands[2], out);
            break;

        case OpCode::Memset:
            out << ' ';
            Print(inst.Operands[0], out);
            out << ", ";
            Print(inst.Operands[1], out);
            out << ", ";
            Print(inst.Operands[2], out);
            break;

        case OpCode::Call:
            if (inst.Extra)
            {
                out << " @" << inst.Extra->Text << " (";
                for (std::size_t i = 0; i < inst.Extra->Arguments.size(); ++i)
                {
                    if (i > 0) out << ", ";
                    out << inst.Extra->Arguments[i].Ty.ToString() << ' ';
                    Print(inst.Extra->Arguments[i], out);
                }
                out << ')';
            }
            break;

        default:
            // Binarias normais: op <T> %a, %b
            if (inst.Operands[0].Kind != ValueKind::None)
            {
                out << ' ';
                Print(inst.Operands[0], out);
            }
            if (inst.Operands[1].Kind != ValueKind::None)
            {
                out << ", ";
                Print(inst.Operands[1], out);
            }
            break;
    }

    if (inst.SourceLine > 0)
    {
        out << "  ; !dbg line " << inst.SourceLine;
    }
}

void Printer::Print(const BasicBlock& block, std::ostream& out)
{
    out << "bb_" << block.Id;
    if (!block.Name.empty())
    {
        out << " ; " << block.Name;
    }
    out << ":\n";

    for (const auto& inst : block.Instructions)
    {
        out << "    ";
        Print(inst, out);
        out << '\n';
    }

    // Terminador obrigatorio
    out << "    ";
    Print(block.Terminator, out);
    out << '\n';
}

void Printer::Print(const Function& function, std::ostream& out)
{
    out << "func @" << function.Name << " : " << function.ReturnType.ToString() << " (";
    for (std::size_t i = 0; i < function.Parameters.size(); ++i)
    {
        if (i > 0) out << ", ";
        out << function.Parameters[i].Ty.ToString() << " %" << function.Parameters[i].Name;
    }
    out << ") {\n";

    // Declaracao dos slots locais
    for (const auto& local : function.Locals)
    {
        out << "    local %local_" << local.Id << " : " << local.Ty.ToString()
            << " ; " << local.Name << '\n';
    }

    if (!function.Locals.empty() && !function.Blocks.empty())
    {
        out << '\n';
    }

    // Blocos basicos
    for (const auto& block : function.Blocks)
    {
        if (block)
        {
            Print(*block, out);
        }
    }

    out << "}\n";
}

void Printer::Print(const Module& module, std::ostream& out)
{
    out << "; Module: " << module.Name << " (pointer_bits=" << module.TargetPointerBits << ")\n\n";

    for (const auto& import : module.Imports)
    {
        out << "import @" << import.Name << " = \"" << import.Symbol << "\"\n";
    }

    for (const auto& global : module.Globals)
    {
        out << (global.IsConst ? "const " : "global ")
            << "@global_" << global.Id << " (" << global.Name << ") : "
            << global.Ty.ToString();
        if (!global.InitialData.empty())
        {
            out << " [ " << global.InitialData.size() << " bytes ]";
        }
        out << '\n';
    }

    if (!module.Globals.empty() || !module.Imports.empty())
    {
        out << '\n';
    }

    for (const auto& function : module.Functions)
    {
        if (function)
        {
            Print(*function, out);
            out << '\n';
        }
    }
}

std::string Printer::ToString(const Module& module)
{
    std::ostringstream ss;
    Print(module, ss);
    return ss.str();
}

std::string Printer::ToString(const Function& function)
{
    std::ostringstream ss;
    Print(function, ss);
    return ss.str();
}

std::string Printer::ToString(const BasicBlock& block)
{
    std::ostringstream ss;
    Print(block, ss);
    return ss.str();
}

std::string Printer::ToString(const Instruction& inst)
{
    std::ostringstream ss;
    Print(inst, ss);
    return ss.str();
}

std::string Printer::ToString(const Value& val)
{
    std::ostringstream ss;
    Print(val, ss);
    return ss.str();
}

} // namespace Emux::Exir
