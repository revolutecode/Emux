#pragma once

#include "Emux/Generator/IGenerator.hpp"
#include <Emux/Core/Arch.hpp>
#include <filesystem>

namespace Emux
{

class NasmGenerator: public IGenerator
{
public:
    explicit NasmGenerator(Architecture arch = Architecture::X86_64);
    virtual ~NasmGenerator() = default;

    void SetArchitecture(Architecture arch) { m_Arch = arch; }
    Architecture GetArchitecture() const { return m_Arch; }

    virtual int Generate(std::string_view code, const std::filesystem::path& output) override;

private:
    Architecture m_Arch = Architecture::X86_64;
};

}
