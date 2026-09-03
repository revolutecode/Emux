#pragma once

#include <Emux/Compiler/AST/NodeType.hpp>
#include <Emux/Compiler/AST/NodePtr.hpp>
#include <Emux/Compiler/SourceLocation.hpp>
#include <Emux/Compiler/Token.hpp>
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
        m_Type(type),
        Name(name),
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
private:

    NodeType m_Type;

    SourceLocation m_Location;
};

}
