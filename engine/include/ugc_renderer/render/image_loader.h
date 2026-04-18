#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <dxgiformat.h>

namespace ugc_renderer
{
struct ImageData
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::vector<std::byte> pixels;
};

class ImageLoader
{
public:
    static ImageData LoadRgba8(const std::filesystem::path& path);
};
} // namespace ugc_renderer
