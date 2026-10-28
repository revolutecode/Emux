#include <Emux/CLI/FileReader.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>


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

    return SourceFile(
        path.lexically_normal().string(),
        buffer.str()
    );
}


}