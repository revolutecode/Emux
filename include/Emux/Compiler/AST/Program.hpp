#pragma once
#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/AST/NodePtr.hpp>

namespace Emux
{

class Program final : public Node
{
public:

    Program()
        :
        Node(
            NodeType::Program,
            {}
        )
    {
    }
};

}
