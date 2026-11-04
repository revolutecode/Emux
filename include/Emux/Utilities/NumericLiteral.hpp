#pragma once

#include <Emux/Compiler/Type/Type.hpp>
#include <Emux/Utilities/BigInt.hpp>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace Emux
{

// Conversao de um literal inteiro ou decimal para a representacao de
// armazenamento de um tipo numerico: o valor de ponto fixo e escalado por
// 2^scale e os bits de um float seguem o formato fN descrito em Type.hpp.
namespace NumericLiteral
{

inline BigInt FixedStorageValue(const BigInt& value, const Type& type)
{
    const Type repr = type.Representation();

    if (!repr.IsFixed() || repr.Bits == 0)
    {
        return value;
    }

    return value.Shl(repr.Scale).WrappedTo(repr.Bits, true);
}

// Verifica se o texto e um literal decimal (parte inteira, '.', parte
// fracionaria). Hexadecimais e numeros sem ponto nao sao decimais.
inline bool IsDecimalText(std::string_view text)
{
    if (text.empty())
    {
        return false;
    }

    std::size_t i = 0;
    if (text[0] == '+' || text[0] == '-')
    {
        ++i;
    }

    bool hasDigits = false;
    bool hasDot = false;

    for (; i < text.size(); ++i)
    {
        const char c = text[i];

        if (c == '.')
        {
            if (hasDot)
            {
                return false;
            }

            hasDot = true;
            continue;
        }

        if (c < '0' || c > '9')
        {
            return false;
        }

        hasDigits = true;
    }

    return hasDot && hasDigits;
}

// Converte um literal decimal "N.M" num par (numerador, denominador) onde o
// denominador e uma potencia de 10. O sinal fica no numerador.
inline std::optional<std::pair<BigInt, BigInt>> ParseDecimal(std::string_view text)
{
    if (!IsDecimalText(text))
    {
        return std::nullopt;
    }

    std::size_t i = 0;
    bool negative = false;

    if (text[0] == '+' || text[0] == '-')
    {
        negative = text[0] == '-';
        ++i;
    }

    std::string digits;
    std::size_t fractionalDigits = 0;
    bool pastDot = false;

    for (; i < text.size(); ++i)
    {
        const char c = text[i];

        if (c == '.')
        {
            pastDot = true;
            continue;
        }

        digits.push_back(c);

        if (pastDot)
        {
            ++fractionalDigits;
        }
    }

    auto parsed = BigInt::Parse(digits, 10);
    if (!parsed.has_value())
    {
        return std::nullopt;
    }

    BigInt numerator = *parsed;
    BigInt denominator = BigInt(1);

    for (std::size_t k = 0; k < fractionalDigits; ++k)
    {
        denominator = denominator * BigInt(10);
    }

    if (negative && !numerator.IsZero())
    {
        numerator = -numerator;
    }

    return std::make_pair(numerator, denominator);
}

// Valor de armazenamento de ponto fixo para uma fracao num/den.
inline BigInt FixedStorageValue(
    const BigInt& numerator,
    const BigInt& denominator,
    const Type& type
)
{
    const Type repr = type.Representation();

    if (!repr.IsFixed() || repr.Bits == 0)
    {
        return BigInt(0);
    }

    if (numerator.IsZero())
    {
        return BigInt(0);
    }

    const bool negative = numerator.IsNegative();
    BigInt magnitude = negative ? -numerator : numerator;

    BigInt scaled = magnitude.Shl(repr.Scale);
    auto divided = scaled.DivMod(denominator);

    if (!divided.has_value())
    {
        return BigInt(0);
    }

    BigInt value = divided->first;
    const BigInt& remainder = divided->second;

    // Arredondamento para o par mais proximo.
    if (remainder.Shl(1).Compare(denominator) >= 0)
    {
        value = value + BigInt(1);
    }

    if (negative)
    {
        value = -value;
    }

    return value.WrappedTo(repr.Bits, true);
}

// Bits IEEE 754 de uma fracao num/den num float fN.
inline BigInt FloatStorageBits(
    const BigInt& numerator,
    const BigInt& denominator,
    const Type& type
)
{
    const Type repr = type.Representation();

    if (!repr.IsFloat() || repr.Bits == 0)
    {
        return BigInt(0);
    }

    const std::uint32_t bits = repr.Bits;
    const std::uint32_t significand = repr.FloatSignificandBits();
    const std::uint32_t mantissa = repr.FloatMantissaBits();

    if (significand == 0 || numerator.IsZero())
    {
        return BigInt(0);
    }

    const bool negative = numerator.IsNegative();
    BigInt n = negative ? -numerator : numerator;
    BigInt d = denominator;

    // Expoente: floor(log2(n/d)).
    std::int64_t e = static_cast<std::int64_t>(n.BitLength()) -
        static_cast<std::int64_t>(d.BitLength());

    BigInt lhs;
    BigInt rhs;

    if (e >= 0)
    {
        lhs = n;
        rhs = d.Shl(static_cast<std::uint32_t>(e));
    }
    else
    {
        lhs = n.Shl(static_cast<std::uint32_t>(-e));
        rhs = d;
    }

    if (lhs.Compare(rhs) < 0)
    {
        --e;
    }

    BigInt twoPm1 = BigInt(1).Shl(significand - 1);

    BigInt num2;
    BigInt den2;

    if (e >= static_cast<std::int64_t>(significand) - 1)
    {
        num2 = n;
        den2 = d.Shl(static_cast<std::uint32_t>(e - (significand - 1)));
    }
    else
    {
        num2 = n.Shl(static_cast<std::uint32_t>((significand - 1) - e));
        den2 = d;
    }

    auto divided = num2.DivMod(den2);
    if (!divided.has_value())
    {
        return BigInt(0);
    }

    BigInt mantissaInt = divided->first;
    const BigInt& remainder = divided->second;

    if (remainder.Shl(1).Compare(den2) >= 0)
    {
        mantissaInt = mantissaInt + BigInt(1);
    }

    BigInt twoP = BigInt(1).Shl(significand);
    if (mantissaInt.Compare(twoP) >= 0)
    {
        mantissaInt = twoPm1;
        ++e;
    }

    const std::int64_t bias = static_cast<std::int64_t>(repr.FloatBias());
    const std::int64_t fieldMax =
        (std::int64_t(1) << repr.FloatExponentBits()) - 1;

    std::int64_t field = e + bias;
    BigInt mantissaField;

    if (field >= fieldMax)
    {
        field = fieldMax;
        mantissaField = BigInt(0);
    }
    else if (field <= 0)
    {
        const std::int64_t shiftDown = 1 - field;

        if (shiftDown >= static_cast<std::int64_t>(significand))
        {
            return BigInt(0);
        }

        mantissaField = mantissaInt.Shr(static_cast<std::uint32_t>(shiftDown));
        field = 0;
    }
    else
    {
        mantissaField = mantissaInt - twoPm1;
    }

    BigInt result = BigInt(field).Shl(mantissa) | mantissaField;

    if (negative)
    {
        result = result | BigInt(1).Shl(bits - 1);
    }

    return result.WrappedTo(bits, false);
}

// Bits IEEE 754 (sinal, expoente com vies e mantissa sem o bit implicito) de
// um inteiro num float fN. Valores que estouram o expoente maximo resultam
// em infinito; o arredondamento nao e feito aqui (literais constantes sao
// validados como exatos pela analise semantica).
inline BigInt FloatStorageBits(const BigInt& value, const Type& type)
{
    const Type repr = type.Representation();

    if (!repr.IsFloat() || repr.Bits == 0)
    {
        return value;
    }

    const std::uint32_t bits = repr.Bits;
    const std::uint32_t significand = repr.FloatSignificandBits();
    const std::uint32_t mantissa = repr.FloatMantissaBits();

    if (significand == 0 || value.IsZero())
    {
        return BigInt(0);
    }

    const bool negative = value.IsNegative();
    BigInt magnitude = negative ? -value : value;

    const std::uint32_t length = magnitude.BitLength();
    const std::int64_t exponent = static_cast<std::int64_t>(length) - 1;

    if (length <= significand)
    {
        magnitude = magnitude.Shl(significand - length);
    }
    else
    {
        magnitude = magnitude.Shr(length - significand);
    }

    BigInt mantissaField = magnitude - BigInt(1).Shl(significand - 1);

    const std::int64_t bias = static_cast<std::int64_t>(repr.FloatBias());
    const std::int64_t fieldMax =
        (std::int64_t(1) << repr.FloatExponentBits()) - 1;

    std::int64_t field = exponent + bias;

    if (field >= fieldMax)
    {
        field = fieldMax;
        mantissaField = BigInt(0);
    }
    else if (field < 0)
    {
        // Inteiros >= 1 nunca chegam aqui (o menor normal e <= 1); por
        // seguranca, satura em zero.
        field = 0;
        mantissaField = BigInt(0);
    }

    BigInt result = BigInt(field).Shl(static_cast<std::uint32_t>(mantissa)) |
        mantissaField;

    if (negative)
    {
        result = result | BigInt(1).Shl(bits - 1);
    }

    return result.WrappedTo(bits, false);
}

} // namespace NumericLiteral

} // namespace Emux
