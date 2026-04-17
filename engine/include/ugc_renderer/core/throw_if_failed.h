#pragma once

#include <Windows.h>

#include <sstream>
#include <stdexcept>
#include <string_view>

namespace ugc_renderer
{
inline void ThrowIfFailed(const HRESULT result, const std::string_view context)
{
    if (SUCCEEDED(result))
    {
        return;
    }

    std::ostringstream stream;
    stream << context << " failed with HRESULT 0x" << std::hex << static_cast<unsigned long>(result);
    throw std::runtime_error(stream.str());
}
} // namespace ugc_renderer
