#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Emux
{

// Formato próprio de objeto: cabeçalho JSON + bytes COFF de um módulo.
struct ObjectInfo
{
    std::string Module;
    std::string Arch;
    std::string Key;
    std::string Ir;
    std::vector<std::uint8_t> Code;
};

bool WriteObject(
    const std::filesystem::path& path,
    const ObjectInfo& info,
    std::string& error
);

std::optional<ObjectInfo> ReadObject(
    const std::filesystem::path& path
);

}
