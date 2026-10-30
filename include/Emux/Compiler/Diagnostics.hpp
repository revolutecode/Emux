#pragma once

#include <string>
#include <cstdint>
#include <vector>

#include <Emux/Compiler/Diagnostic.hpp>

namespace Emux
{


class Diagnostics
{
public:

    void Add(
        DiagnosticLevel level,
        const SourceLocation& location,
        std::string_view message
    );

    // Mescla diagnósticos de outro contexto preservando a ordem.
    void Append(const Diagnostics& other);

    bool HasErrors() const;

    size_t Count() const;

    void Print() const;

    void Clear();

private:

    std::vector<Diagnostic> m_Diagnostics;
};


}