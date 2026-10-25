#pragma once

#include "Emux/Generator/NasmContext.hpp"
#include <string_view>

namespace Emux
{

namespace NasmInstructions
{

bool Dispatch(std::string_view line, NasmContext& ctx);

void HandleRegs(std::string_view line, NasmContext& ctx);
void HandleVars(std::string_view line, NasmContext& ctx);
void HandleArgs(std::string_view line, NasmContext& ctx);
void HandleBytes(std::string_view line, NasmContext& ctx);
void HandleDoubleMath(std::string_view line, NasmContext& ctx);
void HandleMul(std::string_view line, NasmContext& ctx);
void HandleDiv(std::string_view line, NasmContext& ctx);
void HandleNeg(std::string_view line, NasmContext& ctx);
void HandleNot(std::string_view line, NasmContext& ctx);
void HandleImport(std::string_view line, NasmContext& ctx);
void HandleCall(std::string_view line, NasmContext& ctx);
void HandleFunc(std::string_view line, NasmContext& ctx);
void HandleRet(std::string_view line, NasmContext& ctx);
void HandleExit(std::string_view line, NasmContext& ctx);

} // namespace NasmInstructions

} // namespace Emux
