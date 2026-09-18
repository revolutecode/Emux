#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>
#include <cstdint>

namespace Emux
{

class VariableNode final : public Node
{

public:

    VariableNode(
        const SourceLocation& location
    )
        :
        Node(
            NodeType::Variable,
            location
        )
    {
    }


    Token Type;
};

}
