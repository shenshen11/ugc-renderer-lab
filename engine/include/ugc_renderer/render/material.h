#pragma once

#include <cstdint>
#include <limits>

#include <DirectXMath.h>

namespace ugc_renderer
{
inline constexpr std::uint32_t kInvalidTextureIndex = std::numeric_limits<std::uint32_t>::max();

enum class MaterialAlphaMode : std::uint32_t
{
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

struct MaterialConstants
{
    DirectX::XMFLOAT4 baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT4 emissiveFactorAndMetallic = {0.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 roughnessUvScaleAlphaCutoff = {1.0f, 1.0f, 1.0f, 0.5f};
    DirectX::XMFLOAT4 textureControls = {1.0f, 1.0f, 0.0f, 0.0f};
};

struct MaterialTextureSlots
{
    std::uint32_t baseColor = kInvalidTextureIndex;
    std::uint32_t normal = kInvalidTextureIndex;
    std::uint32_t metallicRoughness = kInvalidTextureIndex;
    std::uint32_t occlusion = kInvalidTextureIndex;
    std::uint32_t emissive = kInvalidTextureIndex;
};

struct MaterialDesc
{
    MaterialConstants constants = {};
    MaterialTextureSlots textures = {};
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    bool doubleSided = false;
};
} // namespace ugc_renderer
