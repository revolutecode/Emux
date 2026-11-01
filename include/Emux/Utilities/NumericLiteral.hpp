#pragma once

#include <Emux/Compiler/Type/Type.hpp>
#include <Emux/Utilities/BigInt.hpp>
#include <cstdint>

namespace Emux
{

// Conversao de um literal inteiro para a representacao de armazenamento de
// um tipo numerico: o valor de ponto fixo e escalado por 2^scale e os bits
// de um float seguem o formato fN descrito em Type.hpp.
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
