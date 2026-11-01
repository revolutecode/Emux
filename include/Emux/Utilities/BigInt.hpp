#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Emux
{

// Inteiro de precisão arbitrária (fase 1).
// Guarda o sinal separado dos limbos, em complemento de magnitude:
// |valor| = soma(limbs[i] * 2^(32*i)), com limbos little-endian.
class BigInt
{
public:
    BigInt() = default;

    explicit BigInt(std::int64_t value)
    {
        if (value < 0)
        {
            m_Negative = true;
            // -INT64_MIN não cabe em int64: trata o caso separadamente.
            std::uint64_t magnitude =
                (value == INT64_MIN)
                    ? (std::uint64_t(1) << 63)
                    : static_cast<std::uint64_t>(-value);
            SetMagnitude(magnitude);
        }
        else
        {
            SetMagnitude(static_cast<std::uint64_t>(value));
        }
    }

    static BigInt FromUInt64(std::uint64_t value)
    {
        BigInt result;
        result.SetMagnitude(value);
        return result;
    }

    static std::optional<BigInt> Parse(std::string_view text, int base = 10)
    {
        BigInt result;
        if (text.empty())
        {
            return std::nullopt;
        }

        bool negative = false;
        std::size_t index = 0;

        if (text[0] == '-' || text[0] == '+')
        {
            negative = text[0] == '-';
            index = 1;
        }

        if (base == 16 && text.size() > index + 2 &&
            text[index] == '0' && (text[index + 1] == 'x' || text[index + 1] == 'X'))
        {
            index += 2;
        }

        if (index >= text.size())
        {
            return std::nullopt;
        }

        for (; index < text.size(); ++index)
        {
            const char c = text[index];
            int digit = -1;

            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;

            if (digit < 0 || digit >= base)
            {
                return std::nullopt;
            }

            result.MulSmall(static_cast<std::uint32_t>(base));
            result.AddSmall(static_cast<std::uint32_t>(digit));
        }

        result.m_Negative = negative && !result.IsZero();
        return result;
    }

    bool IsZero() const
    {
        for (std::uint32_t limb : m_Limbs)
        {
            if (limb != 0) return false;
        }
        return true;
    }

    bool IsNegative() const { return m_Negative && !IsZero(); }

    std::uint32_t BitLength() const
    {
        if (m_Limbs.empty()) return 0;

        std::uint32_t bits = static_cast<std::uint32_t>((m_Limbs.size() - 1) * 32);
        std::uint32_t top = m_Limbs.back();

        while (top != 0)
        {
            ++bits;
            top >>= 1;
        }

        return bits;
    }

    bool FitsInt64() const
    {
        if (IsZero()) return true;
        if (m_Limbs.size() > 2) return false;

        // A magnitude cabe em 64 bits aqui. Positivos vao ate 2^63-1 e
        // negativos ate -2^63 (complemento de magnitude).
        const std::uint64_t magnitude = ToUInt64();
        const std::uint64_t limit = m_Negative
            ? (std::uint64_t(1) << 63)
            : ((std::uint64_t(1) << 63) - 1);

        return magnitude <= limit;
    }

    bool FitsUInt64() const
    {
        if (m_Limbs.size() > 2) return false;
        if (m_Negative) return false;
        return true;
    }

    std::uint64_t ToUInt64() const
    {
        std::uint64_t value = 0;
        if (m_Limbs.size() > 0) value |= m_Limbs[0];
        if (m_Limbs.size() > 1) value |= std::uint64_t(m_Limbs[1]) << 32;
        return value;
    }

    // Bytes little-endian da representação de 'count' bytes em complemento de
    // dois: o valor é tomado módulo 2^(8*count). É a forma multi-limb usada em
    // runtime e nos bytes iniciais de globais [[const]].
    std::vector<std::uint8_t> ToLittleEndianBytes(std::size_t count) const
    {
        std::vector<std::uint8_t> bytes(count, 0);

        for (std::size_t i = 0; i < m_Limbs.size() && i * 4 < count; ++i)
        {
            const std::uint32_t limb = m_Limbs[i];

            for (int b = 0; b < 4; ++b)
            {
                const std::size_t index = i * 4 + static_cast<std::size_t>(b);
                if (index >= count) break;
                bytes[index] = static_cast<std::uint8_t>((limb >> (8 * b)) & 0xFFu);
            }
        }

        if (m_Negative && !IsZero())
        {
            std::uint32_t carry = 1;

            for (std::size_t i = 0; i < count; ++i)
            {
                const std::uint32_t current =
                    static_cast<std::uint32_t>(0xFFu - bytes[i]) + carry;
                bytes[i] = static_cast<std::uint8_t>(current);
                carry = current >> 8;
            }
        }

        return bytes;
    }

    std::int64_t ToInt64() const
    {
        std::uint64_t magnitude = ToUInt64();
        if (!m_Negative) return static_cast<std::int64_t>(magnitude);
        if (magnitude == (std::uint64_t(1) << 63)) return INT64_MIN;
        return -static_cast<std::int64_t>(magnitude);
    }

    // -1, 0 ou 1, como um compare de três vias.
    int Compare(const BigInt& other) const
    {
        const bool leftNegative = IsNegative();
        const bool rightNegative = other.IsNegative();

        if (leftNegative != rightNegative)
        {
            return leftNegative ? -1 : 1;
        }

        int magnitude = CompareMagnitude(m_Limbs, other.m_Limbs);
        return leftNegative ? -magnitude : magnitude;
    }

    bool operator==(const BigInt& other) const
    {
        return Compare(other) == 0;
    }

    bool operator!=(const BigInt& other) const
    {
        return Compare(other) != 0;
    }

    // Cabe na representação de N bits, com ou sem sinal?
    bool FitsBits(std::uint32_t bits, bool isSigned) const
    {
        if (bits == 0) return IsZero();

        if (!isSigned)
        {
            return !IsNegative() && BitLength() <= bits;
        }

        const BigInt limit = BigInt(1).Shl(bits - 1);
        return Compare(-limit) >= 0 && Compare(limit - BigInt(1)) <= 0;
    }

    // Valor canónico da representação de N bits: módulo 2^N e, se for
    // signed, convertido para o intervalo [-(2^(N-1)), 2^(N-1)-1].
    BigInt WrappedTo(std::uint32_t bits, bool isSigned) const
    {
        if (bits == 0) return BigInt();

        const BigInt modulus = BigInt(1).Shl(bits);
        auto divided = DivMod(modulus);

        if (!divided.has_value())
        {
            return BigInt();
        }

        BigInt remainder = divided->second;

        // DivMod trunca em direção a zero; o módulo canónico é sempre >= 0.
        if (remainder.IsNegative())
        {
            remainder = remainder + modulus;
        }

        if (isSigned)
        {
            const BigInt half = BigInt(1).Shl(bits - 1);
            if (remainder.Compare(half) >= 0)
            {
                remainder = remainder - modulus;
            }
        }

        return remainder;
    }

    std::string ToString() const
    {
        if (IsZero()) return "0";

        std::vector<std::uint32_t> digits; // base 10, little-endian
        std::vector<std::uint32_t> work = m_Limbs;

        while (!work.empty())
        {
            std::uint64_t rem = 0;
            for (std::size_t i = work.size(); i-- > 0;)
            {
                std::uint64_t current = (rem << 32) | work[i];
                work[i] = static_cast<std::uint32_t>(current / 10);
                rem = current % 10;
            }
            digits.push_back(static_cast<std::uint32_t>(rem));

            while (!work.empty() && work.back() == 0)
            {
                work.pop_back();
            }
        }

        std::string out;
        if (m_Negative) out += '-';
        for (std::size_t i = digits.size(); i-- > 0;)
        {
            out += static_cast<char>('0' + digits[i]);
        }
        return out;
    }

    BigInt operator-() const
    {
        BigInt copy = *this;
        if (!copy.IsZero()) copy.m_Negative = !copy.m_Negative;
        return copy;
    }

    BigInt operator+(const BigInt& other) const
    {
        if (m_Negative == other.m_Negative)
        {
            BigInt result;
            result.m_Negative = m_Negative;
            result.m_Limbs = AddMagnitude(m_Limbs, other.m_Limbs);
            result.Trim();
            return result;
        }

        const int cmp = CompareMagnitude(m_Limbs, other.m_Limbs);
        if (cmp == 0) return BigInt();

        BigInt result;
        result.m_Negative = (cmp > 0) ? m_Negative : other.m_Negative;
        result.m_Limbs = (cmp > 0)
            ? SubMagnitude(m_Limbs, other.m_Limbs)
            : SubMagnitude(other.m_Limbs, m_Limbs);
        result.Trim();
        return result;
    }

    BigInt operator-(const BigInt& other) const
    {
        return *this + (-other);
    }

    BigInt operator*(const BigInt& other) const
    {
        BigInt result;
        if (IsZero() || other.IsZero()) return result;

        result.m_Limbs.assign(m_Limbs.size() + other.m_Limbs.size(), 0);
        result.m_Negative = (m_Negative != other.m_Negative);

        for (std::size_t i = 0; i < m_Limbs.size(); ++i)
        {
            std::uint64_t carry = 0;
            for (std::size_t j = 0; j < other.m_Limbs.size(); ++j)
            {
                const std::uint64_t current =
                    std::uint64_t(m_Limbs[i]) * other.m_Limbs[j] +
                    result.m_Limbs[i + j] + carry;
                result.m_Limbs[i + j] = static_cast<std::uint32_t>(current);
                carry = current >> 32;
            }
            result.m_Limbs[i + other.m_Limbs.size()] += static_cast<std::uint32_t>(carry);
        }

        result.Trim();
        return result;
    }

    // Operações bitwise com sinal em complemento de dois de largura
    // ilimitada (extensão de sinal infinita, como em Python/Zig).
    BigInt operator&(const BigInt& other) const
    {
        return Bitwise(other, [](std::uint32_t a, std::uint32_t b) { return a & b; });
    }

    BigInt operator|(const BigInt& other) const
    {
        return Bitwise(other, [](std::uint32_t a, std::uint32_t b) { return a | b; });
    }

    BigInt operator^(const BigInt& other) const
    {
        return Bitwise(other, [](std::uint32_t a, std::uint32_t b) { return a ^ b; });
    }

    BigInt operator~() const
    {
        const std::size_t words = m_Limbs.size() + 1;
        std::vector<std::uint32_t> raw = ToTwos(words);

        for (std::uint32_t& limb : raw)
        {
            limb = ~limb;
        }

        return FromTwos(std::move(raw));
    }

    // Divisão inteira truncada em direção a zero, como em C/C++.
    std::optional<std::pair<BigInt, BigInt>> DivMod(const BigInt& divisor) const
    {
        if (divisor.IsZero()) return std::nullopt;

        BigInt dividend = *this;
        BigInt div = divisor;
        dividend.m_Negative = false;
        div.m_Negative = false;

        if (CompareMagnitude(dividend.m_Limbs, div.m_Limbs) < 0)
        {
            BigInt zero;
            BigInt remainder = *this;
            return std::make_pair(zero, remainder);
        }

        BigInt quotient;
        BigInt remainder;

        const std::uint32_t bits = dividend.BitLength();
        quotient.m_Limbs.assign((bits + 31) / 32, 0);

        for (std::size_t i = bits; i-- > 0;)
        {
            remainder.ShiftLeftSmall(1);
            if ((dividend.m_Limbs[i / 32] >> (i % 32)) & 1u)
            {
                remainder.AddSmall(1);
            }

            if (CompareMagnitude(remainder.m_Limbs, div.m_Limbs) >= 0)
            {
                remainder.m_Limbs = SubMagnitude(remainder.m_Limbs, div.m_Limbs);
                remainder.Trim();
                quotient.m_Limbs[i / 32] |= (std::uint32_t(1) << (i % 32));
            }
        }

        quotient.Trim();
        quotient.m_Negative = (m_Negative != divisor.m_Negative) && !quotient.IsZero();
        remainder.m_Negative = m_Negative && !remainder.IsZero();

        return std::make_pair(quotient, remainder);
    }

    BigInt Shl(std::uint32_t bits) const
    {
        BigInt result = *this;
        const std::uint32_t words = bits / 32;
        const std::uint32_t rest = bits % 32;

        if (words > 0)
        {
            result.m_Limbs.insert(result.m_Limbs.begin(), words, 0);
        }

        if (rest > 0)
        {
            std::uint32_t carry = 0;
            for (std::size_t i = words; i < result.m_Limbs.size(); ++i)
            {
                const std::uint32_t next = result.m_Limbs[i];
                result.m_Limbs[i] = (next << rest) | carry;
                carry = next >> (32 - rest);
            }
            if (carry != 0) result.m_Limbs.push_back(carry);
        }

        return result;
    }

    BigInt Shr(std::uint32_t bits) const
    {
        BigInt result = *this;
        result.m_Negative = false;

        const std::uint32_t words = bits / 32;
        const std::uint32_t rest = bits % 32;

        if (words >= result.m_Limbs.size())
        {
            return BigInt();
        }

        result.m_Limbs.erase(result.m_Limbs.begin(), result.m_Limbs.begin() + words);

        if (rest > 0)
        {
            for (std::size_t i = 0; i < result.m_Limbs.size(); ++i)
            {
                const std::uint32_t next = (i + 1 < result.m_Limbs.size())
                    ? result.m_Limbs[i + 1]
                    : 0;
                result.m_Limbs[i] = (result.m_Limbs[i] >> rest) | (next << (32 - rest));
            }
        }

        result.Trim();
        return result;
    }

    // Shift aritmético: >> de negativos arredonda para baixo (−∞), como o
    // sar do x86 e o >> de inteiros com sinal em Zig.
    BigInt ShrArithmetic(std::uint32_t bits) const
    {
        if (!IsNegative())
        {
            return Shr(bits);
        }

        auto divided = DivMod(BigInt(1).Shl(bits));
        if (!divided.has_value())
        {
            return BigInt();
        }

        BigInt quotient = divided->first;
        if (!divided->second.IsZero())
        {
            quotient = quotient - BigInt(1);
        }

        return quotient;
    }

private:
    template <typename Operation>
    BigInt Bitwise(const BigInt& other, Operation operation) const
    {
        const std::size_t words =
            std::max(m_Limbs.size(), other.m_Limbs.size()) + 1;

        std::vector<std::uint32_t> left = ToTwos(words);
        const std::vector<std::uint32_t> right = other.ToTwos(words);

        for (std::size_t i = 0; i < words; ++i)
        {
            left[i] = operation(left[i], right[i]);
        }

        return FromTwos(std::move(left));
    }

    std::vector<std::uint32_t> ToTwos(std::size_t words) const
    {
        std::vector<std::uint32_t> raw(words, 0);

        const std::size_t count = std::min(words, m_Limbs.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            raw[i] = m_Limbs[i];
        }

        if (!m_Negative || IsZero())
        {
            return raw;
        }

        std::uint64_t carry = 1;
        for (std::uint32_t& limb : raw)
        {
            const std::uint64_t current = std::uint64_t(~limb) + carry;
            limb = static_cast<std::uint32_t>(current);
            carry = current >> 32;
        }

        return raw;
    }

    static BigInt FromTwos(std::vector<std::uint32_t> raw)
    {
        BigInt result;

        if (raw.empty())
        {
            return result;
        }

        const bool negative = (raw.back() >> 31) != 0;

        if (negative)
        {
            std::uint64_t carry = 1;
            for (std::uint32_t& limb : raw)
            {
                const std::uint64_t current = std::uint64_t(~limb) + carry;
                limb = static_cast<std::uint32_t>(current);
                carry = current >> 32;
            }

            result.m_Negative = true;
        }

        result.m_Limbs = std::move(raw);
        result.Trim();
        return result;
    }

    bool m_Negative = false;
    std::vector<std::uint32_t> m_Limbs; // little-endian, sem zeros à esquerda

    void SetMagnitude(std::uint64_t value)
    {
        m_Limbs.clear();
        while (value != 0)
        {
            m_Limbs.push_back(static_cast<std::uint32_t>(value));
            value >>= 32;
        }
    }

    void Trim()
    {
        while (!m_Limbs.empty() && m_Limbs.back() == 0)
        {
            m_Limbs.pop_back();
        }
        if (m_Limbs.empty()) m_Negative = false;
    }

    void MulSmall(std::uint32_t factor)
    {
        std::uint64_t carry = 0;
        for (std::uint32_t& limb : m_Limbs)
        {
            const std::uint64_t current = std::uint64_t(limb) * factor + carry;
            limb = static_cast<std::uint32_t>(current);
            carry = current >> 32;
        }
        if (carry != 0) m_Limbs.push_back(static_cast<std::uint32_t>(carry));
    }

    void AddSmall(std::uint32_t value)
    {
        std::uint64_t carry = value;
        for (std::size_t i = 0; i < m_Limbs.size() && carry != 0; ++i)
        {
            const std::uint64_t current = std::uint64_t(m_Limbs[i]) + carry;
            m_Limbs[i] = static_cast<std::uint32_t>(current);
            carry = current >> 32;
        }
        if (carry != 0) m_Limbs.push_back(static_cast<std::uint32_t>(carry));
        if (m_Limbs.empty() && value != 0) m_Limbs.push_back(value);
    }

    void ShiftLeftSmall(std::uint32_t bits)
    {
        const BigInt shifted = Shl(bits);
        m_Limbs = shifted.m_Limbs;
    }

    static int CompareMagnitude(
        const std::vector<std::uint32_t>& left,
        const std::vector<std::uint32_t>& right
    )
    {
        if (left.size() != right.size())
        {
            return left.size() < right.size() ? -1 : 1;
        }

        for (std::size_t i = left.size(); i-- > 0;)
        {
            if (left[i] != right[i])
            {
                return left[i] < right[i] ? -1 : 1;
            }
        }

        return 0;
    }

    static std::vector<std::uint32_t> AddMagnitude(
        const std::vector<std::uint32_t>& left,
        const std::vector<std::uint32_t>& right
    )
    {
        std::vector<std::uint32_t> result;
        const std::size_t size = std::max(left.size(), right.size());
        result.reserve(size + 1);

        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < size; ++i)
        {
            const std::uint64_t current =
                carry +
                (i < left.size() ? left[i] : 0) +
                (i < right.size() ? right[i] : 0);
            result.push_back(static_cast<std::uint32_t>(current));
            carry = current >> 32;
        }

        if (carry != 0) result.push_back(static_cast<std::uint32_t>(carry));
        return result;
    }

    static std::vector<std::uint32_t> SubMagnitude(
        const std::vector<std::uint32_t>& left,
        const std::vector<std::uint32_t>& right
    )
    {
        std::vector<std::uint32_t> result;
        result.reserve(left.size());

        std::int64_t borrow = 0;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            std::int64_t current =
                std::int64_t(left[i]) -
                (i < right.size() ? right[i] : 0) -
                borrow;
            borrow = current < 0 ? 1 : 0;
            if (current < 0) current += (std::int64_t(1) << 32);
            result.push_back(static_cast<std::uint32_t>(current));
        }

        return result;
    }
};

}
