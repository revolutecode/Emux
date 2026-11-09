#pragma once

#include "Emux/Exir/Exir.hpp"

#include <ostream>
#include <string>

namespace Emux::Exir
{

// Formata uma instrucao, bloco basico, funcao ou modulo completo
// no formato textual canonico da Exir para fins de debug e inspecao.
class Printer
{
public:
    static void Print(const Module& module, std::ostream& out);
    static void Print(const Function& function, std::ostream& out);
    static void Print(const BasicBlock& block, std::ostream& out);
    static void Print(const Instruction& inst, std::ostream& out);
    static void Print(const Value& val, std::ostream& out);

    static std::string ToString(const Module& module);
    static std::string ToString(const Function& function);
    static std::string ToString(const BasicBlock& block);
    static std::string ToString(const Instruction& inst);
    static std::string ToString(const Value& val);
};

} // namespace Emux::Exir
