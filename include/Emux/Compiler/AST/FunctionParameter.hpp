#pragma once
#include <Emux/Compiler/AttrSet.hpp>
#include <Emux/Compiler/Token.hpp>
#include <Emux/Compiler/Type/Type.hpp>

namespace Emux
{
	
// Parâmetro de função
struct FunctionParameter
{
    Token Type;        // O tipo (int, float, etc)
    Token Name;        // O nome do parâmetro
    Token Value;       // O nome do parâmetro
    AttrSet Attrs;     // Atributos do parâmetro ([[restrict]], [[unrestrict]])
    Emux::Type EvaluatedType; // Tipo resolvido pela análise semântica

    FunctionParameter(const Token& type, const Token& name)
        : Type(type), Name(name)
    {
    }

    FunctionParameter(const Token& type, const Token& name, const AttrSet& attrs)
        : Type(type), Name(name), Attrs(attrs)
    {
    }
    
    FunctionParameter(const Token& type, const Token& name, const Token& value)
        : Type(type), Name(name), Value(value)
    {
    }
};

}
