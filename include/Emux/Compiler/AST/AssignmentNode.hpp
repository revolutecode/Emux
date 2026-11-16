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

    // Expressao do alvo (lvalue): VariableCallNode, IndexNode,
    // MemberAccessNode ou UnaryNode(Deref). Esta sempre definida. As flags
    // IsDereference/IsIndexed/IsMember foram removidas: a forma do alvo diz
    // tudo o que a analise semantica e o builder precisam.
    //
    // Atribuicao composta (x += y, p -= n, ...): Children[0] guarda apenas o
    // lado direito; o valor atual do alvo e lido e combinado no builder.
    // ++ e -- reutilizam isto com Add/Sub e o literal 1.
    std::optional<BinaryOperation> CompoundOp;

    NodePtr Target;
};

}