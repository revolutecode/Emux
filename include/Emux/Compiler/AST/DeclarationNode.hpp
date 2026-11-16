#pragma once

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{

// Onde a declaracao vive. O resto da gramatica '<nome> : <tipo> [= valor]' e
// igual nos dois casos, por isso existe um unico no.
enum class StorageClass
{
    Global, // regiao estatica: .rdata, .data ou .bss
    Local   // slot da frame da funcao
};

class DeclarationNode final : public Node
{
public:

    DeclarationNode(
        StorageClass storage,
        const SourceLocation& location
    )
        :
        Node(
            NodeType::Declaration,
            location
        ),
        Storage(storage)
    {
    }

    // Token do tipo escrito (ParseTypeToken); o tipo resolvido fica em
    // EvaluatedType depois da analise semantica.
    Token Type;

    StorageClass Storage;
};

}
