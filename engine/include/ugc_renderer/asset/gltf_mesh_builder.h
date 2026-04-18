#pragma once

#include "ugc_renderer/asset/gltf_document.h"

#include <cstdint>
#include <vector>

namespace ugc_renderer
{
struct GltfRuntimeVertex
{
    float position[3] = {};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float texCoord[2] = {};
};

struct GltfRuntimeMesh
{
    std::vector<GltfRuntimeVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::uint32_t material = kInvalidGltfIndex;
};

class GltfMeshBuilder
{
public:
    static GltfRuntimeMesh BuildFirstPrimitive(const GltfDocument& document);
};
} // namespace ugc_renderer
