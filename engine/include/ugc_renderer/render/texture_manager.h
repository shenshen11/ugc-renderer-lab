#pragma once

#include "ugc_renderer/render/descriptor_allocator.h"
#include "ugc_renderer/render/material.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace ugc_renderer
{
class Texture2D;

struct TextureAsset
{
    TextureAsset() = default;
    ~TextureAsset();
    TextureAsset(const TextureAsset&) = delete;
    TextureAsset& operator=(const TextureAsset&) = delete;
    TextureAsset(TextureAsset&&) noexcept = default;
    TextureAsset& operator=(TextureAsset&&) noexcept = default;

    std::filesystem::path sourcePath;
    std::unique_ptr<Texture2D> texture;
    DescriptorAllocation srvAllocation = {};
};

enum class DefaultTextureKind : std::uint32_t
{
    White,
    Black,
    FlatNormal,
};

class TextureManager
{
public:
    TextureManager() = default;
    ~TextureManager();

    void Initialize(ID3D12Device& device, DescriptorAllocator& srvAllocator);

    void CreateDefaultTextures(ID3D12GraphicsCommandList& commandList);
    std::uint32_t LoadFromFile(ID3D12GraphicsCommandList& commandList, const std::filesystem::path& path);
    std::uint32_t CreateFromMemory(
        ID3D12GraphicsCommandList& commandList,
        std::span<const std::byte> pixelData,
        std::uint32_t width,
        std::uint32_t height,
        DXGI_FORMAT format,
        const std::filesystem::path& sourcePath);
    const DescriptorAllocation& GetSrvAllocation(std::uint32_t index) const;
    const TextureAsset& GetTextureAsset(std::uint32_t index) const;
    std::uint32_t GetDefaultTextureIndex(DefaultTextureKind kind) const noexcept;
    std::uint32_t ResolveTextureIndex(std::uint32_t requestedIndex, DefaultTextureKind fallbackKind) const noexcept;
    void ReleaseUploadResources() noexcept;

private:
    std::uint32_t CreateTextureAsset(
        ID3D12GraphicsCommandList& commandList,
        std::span<const std::byte> pixelData,
        std::uint32_t width,
        std::uint32_t height,
        DXGI_FORMAT format,
        const std::filesystem::path& sourcePath);

    std::uint32_t CreateSolidColorTexture(
        ID3D12GraphicsCommandList& commandList,
        std::uint8_t red,
        std::uint8_t green,
        std::uint8_t blue,
        std::uint8_t alpha,
        const std::filesystem::path& sourcePath);

    ID3D12Device* device_ = nullptr;
    DescriptorAllocator* srvAllocator_ = nullptr;
    std::unordered_map<std::wstring, std::uint32_t> indicesByPath_;
    std::vector<TextureAsset> assets_;
    std::array<std::uint32_t, 3> defaultTextureIndices_ = {
        kInvalidTextureIndex,
        kInvalidTextureIndex,
        kInvalidTextureIndex,
    };
};
} // namespace ugc_renderer
