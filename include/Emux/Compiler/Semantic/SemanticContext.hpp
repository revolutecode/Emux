#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Emux/Compiler/Scope.hpp"
#include "Emux/Compiler/Semantic/FunctionCallSignature.hpp"
#include "Emux/Compiler/Semantic/FunctionSignature.hpp"
#include "Emux/Compiler/Semantic/ImportInfo.hpp"
#include "Emux/Compiler/Type/Type.hpp"

namespace Emux
{

// Tabelas compartilhadas por todos os módulos. É o resultado da análise
// semântica do programa inteiro, não de um arquivo isolado.
struct SemanticContext
{
    std::unordered_map<std::string, Scope> Scopes;
    std::unordered_map<std::string, Type> Variables;
    std::unordered_map<std::string, FunctionSignature> Functions;
    std::unordered_map<std::string, std::vector<FunctionCallSignature>> FunctionCalls;
    std::unordered_map<std::string, ImportInfo> Imports;
    // Símbolo exportado -> descrição do dono, para detetar colisões.
    std::unordered_map<std::string, std::string> ExportedSymbols;
};

}
