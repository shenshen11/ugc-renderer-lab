#include "ugc_renderer/render/constant_buffer.h"

#include "ugc_renderer/core/throw_if_failed.h"

#include <algorithm>
#include <cstring>

namespace ugc_renderer
{
ConstantBuffer::~ConstantBuffer()
{
    if (resource_ != nullptr && mappedData_ != nullptr)
    {
        resource_->Unmap(0, nullptr);
        mappedData_ = nullptr;
    }
}

void ConstantBuffer::Initialize(ID3D12Device& device, const std::uint32_t sizeInBytes)
{
    alignedSizeInBytes_ = AlignConstantBufferSize(sizeInBytes);

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = alignedSizeInBytes_;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(
        device.CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource_)),
        "ID3D12Device::CreateCommittedResource(constant buffer)");

    void* mappedData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    ThrowIfFailed(resource_->Map(0, &readRange, &mappedData), "ID3D12Resource::Map");
    mappedData_ = static_cast<std::byte*>(mappedData);
}

void ConstantBuffer::Update(const std::span<const std::byte> data)
{
    if (data.size_bytes() > alignedSizeInBytes_)
    {
        throw std::runtime_error("ConstantBuffer update exceeds allocated size.");
    }

    std::memcpy(mappedData_, data.data(), data.size_bytes());
    if (data.size_bytes() < alignedSizeInBytes_)
    {
        std::fill(mappedData_ + data.size_bytes(), mappedData_ + alignedSizeInBytes_, std::byte {0});
    }
}

ID3D12Resource* ConstantBuffer::GetResource() const noexcept
{
    return resource_.Get();
}

D3D12_GPU_VIRTUAL_ADDRESS ConstantBuffer::GetGpuVirtualAddress() const noexcept
{
    return resource_->GetGPUVirtualAddress();
}

std::uint32_t ConstantBuffer::GetAlignedSizeInBytes() const noexcept
{
    return alignedSizeInBytes_;
}

std::uint32_t ConstantBuffer::AlignConstantBufferSize(const std::uint32_t sizeInBytes) noexcept
{
    return (sizeInBytes + 255u) & ~255u;
}
} // namespace ugc_renderer
