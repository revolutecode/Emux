#pragma once

#include "Emux/Compiler/Scope.hpp"
#include <Emux/CLI/Arguments.hpp>
#include <Emux/Compiler/Diagnostics.hpp>
#include <Emux/Compiler/SourceFile.hpp>
#include <Emux/Compiler/Token.hpp>
#include <Emux/Compiler/AST/Program.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace Emux
{

class CompilerContext
{

public:

    Arguments Arguments;

    Diagnostics Diagnostics;

    SourceFile Source;

    std::unordered_map<std::string, Scope> Scopes;
    std::vector<Token> Tokens;
	std::unique_ptr<Program> AST;
};

}
