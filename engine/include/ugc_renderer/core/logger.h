#pragma once

#include <string_view>

namespace ugc_renderer
{
class Logger
{
public:
    static void Info(std::string_view message);
    static void Error(std::string_view message);
};
} // namespace ugc_renderer
