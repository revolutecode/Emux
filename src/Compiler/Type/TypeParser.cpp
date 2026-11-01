#include <Emux/Compiler/Type/TypeParser.hpp>

#include <charconv>
#include <cctype>

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

    // So os blocos usam a forma de bytes (B8 = 8 bytes). Nos restantes
    // tipos o numero e sempre em bits, incluindo o qN.K do ponto fixo.
    bool isBytes = kind == TypeKind::Buffer &&
        std::isupper(static_cast<unsigned char>(text[0]));

    std::uint64_t bits = isBytes ? amount * 8 : amount;

    // Blocos b sao regioes estaticas, por isso nao estao limitados a 64 bits.
    // O teto evita dimensoes absurdas no .bss e overflow no calculo.
    constexpr std::uint64_t maxBufferBits = 8ull * 1024 * 1024;

    // Inteiros, floats e ponto fixo podem ter qualquer largura ate 64 Kbit.
    // Larguras nao alinhadas a bytes sao normalizadas por mascaramento
    // (instrucao Exir `trunc`), por isso qualquer valor e valido.
    constexpr std::uint64_t maxArithmeticBits = 65536;

    if (kind == TypeKind::Buffer)
    {
        if (bits == 0 || bits > maxBufferBits)
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
