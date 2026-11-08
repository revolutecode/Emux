#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/AST/NodePtr.hpp>
#include <Emux/Compiler/SourceLocation.hpp>
#include <vector>

namespace Emux
{

// Nos de controlo de fluxo. Guardam a condicao e os blocos em membros proprios
// (nao em Children) para que os walkers genericos nao confundam a ordem. Os
// percursos que precisam de ver para dentro dos blocos (ex: recolha de chamadas
// para poda de imports) tratam estes membros explicitamente.

class IfNode final : public Node
{
public:
    explicit IfNode(const SourceLocation& location)
        : Node(NodeType::If, location)
    {
    }

    NodePtr Condition;
    std::vector<NodePtr> ThenBody;
    std::vector<NodePtr> ElseBody;
    bool HasElse = false;
};

class WhileNode final : public Node
{
public:
    explicit WhileNode(const SourceLocation& location)
        : Node(NodeType::While, location)
    {
    }

    NodePtr Condition;
    std::vector<NodePtr> Body;
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
    std::vector<NodePtr> Body;
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
