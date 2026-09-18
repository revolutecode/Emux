#include <Emux/Compiler/Semantic/TypeParser.hpp>

#include <charconv>
#include <cctype>

namespace Emux
{

std::optional<Type> TypeParser::Parse(
    std::string_view text
)
{
    if(text.size() < 2)
    {
        return std::nullopt;
    }

    if (text == "void")
    {
        return {

        };
    }

    TypeKind kind;


    switch(text[0])
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


    if(result.ec != std::errc{} ||
       amount == 0)
    {
        return std::nullopt;
    }

    bool isBytes = std::isupper(text[0]);

    std::uint32_t bits = amount;
    if (isBytes)
    {
        bits *= 8;
    }

    return Type
    {
        kind,
        bits
    };
}

}
