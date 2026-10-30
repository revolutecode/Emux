#pragma once

#include "Emux/Builder/IRContext.hpp"
#include "Emux/Compiler/AST/Node.hpp"
#include "Emux/Compiler/AST/BinaryNode.hpp"
#include "Emux/Compiler/AST/UnaryNode.hpp"
#include "Emux/Compiler/AST/FunctionCallNode.hpp"
#include "Emux/Compiler/Type/Type.hpp"
#include <string>

namespace Emux
{

namespace IRExpressionBuilder
{

// Resultado de uma expressão: texto usado nas instruções IR, tipo estático e
// se o valor vive numa variável temporária de 64 bits (o backend então usa as
// instruções *64 / dload / dstore em vez dos registradores virtuais de 32 bits).
struct ExprResult
{
    std::string Text;
    Type ValueType;
    bool Wide = false;
};

std::string HelperBinaryOperand(const std::string& str, IRContext& ctx);

ExprResult BuildBinary(const BinaryNode& node, IRContext& ctx);

ExprResult BuildUnary(const UnaryNode& node, IRContext& ctx);

ExprResult BuildFunctionCall(const FunctionCallNode& functionCall, IRContext& ctx);

ExprResult GetValue(Node& node, IRContext& ctx);

} // namespace IRExpressionBuilder

} // namespace Emux
