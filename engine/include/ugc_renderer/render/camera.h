#pragma once

#include <DirectXMath.h>

namespace ugc_renderer
{
struct Camera
{
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, -2.4f};
    DirectX::XMFLOAT3 target = {0.0f, 0.0f, 0.4f};
    DirectX::XMFLOAT3 up = {0.0f, 1.0f, 0.0f};
    float verticalFovRadians = DirectX::XM_PIDIV4;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};
} // namespace ugc_renderer
