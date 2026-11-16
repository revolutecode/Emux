#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/AST/NodePtr.hpp>
#include <Emux/Compiler/SourceLocation.hpp>
#include <vector>

namespace Emux
{

// Corpo '{ ... }'. Um bloco e um no normal: os statements ficam em
// Statements, e os walkers que precisam de ver para dentro dos corpos tratam
// os nos de controlo explicitamente.
class BlockNode final : public Node
{
public:
    explicit BlockNode(const SourceLocation& location)
        : Node(NodeType::Block, location)
    {
    }

    std::vector<NodePtr> Statements;
};

class IfNode final : public Node
{
public:
    explicit IfNode(const SourceLocation& location)
        : Node(NodeType::If, location)
    {
    }

    NodePtr Condition;
    NodePtr Then; // BlockNode
    NodePtr Else; // BlockNode, ou nulo quando nao ha 'else'
};

class WhileNode final : public Node
{
public:
    explicit WhileNode(const SourceLocation& location)
        : Node(NodeType::While, location)
    {
    }

    NodePtr Condition;
    NodePtr Body; // BlockNode
};

class ForNode final : public Node
{
public:
    explicit ForNode(const SourceLocation& location)
        : Node(NodeType::For, location)
    {
    }

    NodePtr Init;      // pode ser null (secção vazia)
    NodePtr Condition; // pode ser null -> loop infinito
    NodePtr Step;      // pode ser null
    NodePtr Body;      // BlockNode
};

// 'break' e 'continue' nao carregam filhos: o alvo vive na pilha de loops do
// builder, nao no no. Sao statements puros — nunca produzem valor.
class BreakNode final : public Node
{
public:
    explicit BreakNode(const SourceLocation& location)
        : Node(NodeType::Break, location)
    {
    }
};

class ContinueNode final : public Node
{
public:
    explicit ContinueNode(const SourceLocation& location)
        : Node(NodeType::Continue, location)
    {
    }
};

}
