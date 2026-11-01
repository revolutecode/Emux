#pragma once

#include <filesystem>
#include <string>

namespace Emux
{

// Runtime em C que implementa float e ponto fixo de largura arbitraria. O
// codigo e embebido no emux e compilado com o gcc do utilizador no momento do
// link; o objeto resultante e reutilizado enquanto a fonte nao mudar.
std::string NumericRuntimeKey();

bool PrepareNumericRuntime(
    const std::filesystem::path& directory,
    std::filesystem::path& objectPath,
    const std::filesystem::path& logPath
);

} // namespace Emux
