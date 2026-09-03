#include "Emux/Compiler/Diagnostic.hpp"
#include <Emux/Compiler/Diagnostics.hpp>

#include <iostream>
#include <stdexcept>


namespace Emux
{


void Diagnostics::Add(
    DiagnosticLevel level,
    const SourceLocation& location,
    std::string_view message
)
{
    m_Diagnostics.push_back(
    {
        level,
        location,
        std::string(message)
    });

    if (level == DiagnosticLevel::Fatal)
    {
        throw std::runtime_error(message.data());
    }
}

bool Diagnostics::HasErrors() const
{
    for(const auto& diagnostic : m_Diagnostics)
    {
        if(diagnostic.level == DiagnosticLevel::Error ||
           diagnostic.level == DiagnosticLevel::Fatal)
        {
            return true;
        }
    }

    return false;
}

size_t Diagnostics::Count() const
{
    return m_Diagnostics.size();
}

void Diagnostics::Clear()
{
    m_Diagnostics.clear();
}

void Diagnostics::Print() const
{
    for(const auto& diagnostic : m_Diagnostics)
    {
        std::cerr
            << diagnostic.location.File
            << ':'
            << diagnostic.location.Line
            << ':'
            << diagnostic.location.Column
            << ": ";

        switch(diagnostic.level)
        {
            case DiagnosticLevel::Note:
                std::cerr << "note";
                break;

            case DiagnosticLevel::Warning:
                std::cerr << "warning";
                break;

            case DiagnosticLevel::Error:
                std::cerr << "error";
                break;

            case DiagnosticLevel::Fatal:
                std::cerr << "fatal";
                break;
        }

        std::cerr
            << ": "
            << diagnostic.message
            << '\n';
    }
}


}
