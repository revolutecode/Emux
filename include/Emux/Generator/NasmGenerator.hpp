#pragma once

#include "Emux/Generator/IGenerator.hpp"
#include "Emux/CLI/Arguments.hpp"

namespace Emux
{

class NasmGenerator: public IGenerator
{
public:
    explicit NasmGenerator(Architecture arch = Architecture::X86_64);
    virtual ~NasmGenerator() = default;

    void SetArchitecture(Architecture arch) { m_Arch = arch; }
    Architecture GetArchitecture() const { return m_Arch; }

    virtual int Generate(std::string_view code, std::string_view outputName = "output") override;

private:
    Architecture m_Arch = Architecture::X86_64;
};

}
