#pragma once

#include <Emux/Compiler/AST/DeclarationNode.hpp>
#include <Emux/Compiler/Token.hpp>

#include <cstdint>

namespace Emux
{

// Declaracao de corpo de funcao ('local'): a variavel vive num slot da frame,
// nao em memoria estatica. A frame existe automaticamente quando uma funcao
// tem locais; [[frame]] garante que o slot e estavel.
class LocalNode final : public DeclarationNode
{

public:

    LocalNode(
        const SourceLocation& location
    )
        :
        DeclarationNode(
            NodeType::Local,
            location
        )
    {
    }
};

}
