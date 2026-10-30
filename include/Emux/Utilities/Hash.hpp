#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace Emux::Hash
{

inline constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
inline constexpr std::uint64_t kPrime = 1099511628211ull;

class Hasher
{
public:
    void Add(std::string_view data)
    {
        for (unsigned char byte : data)
        {
            m_State ^= byte;
            m_State *= kPrime;
        }
    }

    void AddDelimited(std::string_view data)
    {
        Add(data);
        Add("\n");
    }

    std::uint64_t Value() const
    {
        return m_State;
    }

    std::string Hex() const
    {
        static constexpr char digits[] = "0123456789abcdef";

        std::uint64_t state = m_State;
        std::string out(16, '0');
        for (int i = 15; i >= 0; --i)
        {
            out[static_cast<std::size_t>(i)] = digits[state & 0xFu];
            state >>= 4;
        }

        return out;
    }

private:
    std::uint64_t m_State = kOffsetBasis;
};

inline std::string Hex(std::string_view data)
{
    Hasher hasher;
    hasher.Add(data);
    return hasher.Hex();
}

inline std::optional<std::string> FileHex(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }

    Hasher hasher;
    char buffer[8192];

    while (file)
    {
        file.read(buffer, sizeof(buffer));
        std::streamsize read = file.gcount();
        if (read > 0)
        {
            hasher.Add(std::string_view(buffer, static_cast<std::size_t>(read)));
        }
    }

    return hasher.Hex();
}

}
