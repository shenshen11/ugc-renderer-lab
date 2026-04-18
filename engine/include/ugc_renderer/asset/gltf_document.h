#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace ugc_renderer
{
inline constexpr std::uint32_t kInvalidGltfIndex = std::numeric_limits<std::uint32_t>::max();

struct GltfAssetMetadata
{
    std::string version;
    std::string generator;
};

struct GltfBuffer
{
    std::string uri;
    std::filesystem::path resolvedPath;
    std::uint32_t byteLength = 0;
    bool isDataUri = false;
};

struct GltfBufferView
{
    std::uint32_t buffer = kInvalidGltfIndex;
    std::uint32_t byteOffset = 0;
    std::uint32_t byteLength = 0;
    std::uint32_t byteStride = 0;
    std::uint32_t target = 0;
};

struct GltfAccessor
{
    std::uint32_t bufferView = kInvalidGltfIndex;
    std::uint32_t byteOffset = 0;
    std::uint32_t componentType = 0;
    std::uint32_t count = 0;
    bool normalized = false;
    std::string type;
    std::vector<double> minValues;
    std::vector<double> maxValues;
};

struct GltfImage
{
    std::string name;
    std::string uri;
    std::filesystem::path resolvedPath;
    std::string mimeType;
    std::uint32_t bufferView = kInvalidGltfIndex;
    bool isDataUri = false;
};

struct GltfSampler
{
    std::uint32_t magFilter = 0;
    std::uint32_t minFilter = 0;
    std::uint32_t wrapS = 10497;
    std::uint32_t wrapT = 10497;
};

struct GltfTexture
{
    std::string name;
    std::uint32_t sampler = kInvalidGltfIndex;
    std::uint32_t source = kInvalidGltfIndex;
};

struct GltfTextureReference
{
    std::uint32_t texture = kInvalidGltfIndex;
    std::uint32_t texCoord = 0;
    float scale = 1.0f;
    float strength = 1.0f;
};

struct GltfMaterial
{
    std::string name;
    std::array<float, 4> baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> emissiveFactor = {0.0f, 0.0f, 0.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float alphaCutoff = 0.5f;
    std::string alphaMode = "OPAQUE";
    bool doubleSided = false;
    GltfTextureReference baseColorTexture = {};
    GltfTextureReference metallicRoughnessTexture = {};
    GltfTextureReference normalTexture = {};
    GltfTextureReference occlusionTexture = {};
    GltfTextureReference emissiveTexture = {};
};

struct GltfPrimitive
{
    std::unordered_map<std::string, std::uint32_t> attributes;
    std::uint32_t indices = kInvalidGltfIndex;
    std::uint32_t material = kInvalidGltfIndex;
    std::uint32_t mode = 4;
};

struct GltfMesh
{
    std::string name;
    std::vector<GltfPrimitive> primitives;
};

struct GltfNode
{
    std::string name;
    std::uint32_t mesh = kInvalidGltfIndex;
    std::vector<std::uint32_t> children;
    std::array<float, 3> translation = {0.0f, 0.0f, 0.0f};
    std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
    bool hasMatrix = false;
    std::array<float, 16> matrix = {};
};

struct GltfScene
{
    std::string name;
    std::vector<std::uint32_t> nodes;
};

struct GltfDocument
{
    std::filesystem::path sourcePath;
    std::filesystem::path rootDirectory;
    GltfAssetMetadata asset = {};
    std::vector<GltfBuffer> buffers;
    std::vector<GltfBufferView> bufferViews;
    std::vector<GltfAccessor> accessors;
    std::vector<GltfImage> images;
    std::vector<GltfSampler> samplers;
    std::vector<GltfTexture> textures;
    std::vector<GltfMaterial> materials;
    std::vector<GltfMesh> meshes;
    std::vector<GltfNode> nodes;
    std::vector<GltfScene> scenes;
    std::uint32_t defaultScene = kInvalidGltfIndex;
};
} // namespace ugc_renderer
