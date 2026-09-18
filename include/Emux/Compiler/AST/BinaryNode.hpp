#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{

enum class BinaryOperation
{
    Add,
    Sub,
    Mul,
    Div,
    And,
    Or,
    Xor,
    Lshift,
    Rshift
};

// Retorna a precedência (valores maiores indicam maior prioridade)
inline int GetOperationPrecedence(BinaryOperation op)
{
    switch (op)
    {
        case BinaryOperation::Or:
            return 1;
        case BinaryOperation::Xor:
            return 2;
        case BinaryOperation::And:
            return 3;
        case BinaryOperation::Lshift:
        case BinaryOperation::Rshift:
            return 4;
        case BinaryOperation::Add:
        case BinaryOperation::Sub:
            return 5;
        case BinaryOperation::Mul:
        case BinaryOperation::Div:
            return 6;
        default:
            return 0;
    }
}

class BinaryNode final : public Node
{
public:
    BinaryNode(
        BinaryOperation operation,
        std::unique_ptr<Node> left,
        std::unique_ptr<Node> right,
        const SourceLocation& location
    ):
        Node(NodeType::Binary, {}, location),
        Operation(operation)
    {
        if (left) Children.push_back(std::move(left));
        if (right) Children.push_back(std::move(right));
    }

    BinaryOperation Operation = BinaryOperation::Add;
};

}