#pragma once

#include "Emux/Generator/NasmGeneratorBase.hpp"

namespace Emux
{

class X86Generator final : public NasmGeneratorBase
{
public:
    X86Generator();

protected:
    void EmitFunction(std::ostringstream& text, const Exir::Function& function) override;
    void EmitArchHelpers(std::ostringstream& text) override;
};

} // namespace Emux
