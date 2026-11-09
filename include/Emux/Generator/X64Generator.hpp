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
};

} // namespace Emux
