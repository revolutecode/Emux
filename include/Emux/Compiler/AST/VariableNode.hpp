#pragma once

#include <Emux/Compiler/AST/DeclarationNode.hpp>
#include <Emux/Compiler/Token.hpp>
#include <Emux/Compiler/Scope.hpp>

#include <cstdint>

namespace Emux
{

// Declaracao de nivel de seccao ('var'): a variavel e uma regiao estatica, em
// .rdata se [[const]] inicializada, .data se inicializada mutavel, .bss caso
// contrario.
class VariableNode final : public DeclarationNode
{

public:

    VariableNode(
        const SourceLocation& location
    )
        :
        DeclarationNode(
            NodeType::Variable,
            location
        )
    {
    }
};

}
