#pragma once

#include "Emux/Generator/IGenerator.hpp"
namespace Emux
{

class NasmGenerator: public IGenerator
{
public:
    virtual int Generate(std::string_view code, std::string_view outputName="output") override;
};

}
