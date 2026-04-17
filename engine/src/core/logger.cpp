#include "ugc_renderer/core/logger.h"

#include <Windows.h>

#include <cstdio>
#include <string>

namespace ugc_renderer
{
namespace
{
void Write(const char* level, const std::string_view message)
{
    std::string line = "[";
    line += level;
    line += "] ";
    line.append(message.begin(), message.end());
    line += '\n';

    OutputDebugStringA(line.c_str());
    std::fputs(line.c_str(), stderr);
}
} // namespace

void Logger::Info(const std::string_view message)
{
    Write("Info", message);
}

void Logger::Error(const std::string_view message)
{
    Write("Error", message);
}
} // namespace ugc_renderer
