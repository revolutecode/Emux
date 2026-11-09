#pragma once

#include "Emux/Builder/IRContext.hpp"
#include "Emux/Compiler/AST/BinaryNode.hpp"
#include "Emux/Compiler/AST/Node.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Exir/Exir.hpp"

namespace Emux
{

namespace IRExpressionBuilder
{

// Resultado da avaliacao de uma expressao: um Exir::Value nativo em memoria.
struct ExprResult
{
    Exir::Value Val;
    bool IsLValue = false;
};

// Converte um valor para o tipo pedido, emitindo instrucoes de cast explicitas (zext, sext, trunc, etc.).
ExprResult Coerce(const ExprResult& value, const Type& target, IRContext& ctx);

// Emite uma instrucao binaria tipada no bloco corrente e devolve o temporario gerado.
ExprResult EmitBinaryInst(
    Exir::OpCode op,
    const ExprResult& left,
    const ExprResult& right,
    const Type& resultTy,
    IRContext& ctx
);

// Mapeia BinaryOperation do AST para OpCode canonico da Exir.
Exir::OpCode MapBinaryOp(BinaryOperation operation, const Type& type);

// Escala um deslocamento inteiro por sizeof(pointee) atraves de instrucao gep ou mul.
ExprResult ScalePointerOffset(
    const ExprResult& offset,
    const Type& pointerType,
    IRContext& ctx
);

// Avalia um no da AST e devolve o Exir::Value correspondente.
ExprResult GetValue(Node& node, IRContext& ctx);

// Obtem o endereco de base de uma variavel (Addr sem Load).
// Para variaveis locais/globais devolve o ponteiro bruto; para buffer
// types isto e o endereco do buffer directamente (sem dereferencia).
ExprResult GetAddress(Node& node, IRContext& ctx);

} // namespace IRExpressionBuilder

} // namespace Emux
