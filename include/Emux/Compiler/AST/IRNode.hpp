#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{

class IRNode final : public Node
{
public:

    IRNode(
        const Token& code,
        const SourceLocation& location
    ):
        Node(
            NodeType::IR,
            {},
            location
        ),
        Code(code)
    {
    }

    Token Code;
};

}
