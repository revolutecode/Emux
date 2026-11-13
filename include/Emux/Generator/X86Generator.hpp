#pragma once

#include "Emux/Core/Abi.hpp"
#include "Emux/Exir/Exir.hpp"
#include "Emux/Generator/NasmGeneratorBase.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Emux
{

// Baixa a Exir para NASM win32/i386. Um valor ate 32 bits vive num dword do
// seu slot; os de 33 a 64 vivem num par edx:eax e as operacoes que nao cabem
// numa instruccao (divisao, multiplicacao e shifts de 64 bits) correm nas
// rotinas __emux_udiv64/__emux_idiv64 que EmitArchHelpers escreve no ficheiro.
class X86Generator final : public NasmGeneratorBase
{
public:
    X86Generator();

protected:
    void EmitFunction(std::ostringstream& text, const Exir::Function& function) override;
    void EmitArchHelpers(std::ostringstream& text) override;

private:
    // Onde um valor mora: um slot do frame ([ebp-N]) ou um simbolo de dados.
    // O offset em bytes e aplicado no texto do operando, porque em bits 32 nao
    // ha 'default rel' e [symbol+N] e o endereco absoluto + N.
    struct Address
    {
        std::string Symbol;
        int Offset = 0;
        bool FromFrame = false;
    };

    struct PendingArgument
    {
        const Exir::Value* Value;
        Type As;
        std::uint32_t Bytes;
    };

    using AddressMap = std::unordered_map<std::uint32_t, Address>;

    // Preparacao do frame e simbolos.
    void ReserveSlots(const Exir::Function& function, StackSlots& slots);
    void EmitPrologue(std::ostringstream& out, const Exir::Function& function, StackSlots& slots);
    void EmitEpilogue(std::ostringstream& out, const Exir::Function& function);
    void DeclareImport(const std::string& symbol);

    // Localizacao de armazenamento.
    Address ValueAddress(const Exir::Value& value, StackSlots& slots) const;
    Address ScratchAddress(std::uint32_t id, StackSlots& slots) const;
    Address FunctionParameter(std::size_t index, StackSlots& slots) const;
    bool TryStaticPointer(const Exir::Value& value, Address& address) const;
    std::string Memory(const Address& address, int extra = 0) const;
    std::string Immediate(std::uint64_t value, std::uint32_t bits) const;

    // Le um valor de ate 32 bits para um registo de 32 bits, na forma
    // canonica do tipo (zeros ou sinal acima de Bits).
    void LoadScalarFrom(const std::string& mem, const Type& type,
                        const std::string& reg, std::ostringstream& out) const;
    void LoadScalar(const Exir::Value& value,
                    const std::string& reg,
                    std::ostringstream& out,
                    StackSlots& slots) const;

    // Le um valor de 33 a 64 bits para o par low:high, ja isolado a largura.
    void LoadWideFrom(const std::string& mem, const Type& type,
                      const std::string& low, const std::string& high,
                      std::ostringstream& out) const;
    void LoadWide(const Exir::Value& value,
                  const std::string& low,
                  const std::string& high,
                  std::ostringstream& out,
                  StackSlots& slots) const;

    // Garante um par edx:eax em memoria; literais e valores estreitos passam
    // por um scratch, porque as operacoes de par usam sempre memoria.
    Address WideOperand(const Exir::Value& value,
                        std::uint32_t scratch,
                        std::ostringstream& out,
                        StackSlots& slots) const;

    void StoreScalarAt(const std::string& mem, std::uint32_t bytes,
                       const std::string& reg, std::ostringstream& out) const;

    void StoreWideAt(const std::string& mem, std::uint32_t bytes,
                     std::ostringstream& out) const;

    // Escreve eax, ou o par edx:eax, no destino segundo o tipo do resultado.
    void StoreResult(const Exir::Instruction& inst, std::ostringstream& out,
                     StackSlots& slots) const;

    void IsolateScalar(std::uint32_t bits, bool signedLike, const std::string& reg,
                       std::ostringstream& out) const;
    void IsolateWideIn(std::uint32_t bits, bool signedLike, const std::string& high,
                       std::ostringstream& out) const;

    // Operacoes.
    void LoadPointerInto(const std::string& reg, const Exir::Value& value,
                         std::ostringstream& out, StackSlots& slots) const;
    void EmitAddress(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitLoad(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitStore(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitArithmetic(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitPairLogic(const std::string& lowOp, const std::string& highOp,
                       const Address& a, const Address& b, std::ostringstream& out) const;
    void EmitWideMultiply(const Address& a, const Address& b, std::ostringstream& out,
                          StackSlots& slots) const;
    void EmitWideDivRem(const Exir::Instruction& inst, const Address& a, const Address& b,
                        bool isSigned, std::ostringstream& out, StackSlots& slots);
    void EmitShift(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitWideShift(Exir::OpCode op, const Address& value, const Type& type,
                       std::ostringstream& out) const;
    void EmitUnary(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitCompare(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitConversion(std::ostringstream& out, const Exir::Instruction& inst,
                        StackSlots& slots);
    void EmitBlockOperation(std::ostringstream& out, const Exir::Instruction& inst,
                             StackSlots& slots);
    void EmitMemcmp(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void EmitCall(std::ostringstream& out, const Exir::Instruction& inst, StackSlots& slots);
    void PushArgument(const PendingArgument& argument, std::ostringstream& out,
                      StackSlots& slots) const;
    void EmitInstruction(std::ostringstream& out, const Exir::Instruction& inst,
                         StackSlots& slots);
    void EmitTerminator(std::ostringstream& out, const Exir::Instruction& inst,
                        StackSlots& slots);

    // Simbolos e etiquetas.
    std::string FunctionSymbol(const Exir::Function& function) const;
    std::string CallTarget(const Exir::Instruction& inst, std::uint32_t& stackBytes,
                           Abi& abi) const;
    std::uint32_t CalleeStackBytes(const Exir::Function& function) const;
    const Exir::Function* FindFunction(std::string_view name) const;
    const Exir::Import* FindImport(std::string_view name) const;
    std::string GlobalName(std::uint32_t id) const;
    std::string BlockLabel(std::uint32_t blockId) const;
    std::string NewLabel(std::string_view prefix) const;

    AddressMap m_Addrs;                     // Enderecos conhecidos de %temp.
    std::unordered_set<std::string> m_Declared;
    std::uint32_t m_FunctionIndex = 0;
    mutable std::uint32_t m_LabelCounter = 0;
    bool m_NeedsUdiv64 = false;
    bool m_NeedsIdiv64 = false;
};

} // namespace Emux
