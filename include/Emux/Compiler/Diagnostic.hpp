#pragma once
#include <Emux/Compiler/SourceLocation.hpp>
#include <string>
#include <cstdint>

namespace Emux
{

enum class DiagnosticLevel
{
    Note,
    Warning,
    Error,
    Fatal
};	

struct Diagnostic
{
    DiagnosticLevel level;

    SourceLocation location;

    std::string message;
};


}