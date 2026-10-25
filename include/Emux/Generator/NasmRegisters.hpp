#pragma once

#include "Emux/CLI/Arguments.hpp"
#include "Emux/Generator/NasmContext.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace Emux
{

namespace NasmRegisters
{

bool LooksLikeReg(std::string_view s);

std::string SizedRegName(const std::vector<std::string>& forms, int bits, std::string_view baseName, Architecture arch);

std::string ResolveOperand(std::string_view input, NasmContext& ctx);

const std::vector<std::vector<std::string>>& GetPhysicalRegForms();

} // namespace NasmRegisters

} // namespace Emux
