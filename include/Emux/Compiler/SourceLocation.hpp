#pragma once
#include <string>
#include <cstdint>

namespace Emux
{
	

struct SourceLocation
{
    std::string File;

    uint32_t Line = 1;

    uint32_t Column = 1;

    uint32_t Length = 0;

    size_t Offset = 0;
};


}
