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

        const std::uint64_t limit = m_Negative
            ? (std::uint64_t(1) << 63)
            : ((std::uint64_t(1) << 63) - 1);

        return FitsUInt64() && ToUInt64() <= limit;
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

    std::int64_t ToInt64() const
    {
        std::uint64_t magnitude = ToUInt64();
        if (!m_Negative) return static_cast<std::int64_t>(magnitude);
        if (magnitude == (std::uint64_t(1) << 63)) return INT64_MIN;
        return -static_cast<std::int64_t>(magnitude);
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

private:
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
