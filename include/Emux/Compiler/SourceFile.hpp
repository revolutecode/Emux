#pragma once

#include <string>
#include <utility>


namespace Emux
{

class SourceFile
{

public:

    SourceFile() = default;


    SourceFile(
        std::string name,
        std::string content
    )
        :
        m_Name(std::move(name)),
        m_Content(std::move(content))
    {
    }


    const std::string& GetName() const
    {
        return m_Name;
    }


    const std::string& GetContent() const
    {
        return m_Content;
    }

    size_t Size() const
    {
        return m_Content.size();
    }

    char operator[](size_t index) const
    {
        return m_Content[index];
    }


private:

    std::string m_Name;

    std::string m_Content;

};

}