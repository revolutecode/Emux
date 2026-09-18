#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>
#include <cstdint>

namespace Emux
{

class ReturnNode final : public Node
{

public:

    ReturnNode(
        const SourceLocation& location
    ):
        Node(
            NodeType::Return,
            location
        )
    {
    }
};

}
