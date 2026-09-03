#pragma once

#include <string_view>

namespace Emux
{

class IGenerator
{
public:
    virtual int Generate(std::string_view code,std::string_view name)=0;
};

}
