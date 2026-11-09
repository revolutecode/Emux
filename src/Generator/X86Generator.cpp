#include "Emux/Generator/X86Generator.hpp"

namespace Emux
{

X86Generator::X86Generator()
    : NasmGeneratorBase(Architecture::X86)
{
}

void X86Generator::EmitArchHelpers(std::ostringstream&)
{
}

void X86Generator::EmitFunction(std::ostringstream& out, const Exir::Function& function)
{
    out << function.Name << ":\n";
    out << "    ret\n";
}

} // namespace Emux
