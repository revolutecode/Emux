#pragma once

#include <cstdint>
#include <string_view>

namespace Emux
{

inline constexpr std::string_view kEmuxVersion = "0.2.0";

// Muda sempre que o formato dos artefatos (.emo) ou a ABI gerada mudar:
// invalida todo o cache de módulos.
inline constexpr std::uint32_t kObjectFormatVersion = 1;

}
