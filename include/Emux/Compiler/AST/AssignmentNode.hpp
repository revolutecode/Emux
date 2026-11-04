#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/AST/NodePtr.hpp>
#include <Emux/Compiler/AST/BinaryNode.hpp>
#include <Emux/Compiler/Token.hpp>
#include <cstdint>
#include <optional>

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

    // Atribuição por índice: base[i] = valor.
    // Target guarda o IndexNode (base + índice); Children[0] guarda o valor.
    bool IsIndexed = false;

    // Atribuição composta (x += y, p -= n, ...): Children[0] guarda apenas o
    // lado direito; o valor atual do alvo é lido e combinado no builder.
    // ++ e -- reutilizam isto com Add/Sub e o literal 1.
    std::optional<BinaryOperation> CompoundOp;

    NodePtr Target;
};

}