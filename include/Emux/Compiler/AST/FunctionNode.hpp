#pragma once

#include <vector>
#include <Emux/Compiler/AST/NodePtr.hpp>
#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/AST/FunctionParameter.hpp>
#include <Emux/Compiler/Scope.hpp>

#include <Emux/Compiler/Token.hpp>

namespace Emux
{

// O nó de função
class FunctionNode : public Node
{
public:
    
    FunctionNode(
        const Token& returnType,
        const Token& name,
        const SourceLocation& location
    );

    FunctionNode(
        const Token& name,
        const SourceLocation& location
    );

    // Metadata
    Token ReturnType;  // Tipo de retorno
    std::vector<FunctionParameter> Parameters;  // Parâmetros
};

} // namespace Emux
