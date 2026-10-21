#include <Emux/CLI/FileReader.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>


namespace Emux
{


SourceFile FileReader::Read(
    const std::string& path
)
{

    std::ifstream file(path);


    if(!file)
    {
        throw std::runtime_error(
            "Failed to open file: " + path
        );
    }


    std::stringstream buffer;

    buffer << file.rdbuf();


    return SourceFile(
        path,
        buffer.str()
    );
}


}