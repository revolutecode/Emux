#pragma once

#include "Emux/Compiler/Scope.hpp"
#include "Semantic/FunctionCallSignature.hpp"
#include "Semantic/FunctionSignature.hpp"
#include <Emux/CLI/Arguments.hpp>
#include <Emux/Compiler/Diagnostics.hpp>
#include <Emux/Compiler/SourceFile.hpp>
#include <Emux/Compiler/Token.hpp>
#include <Emux/Compiler/AST/Program.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Emux
{

class CompilerContext
{

public:

    Arguments Arguments;

    Diagnostics Diagnostics;

    SourceFile Source;
    
    std::unordered_map<std::string, SourceFile> Sources;
    std::unordered_map<std::string, Scope> Scopes;
    std::unordered_map<std::string, Type> Variables;
    std::unordered_map<std::string, FunctionSignature> Functions;
    std::unordered_map<std::string, std::vector<FunctionCallSignature>> FunctionCalls;

    std::vector<Token> Tokens;
	std::unique_ptr<Program> AST;
};

}
