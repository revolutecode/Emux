#pragma once

#include "Emux/Compiler/AttrSet.hpp"
#include <Emux/Compiler/AST/NodeType.hpp>
#include <Emux/Compiler/AST/NodePtr.hpp>
#include <Emux/Compiler/SourceLocation.hpp>
#include <Emux/Compiler/Token.hpp>
#include <Emux/Compiler/Type/Type.hpp>
#include <Emux/Utilities/BigInt.hpp>
#include <optional>
#include <vector>

namespace Emux
{

class Node
{
public:

    Node(
        const NodeType& type,
        const SourceLocation& location
    ):
        m_Type(type),
        m_Location(location)
    {
    }

    Node(
        const NodeType& type,
        const Token& name,
        const SourceLocation& location
    ):
        Name(name),
        m_Type(type),
        m_Location(location)
    {
    }

    Node(Node&&)=delete;
    Node& operator=(Node&&)=delete;

    virtual ~Node() = default;

    Token GetName() const
    {
        return Name;
    }

    NodeType GetType() const
    {
        return m_Type;
    }

    const SourceLocation& GetLocation() const
    {
        return m_Location;
    }

public:
    std::vector<NodePtr> Children;

    Token Name;

    AttrSet Attrs;

    Type EvaluatedType;

    // Valor exato de uma expressão constante, calculado em precisão
    // arbitrária pela análise semântica. Só chega ao IR quando cabe em 64
    // bits (ou na representação de 64 bits do tipo).
    std::optional<BigInt> FoldedValue;
private:

    NodeType m_Type;

    SourceLocation m_Location;
};

}
