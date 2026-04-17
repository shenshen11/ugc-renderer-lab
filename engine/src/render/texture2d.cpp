#include "ugc_renderer/render/texture2d.h"

#include "ugc_renderer/core/throw_if_failed.h"

#include <algorithm>
#include <cstring>

namespace ugc_renderer
{
void Texture2D::Initialize(
    ID3D12Device& device,
    ID3D12GraphicsCommandList& commandList,
    const std::uint32_t width,
    const std::uint32_t height,
    const DXGI_FORMAT format,
    const std::span<const std::byte> pixelData)
{
    D3D12_HEAP_PROPERTIES defaultHeapProperties = {};
    defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeapProperties.CreationNodeMask = 1;
    defaultHeapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(
        device.CreateCommittedResource(
            &defaultHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&resource_)),
        "ID3D12Device::CreateCommittedResource(texture)");

    UINT64 uploadBufferSize = 0;
    device.GetCopyableFootprints(&resourceDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

    D3D12_HEAP_PROPERTIES uploadHeapProperties = {};
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeapProperties.CreationNodeMask = 1;
    uploadHeapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadResourceDesc = {};
    uploadResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadResourceDesc.Alignment = 0;
    uploadResourceDesc.Width = uploadBufferSize;
    uploadResourceDesc.Height = 1;
    uploadResourceDesc.DepthOrArraySize = 1;
    uploadResourceDesc.MipLevels = 1;
    uploadResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadResourceDesc.SampleDesc.Count = 1;
    uploadResourceDesc.SampleDesc.Quality = 0;
    uploadResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(
        device.CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &uploadResourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&uploadResource_)),
        "ID3D12Device::CreateCommittedResource(texture upload)");

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    device.GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

    void* mappedData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    ThrowIfFailed(uploadResource_->Map(0, &readRange, &mappedData), "ID3D12Resource::Map");

    auto* destination = static_cast<std::byte*>(mappedData) + footprint.Offset;
    const std::size_t sourceRowPitch = pixelData.size_bytes() / height;
    for (UINT row = 0; row < numRows; ++row)
    {
        std::memcpy(
            destination + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
            pixelData.data() + static_cast<std::size_t>(row) * sourceRowPitch,
            std::min<std::size_t>(sourceRowPitch, static_cast<std::size_t>(rowSizeInBytes)));
    }
    uploadResource_->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION destinationLocation = {};
    destinationLocation.pResource = resource_.Get();
    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destinationLocation.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION sourceLocation = {};
    sourceLocation.pResource = uploadResource_.Get();
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sourceLocation.PlacedFootprint = footprint;

    commandList.CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList.ResourceBarrier(1, &barrier);
}

ID3D12Resource* Texture2D::GetResource() const noexcept
{
    return resource_.Get();
}

void Texture2D::ReleaseUploadResource() noexcept
{
    uploadResource_.Reset();
}
} // namespace ugc_renderer
