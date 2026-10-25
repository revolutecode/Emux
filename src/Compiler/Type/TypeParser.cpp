#include <Emux/Compiler/Type/TypeParser.hpp>

#include <charconv>
#include <cctype>

namespace Emux
{

std::optional<Type> TypeParser::Parse(
    std::string_view text
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
        auto pointee = Parse(text.substr(0, text.size() - 1));
        if (!pointee.has_value())
        {
            return std::nullopt;
        }
        return Type::MakePointer(*pointee);
    }

    if (text == "void")
    {
        return Type::MakeVoid();
    }

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

        case 'b':
        case 'B':
            kind = TypeKind::Buffer;
            break;

        default:
            return std::nullopt;
    }

    std::uint32_t amount = 0;

    auto result = std::from_chars(
        text.data() + 1,
        text.data() + text.size(),
        amount
    );

    if (result.ec != std::errc{} || amount == 0)
    {
        return std::nullopt;
    }

    bool isBytes = std::isupper(static_cast<unsigned char>(text[0]));

    std::uint32_t bits = amount;
    if (isBytes)
    {
        bits *= 8;
    }

    return Type(kind, bits);
}

}
