#pragma once

#include <optional>
#include <string>
#include <vector>

#include <Emux/Compiler/AST/Node.hpp>
#include <Emux/Compiler/AST/NodePtr.hpp>
#include <Emux/Compiler/SourceLocation.hpp>
#include <Emux/Compiler/Token.hpp>

namespace Emux
{

// Value e nulo quando o elemento nao tem '= valor' e depende do
// auto-incremento definido por [[step]]. Key, EvaluatedType e FoldedValue sao
// preenchidos pela analise semantica; FoldedValue fica nulo em string/buffer,
// cujo valor vive em .rdata.
struct EnumElement
{
    Token Name;
    NodePtr Value;
    std::string Key;
    Type EvaluatedType;
    std::optional<BigInt> FoldedValue;
};

// enum Nome : Tipo { A, B = 2 }. O nome e opcional: um enum anonimo nao cria
// um tipo e injeta os elementos diretamente no escopo atual.
class EnumNode final : public Node
{
public:
    EnumNode(
        const SourceLocation& location
    ):
        Node(NodeType::Enum, location)
    {
    }

    Token ElementType;
    std::vector<EnumElement> Elements;
    bool IsAnonymous = false;
};

}
