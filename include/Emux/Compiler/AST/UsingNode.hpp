#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{

// using Nome = Tipo
// Cria um tipo nominal restrito, salvo [[unrestrict]].
class UsingNode final : public Node
{
public:
    UsingNode(
        const Token& target,
        const SourceLocation& location
    ):
        Node(
            NodeType::Using,
            location
        ),
        Type(target)
    {
    }

    Token Type;
};

}
