#pragma once
#include <Emux/Compiler/Token.hpp>

namespace Emux
{
	
// Parâmetro de função
struct FunctionParameter
{
    Token Type;        // O tipo (int, float, etc)
    Token Name;        // O nome do parâmetro
    Token Value;        // O nome do parâmetro
    
    FunctionParameter(const Token& type, const Token& name)
        : Type(type), Name(name)
    {
    }
    
    FunctionParameter(const Token& type, const Token& name, const Token& value)
        : Type(type), Name(name), Value(value)
    {
    }
};

}