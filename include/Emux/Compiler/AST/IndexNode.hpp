#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/SourceLocation.hpp>
#include <memory>

namespace Emux
{

// base[índice]: acesso por índice.
// Num bloco b o índice é um offset de byte (resultado u8); num ponteiro T*
// o índice escala por sizeof(T) e o resultado é T. Children[0] é a base,
// Children[1] o índice.
class IndexNode final : public Node
{
public:
    IndexNode(
        std::unique_ptr<Node> base,
        std::unique_ptr<Node> index,
        const SourceLocation& location
    )
        : Node(NodeType::Index, location)
    {
        Children.push_back(std::move(base));
        Children.push_back(std::move(index));
    }
};

}
