#pragma once

#include "Emux/Compiler/Scope.hpp"
#include "Emux/Core/Config.hpp"
#include <Emux/CLI/Arguments.hpp>
#include <Emux/Compiler/Diagnostics.hpp>
#include <Emux/Compiler/SourceFile.hpp>
#include <Emux/Compiler/Token.hpp>
#include <Emux/Compiler/AST/Program.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Emux
{

class CompilerContext
{

public:

    Arguments Arguments;

    Diagnostics Diagnostics;

    Config::Project Project;

    // Um contexto por módulo: fontes, tokens e AST são locais ao arquivo.
    std::unordered_map<std::string, SourceFile> Sources;
    std::unordered_map<std::string, Scope> Scopes;

    // Arquivos realmente embutidos via @include (dependências do objeto).
    std::unordered_set<std::string> IncludedFiles;

    // Arquivos que são módulos do projeto: @include deles é ignorado,
    // pois suas declarações chegam pelo objeto linkado.
    std::unordered_set<std::string> ModulePaths;

    std::vector<Token> Tokens;
	std::unique_ptr<Program> AST;
};

}
