#pragma once

#include "Emux/Generator/NasmGeneratorBase.hpp"

namespace Emux
{

class X64Generator final : public NasmGeneratorBase
{
public:
    X64Generator();

protected:
    void EmitFunction(std::ostringstream& text, const Exir::Function& function) override;
    void EmitArchHelpers(std::ostringstream& text) override;

private:
    void EmitInstruction(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitTerminator(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);

    // Aritmetica, comparacoes e conversoes dispatchadas pela classe do tipo:
    // escalar (<= 64 bits), regiao larga (> 64 bits), float ou ponto fixo.
    void EmitArithmetic(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitCompare(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitUnary(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitConv(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitCast(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);

    void EmitWideBinary(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitWideCompare(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitWideUnary(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    // Copia uma regiao larga. Em 'load' a origem e o conteudo de um ponteiro;
    // nos demais casos e o proprio valor.
    void EmitWideCopy(
        std::ostringstream& out,
        const Exir::Value& source,
        std::uint32_t dstId,
        std::uint32_t bytes,
        StackSlots& slots,
        bool sourceIsPointer
    );
    void EmitWideExtendFill(
        std::ostringstream& out,
        const Exir::Value& source,
        const Type& sourceType,
        std::uint32_t dstId,
        std::uint32_t dstBytes,
        bool signedFill,
        StackSlots& slots
    );

    // Restaura os bits de padding acima de Bits no byte mais alto de uma
    // regiao larga: sem isto uma conta assinada deixa o sinal a descoberto.
    void EmitWideMask(std::ostringstream& out, std::uint32_t dstId, const Type& type, StackSlots& slots);

    // Endereco de armazenamento de um operando, para as rotinas do runtime.
    void EmitOperandAddress(
        std::ostringstream& out,
        const Exir::Value& value,
        StackSlots& slots,
        const char* reg,
        std::size_t scratchIndex
    );

    void EmitFloatBinary(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitFixedBinary(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitShiftCount(
        std::ostringstream& out,
        const Exir::Value& count,
        std::uint32_t bytes,
        StackSlots& slots
    );

    // Le um operando escalonar para um registo no padrao canonico do tipo.
    void EmitOperand(
        std::ostringstream& out,
        const Exir::Value& value,
        const Type& type,
        const char* reg,
        StackSlots& slots
    );

    // Le o ponteiro de um operando (temp, slot, global ou imediato) num registo.
    void EmitPointer(std::ostringstream& out, const Exir::Value& value, const char* reg, StackSlots& slots);

    // Reescreve o slot com o padding limpo depois de uma rotina que so escreve
    // os bytes do tipo.
    void EmitNormalizeSlot(
        std::ostringstream& out,
        std::uint32_t dstId,
        const Type& type,
        StackSlots& slots
    );

    // Reserva (ou recupera) o slot do destino e devolve a sua memoria.
    std::string SlotMemory(StackSlots& slots, std::uint32_t id, std::uint32_t bytes);
    std::string ScratchMemory(StackSlots& slots, std::size_t index);
    std::string NewLabel(std::string_view prefix);

    std::size_t m_LabelCounter = 0;
};

} // namespace Emux
