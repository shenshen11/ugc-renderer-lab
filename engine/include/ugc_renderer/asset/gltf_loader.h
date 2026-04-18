#pragma once

#include "ugc_renderer/asset/gltf_document.h"

#include <filesystem>

namespace ugc_renderer
{
class GltfLoader
{
public:
    static GltfDocument LoadFromFile(const std::filesystem::path& path);
};
} // namespace ugc_renderer
