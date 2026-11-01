#pragma once

#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Generator/NasmGeneratorBase.hpp"

#include <sstream>
#include <string>
#include <string_view>

namespace Emux
{

// Baixa a Exir para NASM win64 (ABI Microsoft x64).
class X64Generator final : public NasmGeneratorBase
{
public:
    X64Generator();

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

    void EmitWideConst(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitWideCast(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitTrunc(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitShiftCount(
        std::ostringstream& out,
        const std::string& operand,
        int bytes,
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
        const std::string& reg64,
        StackSlots& slots
    );

    void LoadAddress(
        std::ostringstream& out,
        const std::string& operand,
        StackSlots& slots
    );

    void EmitImmediate(
        std::ostringstream& out,
        const std::string& literal,
        const std::string& reg64
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

    std::string MemoryOperand(const std::string& operand, StackSlots& slots);
    std::string WideAddress(
        const std::string& operand,
        StackSlots& slots,
        const std::string& reg
    );
    std::string DataSymbol(std::string_view name);
    std::string NewLabel(std::string_view prefix);

    std::size_t m_LabelCounter = 0;
};

} // namespace Emux
