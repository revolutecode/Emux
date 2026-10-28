#pragma once

#include <string_view>
#include <filesystem>

namespace Emux
{

class IGenerator
{
public:
    virtual int Generate(std::string_view code, const std::filesystem::path& output)=0;
};

}
