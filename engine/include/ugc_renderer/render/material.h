#pragma once

#include <DirectXMath.h>

namespace ugc_renderer
{
struct Material
{
    DirectX::XMFLOAT4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
};
} // namespace ugc_renderer
