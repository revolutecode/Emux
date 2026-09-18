#pragma once
#include <cstdint>

namespace Emux::Utilities
{

inline void SetValue(uint64_t value, uint8_t* result, size_t bits)
{
	if(bits < 64)
    {
        uint64_t mask = (1ULL << bits) - 1;

        value &= mask;
    }

    size_t bytes = (bits + 7)/8;
    for(size_t i = 0; i < bytes; i++)
    {
        result[i] = static_cast<uint8_t>(
            value >> (i * 8)
        );
    }
}

inline std::size_t MaskBits(std::size_t bits)
{
    if (bits == 8) return UINT8_MAX;
    else if (bits == 16) return UINT16_MAX;
    else if (bits == 32) return UINT32_MAX;
    else if (bits == 64) return UINT64_MAX;    
    else return (1ull << bits) - 1;
}

};
