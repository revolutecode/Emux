#pragma once

#include <string>
#include <string_view>

namespace Emux::NasmPeephole
{

// Otimizacao local do NASM gerado, aplicada em -O2. Mantem os temporarios de
// vida curta no registo (forwarding store->load), elimina 'mov reg,reg' e
// 'lea' redundantes. Conservador: repoe os factos em labels, calls, saltos,
// ret e instrucoes desconhecidas; nunca altera fluxo de controlo, dados nem
// larguras. Agnostico da arquitetura (x64 e x86). -O0/-O1 nao passam por aqui.
std::string Optimize(std::string_view asmText);

} // namespace Emux::NasmPeephole