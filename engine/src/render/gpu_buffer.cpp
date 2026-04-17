#include "ugc_renderer/render/gpu_buffer.h"

#include "ugc_renderer/core/throw_if_failed.h"

#include <cstring>

namespace ugc_renderer
{
namespace
{
D3D12_HEAP_PROPERTIES CreateHeapProperties(const D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = type;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;
    return heapProperties;
}

D3D12_RESOURCE_DESC CreateBufferResourceDesc(const std::size_t sizeInBytes)
{
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    return resourceDesc;
}
} // namespace

void GpuBuffer::InitializeVertexBuffer(
    ID3D12Device& device,
    ID3D12GraphicsCommandList& commandList,
    const std::span<const std::byte> initialData,
    const std::uint32_t strideInBytes)
{
    const auto defaultHeapProperties = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const auto uploadHeapProperties = CreateHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const auto resourceDesc = CreateBufferResourceDesc(initialData.size_bytes());

    ThrowIfFailed(
        device.CreateCommittedResource(
            &defaultHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&resource_)),
        "ID3D12Device::CreateCommittedResource(default buffer)");

    ThrowIfFailed(
        device.CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&uploadResource_)),
        "ID3D12Device::CreateCommittedResource(upload buffer)");

    void* mappedData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    ThrowIfFailed(uploadResource_->Map(0, &readRange, &mappedData), "ID3D12Resource::Map");
    std::memcpy(mappedData, initialData.data(), initialData.size_bytes());
    uploadResource_->Unmap(0, nullptr);

    commandList.CopyBufferRegion(resource_.Get(), 0, uploadResource_.Get(), 0, initialData.size_bytes());

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList.ResourceBarrier(1, &barrier);

    vertexBufferView_.BufferLocation = resource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(initialData.size_bytes());
    vertexBufferView_.StrideInBytes = strideInBytes;
}

ID3D12Resource* GpuBuffer::GetResource() const noexcept
{
    return resource_.Get();
}

const D3D12_VERTEX_BUFFER_VIEW& GpuBuffer::GetVertexBufferView() const noexcept
{
    return vertexBufferView_;
}

void GpuBuffer::ReleaseUploadResource() noexcept
{
    uploadResource_.Reset();
}
} // namespace ugc_renderer
