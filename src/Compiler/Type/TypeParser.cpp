#include <Emux/Compiler/Type/TypeParser.hpp>

#include <charconv>
#include <cctype>
#include <cstdint>

namespace Emux
{

std::optional<Type> TypeParser::Parse(
    std::string_view text,
    std::uint32_t pointerBits
)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }

    if (text.empty())
    {
        return std::nullopt;
    }

    // Suporte a ponteiros: ex "u8*" ou "i32*"
    if (text.ends_with('*'))
    {
        auto pointee = Parse(text.substr(0, text.size() - 1), pointerBits);
        if (!pointee.has_value())
        {
            return std::nullopt;
        }
        return Type::MakePointer(*pointee, pointerBits);
    }

    if (text == "void")
    {
        return Type::MakeVoid();
    }

    // bool/usize/isize/uptr/iptr sao usings embutidos: a analise semantica
    // resolve-os com identidade nominal, por isso nao sao primitivos aqui.

    if (text.size() < 2)
    {
        return std::nullopt;
    }

    TypeKind kind = TypeKind::Invalid;

    switch (text[0])
    {
        case 'u':
        case 'U':
            kind = TypeKind::Unsigned;
            break;

        case 'i':
        case 'I':
            kind = TypeKind::Signed;
            break;

        case 'f':
        case 'F':
            kind = TypeKind::Float;
            break;

        case 'q':
        case 'Q':
            kind = TypeKind::Fixed;
            break;

        case 'b':
        case 'B':
            kind = TypeKind::Buffer;
            break;

        default:
            return std::nullopt;
    }

    std::uint64_t amount = 0;
    std::uint64_t scale = 0;
    std::string_view number = text.substr(1);
    std::string_view scaleText;

    // Ponto fixo: q<bits>.<scale>, ex "q32.16".
    if (kind == TypeKind::Fixed)
    {
        const std::size_t dot = number.find('.');
        if (dot == std::string_view::npos)
        {
            return std::nullopt;
        }

        scaleText = number.substr(dot + 1);
        number = number.substr(0, dot);

        if (number.empty() || scaleText.empty())
        {
            return std::nullopt;
        }

        auto scaleResult = std::from_chars(
            scaleText.data(),
            scaleText.data() + scaleText.size(),
            scale
        );

        if (scaleResult.ec != std::errc{} ||
            scaleResult.ptr != scaleText.data() + scaleText.size())
        {
            return std::nullopt;
        }
    }

    auto result = std::from_chars(
        number.data(),
        number.data() + number.size(),
        amount
    );

    if (result.ec != std::errc{} || result.ptr != number.data() + number.size() || amount == 0)
    {
        return std::nullopt;
    }

    // Tipos com letra maiuscula especificam tamanhos em bytes (ex.: U4 = 32 bits,
    // B8 = 64 bits, F4 = 32 bits). No ponto fixo (q/Q), o primeiro numero e sempre a
    // largura total e o segundo e a escala fracionaria, sendo (total - scale) a parte
    // inteira. Em q minusculo as grandezas sao em bits (ex.: q32.16 = 32 bits totais,
    // 16 bits fracionarios = 4 bytes); em Q maiusculo as grandezas sao em bytes
    // (ex.: Q32.16 = 32 bytes totais, 16 bytes fracionarios, 16 bytes inteiros).
    bool isBytes = std::isupper(static_cast<unsigned char>(text[0]));

    // Inteiros, floats e ponto fixo podem ter qualquer largura ate UINT32_MAX bits.
    // Larguras nao alinhadas a bytes sao normalizadas por mascaramento
    // (instrucao Exir `trunc`), por isso qualquer valor e valido.
    constexpr std::uint64_t maxArithmeticBits = UINT32_MAX;

    std::uint64_t bits = 0;
    if (kind == TypeKind::Fixed)
    {
        if (isBytes)
        {
            // Em Q maiusculo, tanto a largura total (amount) quanto a escala (scale)
            // sao expressas em bytes: Q<total_bytes>.<scale_bytes>.
            // Ex.: Q32.16 = 32 bytes totais (256 bits) com escala de 16 bytes (128 bits),
            // restando 16 bytes (128 bits) para a parte inteira (metade de cada lado).
            constexpr std::uint64_t maxByteComponent = maxArithmeticBits / 8;
            if (amount > maxByteComponent || scale > maxByteComponent)
            {
                return std::nullopt;
            }
            bits = amount * 8;
            scale = scale * 8;
        }
        else
        {
            // Em q minusculo, amount e a largura total em bits e scale sao os bits fracionarios.
            bits = amount;
        }
    }
    else
    {
        bits = isBytes ? amount * 8 : amount;
    }

    // Blocos b sao regioes estaticas, por isso nao estao limitados a 64 bits.
    // O teto evita dimensoes absurdas no .bss e overflow no calculo.
    constexpr std::uint64_t maxBufferBits = 8ull * 1024 * 1024;
    if (kind == TypeKind::Buffer)
    {
        // Blocos precisam de ser multiplos de 8 bits (alinhados a bytes);
        // larguras nao alinhadas a bytes nao tem correspondencia indexavel.
        if (bits == 0 || bits > maxBufferBits || bits % 8 != 0)
        {
            return std::nullopt;
        }
    }
    else if (kind == TypeKind::Fixed)
    {
        if (bits == 0 || bits > maxArithmeticBits ||
            scale == 0 || scale >= bits)
        {
            return std::nullopt;
        }
    }
    else if (kind == TypeKind::Float)
    {
        // O formato precisa de sinal, expoente e mantissa com bit implicito;
        // larguras nao alinhadas a bytes nao tem representacao natural.
        if (bits < 16 || bits > maxArithmeticBits || bits % 8 != 0)
        {
            return std::nullopt;
        }
    }
    else if (bits == 0 || bits > maxArithmeticBits)
    {
        return std::nullopt;
    }

    if (kind == TypeKind::Fixed)
    {
        return Type::MakeFixed(
            static_cast<std::uint32_t>(bits),
            static_cast<std::uint32_t>(scale)
        );
    }

    return Type(kind, static_cast<std::uint32_t>(bits));
}

}
