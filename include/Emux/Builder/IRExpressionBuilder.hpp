#pragma once

#include "Emux/Builder/IRContext.hpp"
#include "Emux/Compiler/AST/BinaryNode.hpp"
#include "Emux/Compiler/AST/Node.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include "Emux/Exir/Exir.hpp"

#include <cstdint>

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

// Uma struct por valor e um tipo nominal cuja representacao e um bloco. Na ABI
// interna os seus bytes crus viajam num registo inteiro da palavra nativa
// (carrier); o acesso a campo continua sobre a regiao em memoria. So structs
// com tamanho <= palavra nativa sao passadas/retornadas por valor.
inline bool IsStructValue(const Type& type)
{
    return type.IsNominal() && type.IsBuffer();
}

inline Type StructCarrier(std::uint32_t pointerBits)
{
    return Type::MakeUnsigned(pointerBits);
}

// Converte um valor para o tipo pedido, emitindo instrucoes de cast explicitas (zext, sext, trunc, etc.).
ExprResult Coerce(const ExprResult& value, const Type& target, IRContext& ctx);

// Constante canonica de um tipo numerico: imediato ate 64 bits, regiao de
// dados na .rdata acima disso (o imediato de um Value nao guarda mais).
ExprResult MakeNumericConstant(const BigInt& value, const Type& type, IRContext& ctx);

// Idem para um literal decimal, onde o valor e a fracao num/den e a
// arredondamento para o padrao de bits de float/fixed acontece aqui.
ExprResult MakeNumericConstant(
    const BigInt& numerator,
    const BigInt& denominator,
    const Type& type,
    IRContext& ctx
);

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

// Emite 'gep': endereco base + index * elementSize. O indice conta elementos
// (nao bytes); para deslocamentos em bytes use elementSize = 1. O resultado e
// um ponteiro e 'subtract' recua o indice em complemento para dois.
ExprResult EmitGep(
    const ExprResult& base,
    const ExprResult& index,
    std::uint32_t elementSize,
    const Type& resultType,
    IRContext& ctx,
    bool subtract = false
);

// Bytes de um elemento do tipo apontado; 1 para ponteiros opacos/blocos.
std::uint32_t PointerElementSize(const Type& pointerType);

// Avalia um no da AST e devolve o Exir::Value correspondente.
ExprResult GetValue(Node& node, IRContext& ctx);

// Obtem o endereco de base de uma variavel (Addr sem Load).
// Para variaveis locais/globais devolve o ponteiro bruto; para buffer
// types isto e o endereco do buffer directamente (sem dereferencia).
ExprResult GetAddress(Node& node, IRContext& ctx);

// Le os bytes crus de uma regiao struct (endereco u8*) como um inteiro sem
// sinal da largura exata da struct (<= palavra nativa), para passar/retornar a
// struct por valor na ABI interna. O gerador zero-estende o load ao registo.
ExprResult LoadStructCarrier(const Exir::Value& addr, const Type& structType, IRContext& ctx);

} // namespace IRExpressionBuilder

} // namespace Emux
