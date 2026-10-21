#pragma once

#include <string>


namespace Emux
{

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};


class Logger
{
public:

    static void Log(
        LogLevel level,
        const std::string& message
    );


    static void Trace(
        const std::string& message
    );

    static void Debug(
        const std::string& message
    );

    static void Info(
        const std::string& message
    );

    static void Warning(
        const std::string& message
    );

    static void Error(
        const std::string& message
    );

    static void Fatal(
        const std::string& message
    );
};

}