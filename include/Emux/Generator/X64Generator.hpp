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

    // Converte entre inteiro, ponto fixo e float (qualquer largura) usando o
    // runtime em C compilado no link.
    void EmitNumericCast(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitBitCast(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitFloatBinary(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitFixedBinary(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    void EmitWideCopy(
        std::ostringstream& out,
        const std::string& source,
        const std::string& destination,
        const Type& sourceType,
        int bytes,
        StackSlots& slots
    );

    void EmitFloatUnary(
        std::ostringstream& out,
        const Exir::Instruction& instruction,
        StackSlots& slots
    );

    // Escreve os bytes de um literal na representacao de armazenamento do tipo
    // num slot pre-alocado.
    void MaterializeLiteral(
        std::ostringstream& out,
        const std::string& literal,
        const Type& type,
        StackSlots& slots,
        const std::string& slot
    );

    // Endereco de um operando numerico: lea para slots/globais e copia do
    // literal para um slot temporario quando necessario.
    void LoadNumericPointer(
        std::ostringstream& out,
        const std::string& operand,
        const Type& type,
        StackSlots& slots,
        const std::string& reg,
        const std::string& scratch
    );

    void EmitScalarStore(
        std::ostringstream& out,
        const std::string& mem,
        const Type& type,
        const std::string& reg64
    );

    void EmitExactStore(
        std::ostringstream& out,
        const std::string& mem,
        int bytes,
        const std::string& reg64
    );

    void EmitExactLoad(
        std::ostringstream& out,
        const std::string& mem,
        int bytes,
        const std::string& reg64
    );

    void EmitSignExtend(
        std::ostringstream& out,
        const std::string& reg64,
        int bits,
        bool signedLike
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

    void LoadFromMemory(
        std::ostringstream& out,
        const std::string& mem,
        const Type& type,
        const std::string& reg64
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
