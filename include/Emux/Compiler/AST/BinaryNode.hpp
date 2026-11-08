#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{

enum class BinaryOperation
{
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    And,
    Or,
    Xor,
    Lshift,
    Rshift,
    // Logicos de curto-circuito: devolvem 0/1 em u8 e NAO avaliam o lado
    // direito quando o esquerdo ja decide o resultado.
    LogicalAnd,
    LogicalOr,
    // Comparações: devolvem 0/1 (tipo u8, como o LogicalNot).
    Eq,
    Ne,
    Lt,
    Gt,
    Le,
    Ge
};

// Devolve verdadeiro para os seis operadores relacionais/de igualdade.
inline bool IsComparisonOperation(BinaryOperation op)
{
    switch (op)
    {
        case BinaryOperation::Eq:
        case BinaryOperation::Ne:
        case BinaryOperation::Lt:
        case BinaryOperation::Gt:
        case BinaryOperation::Le:
        case BinaryOperation::Ge:
            return true;
        default:
            return false;
    }
}

// Retorna a precedência (valores maiores indicam maior prioridade)
// Os dois lógicos de curto-circuito abrem a tabela: '||' liga mais fraco que
// '&&', e ambos ligam mais fraco que qualquer operador bit-a-bit ou de
// comparação. Os níveis abaixo de '&&' são os antigos, deslocados dois
// lugares, para que a ordem relativa entre eles não mude.
inline int GetOperationPrecedence(BinaryOperation op)
{
    switch (op)
    {
        case BinaryOperation::LogicalOr:
            return 1;
        case BinaryOperation::LogicalAnd:
            return 2;
        case BinaryOperation::Or:
            return 3;
        case BinaryOperation::Xor:
            return 4;
        case BinaryOperation::And:
            return 5;
        case BinaryOperation::Eq:
        case BinaryOperation::Ne:
            return 6;
        case BinaryOperation::Lt:
        case BinaryOperation::Gt:
        case BinaryOperation::Le:
        case BinaryOperation::Ge:
            return 7;
        case BinaryOperation::Lshift:
        case BinaryOperation::Rshift:
            return 8;
        case BinaryOperation::Add:
        case BinaryOperation::Sub:
            return 9;
        case BinaryOperation::Mul:
        case BinaryOperation::Div:
        case BinaryOperation::Mod:
            return 10;
        default:
            return 0;
    }
}

// Os lógicos não avaliam o lado direito quando o esquerdo já decide o
// resultado; precisam de fluxo de controlo, não de uma instrução binária.
inline bool IsShortCircuitOperation(BinaryOperation op)
{
    return op == BinaryOperation::LogicalAnd || op == BinaryOperation::LogicalOr;
}

class BinaryNode final : public Node
{
public:
    BinaryNode(
        BinaryOperation operation,
        std::unique_ptr<Node> left,
        std::unique_ptr<Node> right,
        const SourceLocation& location
    ):
        Node(NodeType::Binary, {}, location),
        Operation(operation)
    {
        if (left) Children.push_back(std::move(left));
        if (right) Children.push_back(std::move(right));
    }

    BinaryOperation Operation = BinaryOperation::Add;
};

}