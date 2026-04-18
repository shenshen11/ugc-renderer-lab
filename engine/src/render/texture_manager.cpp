#include "ugc_renderer/render/texture_manager.h"

#include "ugc_renderer/render/image_loader.h"
#include "ugc_renderer/render/texture2d.h"

namespace ugc_renderer
{
TextureAsset::~TextureAsset() = default;

TextureManager::~TextureManager() = default;

void TextureManager::Initialize(ID3D12Device& device, DescriptorAllocator& srvAllocator)
{
    device_ = &device;
    srvAllocator_ = &srvAllocator;
}

std::uint32_t TextureManager::LoadFromFile(ID3D12GraphicsCommandList& commandList, const std::filesystem::path& path)
{
    const auto normalizedPath = std::filesystem::absolute(path).lexically_normal();
    const auto key = normalizedPath.wstring();

    if (const auto iterator = indicesByPath_.find(key); iterator != indicesByPath_.end())
    {
        return iterator->second;
    }

    const ImageData imageData = ImageLoader::LoadRgba8(normalizedPath);

    TextureAsset asset = {};
    asset.sourcePath = normalizedPath;
    asset.texture = std::make_unique<Texture2D>();
    asset.texture->Initialize(
        *device_,
        commandList,
        imageData.width,
        imageData.height,
        imageData.format,
        std::span(imageData.pixels));
    asset.srvAllocation = srvAllocator_->Allocate(1);

    D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
    shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shaderResourceViewDesc.Format = imageData.format;
    shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
    shaderResourceViewDesc.Texture2D.MipLevels = 1;
    shaderResourceViewDesc.Texture2D.PlaneSlice = 0;
    shaderResourceViewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    device_->CreateShaderResourceView(asset.texture->GetResource(), &shaderResourceViewDesc, asset.srvAllocation.GetCpuHandle());

    const std::uint32_t index = static_cast<std::uint32_t>(assets_.size());
    assets_.push_back(std::move(asset));
    indicesByPath_.emplace(key, index);
    return index;
}

const DescriptorAllocation& TextureManager::GetSrvAllocation(const std::uint32_t index) const
{
    return assets_.at(index).srvAllocation;
}

void TextureManager::ReleaseUploadResources() noexcept
{
    for (auto& asset : assets_)
    {
        if (asset.texture != nullptr)
        {
            asset.texture->ReleaseUploadResource();
        }
    }
}
} // namespace ugc_renderer
