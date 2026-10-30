#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/AST/NodePtr.hpp>
#include <Emux/Compiler/Token.hpp>
#include <cstdint>

namespace Emux
{

class AssignmentNode final : public Node
{
public:
    AssignmentNode(
        const Token& name,
        const SourceLocation& location
    ):
        Node(
            NodeType::Assign,
            name,
            location
        )
    {
    }

    AssignmentNode(
        const SourceLocation& location
    ):
        Node(
            NodeType::Assign,
            {},
            location
        )
    {
    }

    // Atribuição por dereferência: *ptr = valor.
    // Target guarda a expressão do ponteiro; Children[0] guarda o valor.
    bool IsDereference = false;
    NodePtr Target;
};

}