#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/SourceLocation.hpp>
#include <memory>

namespace Emux
{

enum class UnaryOperation
{
    Neg, // -
    Not  // ~ ou !
};

class UnaryNode final : public Node
{
public:
    UnaryNode(
        UnaryOperation op,
        std::unique_ptr<Node> operand,
        const SourceLocation& location
    )
        : Node(NodeType::Unary, location), Operation(op)
    {
        Children.push_back(std::move(operand));
    }

    UnaryOperation Operation;
};

}
