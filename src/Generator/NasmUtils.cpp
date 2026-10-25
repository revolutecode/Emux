#include "Emux/Generator/NasmUtils.hpp"
#include "Emux/Compiler/Type/TypeParser.hpp"
#include "Emux/Core/Logger.hpp"
#include <sstream>
#include <stdexcept>

namespace Emux
{

namespace NasmUtils
{

std::pair<std::string, std::size_t> SplitOne(std::string_view input, char separator)
{
    auto pos = input.find_first_of(separator);
    return { std::string(input.substr(0, pos)), pos };
}

std::vector<std::string> Split(std::string_view input, char separator, bool respectQuotes)
{
    std::vector<std::string> vec;
    size_t index = 0;

    bool isString = false;
    while (index < input.size())
    {
        std::string element;
        for (auto it = input.begin() + index; it != input.end(); ++it)
        {
            bool end = (*it == separator) && (respectQuotes ? !isString : true);
            if (end) break;

            if (*it == '"') isString = !isString;

            element += *it;
            index++;
        }
        vec.push_back(element);
        index++;
    }

    return vec;
}

std::string Mangled(std::string_view name, uint32_t size, bool isStdcall, Architecture arch)
{
    if (arch == Architecture::X86_64)
    {
        // Windows x64 ABI não utiliza decoração de stdcall (@N) nem sublinhado inicial
        return std::string(name);
    }

    std::string res(name);
    res.insert(res.begin(), '_');
    if (isStdcall && size > 0)
    {
        res += "@";
        res += std::to_string(size);
    }
    return res;
}

std::string NasmSize(int bits)
{
    if (bits <= 8) return "byte";
    if (bits <= 16) return "word";
    if (bits <= 32) return "dword";
    if (bits <= 64) return "qword";
    throw std::runtime_error("Tipo maior que 64 bits nao suportado no backend nasm");
}

std::pair<std::string, Type> ParseDecl(std::string_view input)
{
    static TypeParser parser;

    auto [name, namePos] = SplitOne(input, ':');

    if (namePos == std::string::npos)
    {
        Logger::Error("Variavel '" + name + "' sem tipo declarado");
    }

    auto type = parser.Parse(input.substr(namePos + 1));

    if (!type.has_value())
    {
        Logger::Error("Tipo invalido: " + std::string(input.substr(namePos + 1)));
        return std::make_pair(name, Type{});
    }

    return std::make_pair(name, type.value());
}

} // namespace NasmUtils

} // namespace Emux
