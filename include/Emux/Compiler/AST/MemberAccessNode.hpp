#pragma once

#include <cstdint>
#include <memory>

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{

// base.campo: acesso a um campo de struct. Children[0] e a base; FieldName e o
// nome escrito. A analise semantica preenche FieldOffsetBytes e EvaluatedType
// (o tipo do campo), para o lowering nao precisar do registo de layouts.
class MemberAccessNode final : public Node
{
public:
    MemberAccessNode(
        std::unique_ptr<Node> base,
        const Token& fieldName,
        const SourceLocation& location
    ):
        Node(NodeType::MemberAccess, location)
    {
        Children.push_back(std::move(base));
        FieldName = fieldName;
    }

    Token FieldName;
    std::uint32_t FieldOffsetBytes = 0;
};

}