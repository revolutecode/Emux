#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>
#include <memory>

namespace Emux
{

enum class CastKind
{
    Static,     // static_cast<T>(expr)
    Reinterpret // reinterpret_cast<T>(expr)
};

class CastNode final : public Node
{
public:
    CastNode(
        CastKind kind,
        const Token& type,
        std::unique_ptr<Node> operand,
        const SourceLocation& location
    )
        : Node(NodeType::Cast, location), Kind(kind), Type(type)
    {
        Children.push_back(std::move(operand));
    }

    CastKind Kind;
    Token Type;
};

}
