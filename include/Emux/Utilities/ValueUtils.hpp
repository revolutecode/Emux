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

};
