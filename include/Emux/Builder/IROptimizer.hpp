#pragma once

#include "Emux/Compiler/AST/Node.hpp"
#include "Emux/Compiler/AST/BinaryNode.hpp"
#include "Emux/Compiler/AST/UnaryNode.hpp"
#include "Emux/Compiler/AttrSet.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace Emux
{

class IRContext;

namespace IROptimizer
{

bool IsConst(const Node& node, const IRContext& ctx);

bool IsVolatile(const Node& node);

std::optional<int64_t> TryEvaluateConst(const Node& node, const IRContext& ctx);

std::optional<int64_t> TryFoldBinary(const BinaryNode& node, const IRContext& ctx);

std::optional<int64_t> TryFoldUnary(const UnaryNode& node, const IRContext& ctx);

} // namespace IROptimizer

} // namespace Emux
