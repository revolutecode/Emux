#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Emux/Compiler/Scope.hpp"
#include "Emux/Compiler/Semantic/FunctionCallSignature.hpp"
#include "Emux/Compiler/Semantic/FunctionSignature.hpp"
#include "Emux/Compiler/Semantic/ImportInfo.hpp"
#include "Emux/Compiler/SourceLocation.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Utilities/BigInt.hpp"

namespace Emux
{

// using Nome = Tipo, tal como escrito na fonte. A resolucao e adiada para
// permitir referencias entre modulos/ordens diferentes.
struct TypeAliasDefinition
{
    std::string Name;       // como escrito na fonte (para diagnosticos)
    std::string Target;
    std::string Scope;
    bool Unrestricted = false;
    SourceLocation Location;
};

// Elemento de um enum. Um enum e uma constante de compilacao: o valor escalar
// dobra para um literal e string/buffer fica em .rdata, com Value nulo.
struct EnumElementInfo
{
    Type Type;
    std::optional<BigInt> Value;
    // Chave do enum dono ('main::color') ou do escopo, num enum anonimo.
    std::string Owner;
    SourceLocation Location;
};

// Tabelas compartilhadas por todos os módulos. É o resultado da análise
// semântica do programa inteiro, não de um arquivo isolado.
struct SemanticContext
{
    std::unordered_map<std::string, Scope> Scopes;
    std::unordered_map<std::string, Type> Variables;
    // Variaveis declaradas [[const]]: imutaveis depois da inicializacao.
    std::unordered_set<std::string> ConstVariables;
    // Blocos b declarados [[restrict]]: a conversao implicita para T* exige
    // static_cast explicito; b[i], &b e *b continuam permitidos.
    std::unordered_set<std::string> RestrictedBuffers;
    // Variaveis de nivel de seccao/modulo que sao exportadas para outros modulos.
    std::unordered_set<std::string> ExportedVariables;
    std::unordered_map<std::string, FunctionSignature> Functions;
    std::unordered_map<std::string, std::vector<FunctionCallSignature>> FunctionCalls;
    std::unordered_map<std::string, ImportInfo> Imports;
    // Símbolo exportado -> descrição do dono, para detetar colisões.
    std::unordered_map<std::string, std::string> ExportedSymbols;
    // using declarados (chave canonica em minusculas) e tipos ja resolvidos.
    std::unordered_map<std::string, TypeAliasDefinition> TypeAliases;
    std::unordered_map<std::string, Type> ResolvedTypes;
    // Elementos de enums, pela chave completa ('main::color::Red').
    std::unordered_map<std::string, EnumElementInfo> EnumElements;
};

}
