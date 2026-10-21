#pragma once

#include <string>

#include <Emux/Compiler/SourceFile.hpp>


namespace Emux
{

class FileReader
{

public:

    static SourceFile Read(
        const std::string& path
    );

};

}