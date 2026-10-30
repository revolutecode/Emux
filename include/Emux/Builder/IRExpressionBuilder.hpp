#pragma once

#include "Emux/Builder/IRContext.hpp"
#include "Emux/Compiler/AST/Node.hpp"
#include "Emux/Compiler/Type/Type.hpp"

#include <string>

namespace Emux
{

namespace IRExpressionBuilder
{

// Resultado da avaliação de uma expressão na Exir: texto do operando,
// tipo estático e se é um literal imediato.
struct ExprResult
{
    std::string Text;
    Type Ty;
    bool Literal = false;
};

// Converte um valor para o tipo pedido, emitindo 'cast' quando necessário.
ExprResult Coerce(const ExprResult& value, const Type& target, IRContext& ctx);

// Avalia um nó e devolve o operando que representa o seu valor.
ExprResult GetValue(Node& node, IRContext& ctx);

} // namespace IRExpressionBuilder

} // namespace Emux
