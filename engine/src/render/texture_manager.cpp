#include "ugc_renderer/render/texture_manager.h"

#include "ugc_renderer/render/image_loader.h"
#include "ugc_renderer/render/texture2d.h"

#include <array>
#include <cstddef>

namespace ugc_renderer
{
TextureAsset::~TextureAsset() = default;

TextureManager::~TextureManager() = default;

void TextureManager::Initialize(ID3D12Device& device, DescriptorAllocator& srvAllocator)
{
    device_ = &device;
    srvAllocator_ = &srvAllocator;
}

void TextureManager::CreateDefaultTextures(ID3D12GraphicsCommandList& commandList)
{
    if (defaultTextureIndices_[static_cast<std::size_t>(DefaultTextureKind::White)] != kInvalidTextureIndex)
    {
        return;
    }

    defaultTextureIndices_[static_cast<std::size_t>(DefaultTextureKind::White)] =
        CreateSolidColorTexture(commandList, 255, 255, 255, 255, L"__default_white");
    defaultTextureIndices_[static_cast<std::size_t>(DefaultTextureKind::Black)] =
        CreateSolidColorTexture(commandList, 0, 0, 0, 255, L"__default_black");
    defaultTextureIndices_[static_cast<std::size_t>(DefaultTextureKind::FlatNormal)] =
        CreateSolidColorTexture(commandList, 128, 128, 255, 255, L"__default_flat_normal");
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
    return CreateTextureAsset(
        commandList,
        std::span(imageData.pixels),
        imageData.width,
        imageData.height,
        imageData.format,
        normalizedPath);
}

std::uint32_t TextureManager::CreateFromMemory(
    ID3D12GraphicsCommandList& commandList,
    const std::span<const std::byte> pixelData,
    const std::uint32_t width,
    const std::uint32_t height,
    const DXGI_FORMAT format,
    const std::filesystem::path& sourcePath)
{
    return CreateTextureAsset(commandList, pixelData, width, height, format, sourcePath);
}

std::uint32_t TextureManager::CreateTextureAsset(
    ID3D12GraphicsCommandList& commandList,
    const std::span<const std::byte> pixelData,
    const std::uint32_t width,
    const std::uint32_t height,
    const DXGI_FORMAT format,
    const std::filesystem::path& sourcePath)
{
    TextureAsset asset = {};
    asset.sourcePath = sourcePath;
    asset.texture = std::make_unique<Texture2D>();
    asset.texture->Initialize(
        *device_,
        commandList,
        width,
        height,
        format,
        pixelData);
    asset.srvAllocation = srvAllocator_->Allocate(1);

    D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
    shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shaderResourceViewDesc.Format = format;
    shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
    shaderResourceViewDesc.Texture2D.MipLevels = 1;
    shaderResourceViewDesc.Texture2D.PlaneSlice = 0;
    shaderResourceViewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    device_->CreateShaderResourceView(asset.texture->GetResource(), &shaderResourceViewDesc, asset.srvAllocation.GetCpuHandle());

    const std::uint32_t index = static_cast<std::uint32_t>(assets_.size());
    assets_.push_back(std::move(asset));
    if (!sourcePath.empty())
    {
        indicesByPath_.emplace(sourcePath.wstring(), index);
    }
    return index;
}

std::uint32_t TextureManager::CreateSolidColorTexture(
    ID3D12GraphicsCommandList& commandList,
    const std::uint8_t red,
    const std::uint8_t green,
    const std::uint8_t blue,
    const std::uint8_t alpha,
    const std::filesystem::path& sourcePath)
{
    constexpr std::uint32_t textureWidth = 2;
    constexpr std::uint32_t textureHeight = 2;
    std::array<std::byte, textureWidth * textureHeight * 4> pixels = {};
    for (std::size_t pixelIndex = 0; pixelIndex < textureWidth * textureHeight; ++pixelIndex)
    {
        const std::size_t byteOffset = pixelIndex * 4;
        pixels[byteOffset + 0] = static_cast<std::byte>(red);
        pixels[byteOffset + 1] = static_cast<std::byte>(green);
        pixels[byteOffset + 2] = static_cast<std::byte>(blue);
        pixels[byteOffset + 3] = static_cast<std::byte>(alpha);
    }

    return CreateTextureAsset(
        commandList,
        pixels,
        textureWidth,
        textureHeight,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        sourcePath);
}

const DescriptorAllocation& TextureManager::GetSrvAllocation(const std::uint32_t index) const
{
    return assets_.at(index).srvAllocation;
}

const TextureAsset& TextureManager::GetTextureAsset(const std::uint32_t index) const
{
    return assets_.at(index);
}

std::uint32_t TextureManager::GetDefaultTextureIndex(const DefaultTextureKind kind) const noexcept
{
    return defaultTextureIndices_[static_cast<std::size_t>(kind)];
}

std::uint32_t TextureManager::ResolveTextureIndex(
    const std::uint32_t requestedIndex,
    const DefaultTextureKind fallbackKind) const noexcept
{
    if (requestedIndex == kInvalidTextureIndex)
    {
        return GetDefaultTextureIndex(fallbackKind);
    }

    return requestedIndex;
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
