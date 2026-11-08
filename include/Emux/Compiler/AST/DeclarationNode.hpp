#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{

// Base das declaracoes '<nome> : <tipo> [= literal]'. O que distingue 'var' de
// 'local' e onde a variavel vive (regiao estatica vs slot da frame), nao a
// gramatica; por isso ambos partilham este no.
class DeclarationNode : public Node
{
public:

    DeclarationNode(
        const NodeType& type,
        const SourceLocation& location
    )
        :
        Node(
            type,
            location
        )
    {
    }

    // Token do tipo escrito (ParseTypeToken); o tipo resolvido fica em
    // EvaluatedType depois da analise semantica.
    Token Type;
};

}
