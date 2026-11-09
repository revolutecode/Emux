#pragma once

#include "Emux/Exir/Exir.hpp"

#include <filesystem>
#include <string_view>

namespace Emux
{

class IGenerator
{
public:
    virtual ~IGenerator() = default;

    // Gera codigo de maquina direto do modulo Exir em memoria
    virtual int GenerateModule(const Exir::Module& module, const std::filesystem::path& output) = 0;

    // Compatibilidade transitoria
    virtual int Generate(std::string_view code, const std::filesystem::path& output) = 0;
};

} // namespace Emux
