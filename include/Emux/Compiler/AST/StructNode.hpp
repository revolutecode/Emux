#pragma once

#include <vector>

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/AttrSet.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{

// Campo escrito na fonte: 'nome : tipo'. O tipo resolvido e o offset calculado
// vivem no StructInfo do SemanticContext, nao aqui. Attrs guarda o [[align(N)]]
// por campo.
struct StructFieldDecl
{
    Token Name;
    Token Type;
    AttrSet Attrs;
};

// [[packed]] struct Nome { x:u24, y:u8 }. Name e a chave canonica
// 'escopo::nome'; os atributos da struct (packed/align/unrestrict) ficam em
// Attrs. A semantica calcula o layout e regista o tipo nominal.
class StructNode final : public Node
{
public:
    StructNode(
        const SourceLocation& location
    ):
        Node(NodeType::Struct, location)
    {
    }

    std::vector<StructFieldDecl> Fields;
};

}