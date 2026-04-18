#pragma once

#include "ugc_renderer/asset/gltf_document.h"

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

namespace ugc_renderer
{
struct GltfSceneMeshInstance
{
    std::uint32_t node = kInvalidGltfIndex;
    std::uint32_t mesh = kInvalidGltfIndex;
    DirectX::XMFLOAT4X4 worldTransform = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
};

class GltfSceneBuilder
{
public:
    static std::vector<GltfSceneMeshInstance> BuildMeshInstances(const GltfDocument& document);
};
} // namespace ugc_renderer
