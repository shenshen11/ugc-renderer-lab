#pragma once

#include "ugc_renderer/render/descriptor_allocator.h"
#include "ugc_renderer/render/material.h"

#include <DirectXMath.h>

#include <memory>

namespace ugc_renderer
{
class ConstantBuffer;
class Mesh;

struct RenderItem
{
    Mesh* mesh = nullptr;
    Material material = {};
    DirectX::XMFLOAT3 translation = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 scale = {1.0f, 1.0f, 1.0f};
    float rotationOffset = 0.0f;
    float rotationSpeed = 1.0f;
    std::unique_ptr<ConstantBuffer> constantBuffer;
    DescriptorAllocation cbvAllocation = {};
};
} // namespace ugc_renderer
