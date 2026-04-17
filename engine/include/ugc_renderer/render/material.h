#pragma once

#include <cstdint>

#include <DirectXMath.h>

namespace ugc_renderer
{
struct Material
{
    DirectX::XMFLOAT4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    std::uint32_t textureIndex = 0;
};
} // namespace ugc_renderer
