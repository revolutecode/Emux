#pragma once

#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Generator/NasmGeneratorBase.hpp"

#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace Emux
{

// Baixa a Exir para NASM win32. Inteiros de 64 bits usam pares de 32 bits
// (lo/hi) e as convenções cdecl/stdcall/fastcall do Windows 32 bits.
class X86Generator final : public NasmGeneratorBase
{
public:
    X86Generator();

protected:
    void EmitFunction(std::ostringstream& text, const Exir::Function& function) override;
    void EmitArchHelpers(std::ostringstream& text) override;
    std::string FunctionSymbol(const Exir::Function& function) const override;
    std::string CallSymbol(const std::string& target) const override;

private:
    void EmitInstruction(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitBinary(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitWideBinary(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitUnary(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitWideUnary(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitCall(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void LoadValue(
        std::ostringstream& out,
        const std::string& operand,
        const Type& type,
        const std::string& reg32,
        StackSlots& slots
    );

    void LoadAddress(
        std::ostringstream& out,
        const std::string& operand,
        StackSlots& slots
    );

    void LoadWide(
        std::ostringstream& out,
        const std::string& operand,
        StackSlots& slots
    );

    void PushValue(
        std::ostringstream& out,
        const std::string& operand,
        const Type& type,
        StackSlots& slots,
        int& pushedBytes
    );

    void StoreResult(
        std::ostringstream& out,
        const std::string& destination,
        const Type& type,
        StackSlots& slots
    );

    void StoreToName(
        std::ostringstream& out,
        const std::string& name,
        const Type& type,
        StackSlots& slots
    );

    std::string MemoryOperand(const std::string& operand, StackSlots& slots, int extra = 0);
    std::string WideSource(
        std::ostringstream& out,
        const std::string& operand,
        StackSlots& slots,
        const std::string& scratch
    );
    std::string DataSymbol(std::string_view name);

    bool m_NeedsUdiv64 = false;
    bool m_NeedsIdiv64 = false;
};

} // namespace Emux
