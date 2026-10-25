#pragma once

#include "Emux/Builder/IRContext.hpp"
#include "Emux/Compiler/AST/Node.hpp"
#include "Emux/Compiler/AST/BinaryNode.hpp"
#include "Emux/Compiler/AST/UnaryNode.hpp"
#include "Emux/Compiler/AST/FunctionCallNode.hpp"
#include <string>

namespace Emux
{

namespace IRExpressionBuilder
{

std::string HelperBinaryOperand(const std::string& str, IRContext& ctx);

void BuildBinary(const BinaryNode& node, IRContext& ctx);

void BuildUnary(const UnaryNode& node, IRContext& ctx);

void BuildFunctionCall(const FunctionCallNode& functionCall, IRContext& ctx);

std::string GetValue(Node& node, IRContext& ctx);

} // namespace IRExpressionBuilder

} // namespace Emux
