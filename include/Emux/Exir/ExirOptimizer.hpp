#pragma once

#include <string_view>

namespace Emux::Exir
{

// Nivel de otimizacao da Exir. O0 preserva o texto tal como sai do IRBuilder.
enum class OptimizationLevel
{
    O0 = 0,
    O1 = 1,
    O2 = 2
};

// Reduz o texto da Exir de um modulo. Corre a nivel de texto porque o texto
// otimizado e o que vai para o .emo e para o artefacto .exir, mantendo cache,
// debug e asm consistentes entre si. Em O0 devolve uma copia literal.
//
// O0: nenhuma transformacao.
// O1: eliminacao de codigo morto (instrucoes puras cujo destino nunca e lido).
// O2: O1 mais simplificações locais seguras.
std::string Optimize(std::string_view irText, OptimizationLevel level);

} // namespace Emux::Exir
