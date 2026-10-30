#include <Emux/Core/Logger.hpp>

#include <iostream>
#include <mutex>
#include <sstream>


namespace Emux
{


static const char* LevelName(LogLevel level)
{
    switch(level)
    {
        case LogLevel::Trace:
            return "TRACE";

        case LogLevel::Debug:
            return "DEBUG";

        case LogLevel::Info:
            return "INFO";

        case LogLevel::Warning:
            return "WARNING";

        case LogLevel::Error:
            return "ERROR";

        case LogLevel::Fatal:
            return "FATAL";
    }

    return "UNKNOWN";
}


void Logger::Log(
    LogLevel level,
    const std::string& message
)
{
    std::ostringstream line;
    line
        << "["
        << LevelName(level)
        << "] "
        << message
        << '\n';

    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::cout << line.str();
}


void Logger::Trace(const std::string& m)
{
    Log(LogLevel::Trace, m);
}


void Logger::Debug(const std::string& m)
{
    Log(LogLevel::Debug, m);
}


void Logger::Info(const std::string& m)
{
    Log(LogLevel::Info, m);
}


void Logger::Warning(const std::string& m)
{
    Log(LogLevel::Warning, m);
}


void Logger::Error(const std::string& m)
{
    Log(LogLevel::Error, m);
}


void Logger::Fatal(const std::string& m)
{
    Log(LogLevel::Fatal, m);
}


}