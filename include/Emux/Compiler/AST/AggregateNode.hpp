#pragma once

#include <cstdint>
#include <vector>

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Type/Type.hpp>

namespace Emux
{

// Literal agregado anonimo '{ a, b, ... }'. Children sao as expressoes dos
// elementos, na ordem dos campos. A analise semantica preenche FieldOffsets e
// FieldTypes (paralelos a Children) com o layout da struct alvo, para o
// lowering materializar cada elemento no seu offset sem aceder ao registo.
class AggregateNode final : public Node
{
public:
    AggregateNode(
        const SourceLocation& location
    ):
        Node(NodeType::Aggregate, location)
    {
    }

    std::vector<std::uint32_t> FieldOffsets;
    std::vector<Type> FieldTypes;
};

}