#pragma once

#include "ugc_renderer/render/descriptor_allocator.h"

#include <cstdint>
#include <filesystem>
#include <memory>
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

class TextureManager
{
public:
    TextureManager() = default;
    ~TextureManager();

    void Initialize(ID3D12Device& device, DescriptorAllocator& srvAllocator);

    std::uint32_t LoadFromFile(ID3D12GraphicsCommandList& commandList, const std::filesystem::path& path);
    const DescriptorAllocation& GetSrvAllocation(std::uint32_t index) const;
    void ReleaseUploadResources() noexcept;

private:
    ID3D12Device* device_ = nullptr;
    DescriptorAllocator* srvAllocator_ = nullptr;
    std::unordered_map<std::wstring, std::uint32_t> indicesByPath_;
    std::vector<TextureAsset> assets_;
};
} // namespace ugc_renderer
