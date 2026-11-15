#include <Emux/CLI/FileReader.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>


namespace Emux
{


SourceFile FileReader::Read(
    const std::filesystem::path& path
)
{

    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error(
            "Doesn't exist file: " + path.lexically_normal().string()
        );
        return {};
    }

    std::ifstream file(path);

    if(!file)
    {
        throw std::runtime_error(
            "Failed to open file: " + path.lexically_normal().string()
        );
        return {};
    }

    std::stringstream buffer;

    buffer << file.rdbuf();

    std::string content = buffer.str();

    // Editores Windows gravam UTF-8 com BOM por omissao. O lexer espera que o
    // primeiro token seja '['; remover o BOM aqui mantem os offsets alinhados.
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF)
    {
        content.erase(0, 3);
    }

    return SourceFile(
        path.lexically_normal().string(),
        std::move(content)
    );
}


}