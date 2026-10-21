#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{

class LiteralNode final : public Node
{
public:
    LiteralNode(
        const Token& value,
        const SourceLocation& location
    ):
        Node(
            NodeType::Literal,
            {},
            location
        ),
        Value(value)
    {
    }

    LiteralNode(
        const Token& type,
        const Token& value,
        const SourceLocation& location
    ):
        Node(
            NodeType::Literal,
            {},
            location
        ),
        Type(type),
        Value(value)
    {
    }

    LiteralNode(
        const Token& name,
        const Token& type,
        const Token& value,
        const SourceLocation& location
    ):
        Node(
            NodeType::Literal,
            name,
            location
        ),
        Value(value)
    {
    }

    Token Value;
    Token Type;
};

}