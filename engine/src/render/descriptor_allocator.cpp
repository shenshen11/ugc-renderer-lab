#include "ugc_renderer/render/descriptor_allocator.h"

#include "ugc_renderer/core/throw_if_failed.h"

namespace ugc_renderer
{
DescriptorAllocation::DescriptorAllocation(
    const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart,
    const D3D12_GPU_DESCRIPTOR_HANDLE gpuStart,
    const std::uint32_t descriptorSize,
    const std::uint32_t count,
    const bool shaderVisible) noexcept
    : cpuStart_(cpuStart)
    , gpuStart_(gpuStart)
    , descriptorSize_(descriptorSize)
    , count_(count)
    , shaderVisible_(shaderVisible)
{
}

bool DescriptorAllocation::IsValid() const noexcept
{
    return count_ != 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocation::GetCpuHandle(const std::uint32_t offset) const
{
    if (offset >= count_)
    {
        throw std::out_of_range("DescriptorAllocation CPU offset out of range.");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuStart_;
    handle.ptr += static_cast<SIZE_T>(offset) * static_cast<SIZE_T>(descriptorSize_);
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocation::GetGpuHandle(const std::uint32_t offset) const
{
    if (!shaderVisible_)
    {
        throw std::logic_error("Descriptor allocation is not shader visible.");
    }

    if (offset >= count_)
    {
        throw std::out_of_range("DescriptorAllocation GPU offset out of range.");
    }

    D3D12_GPU_DESCRIPTOR_HANDLE handle = gpuStart_;
    handle.ptr += static_cast<UINT64>(offset) * static_cast<UINT64>(descriptorSize_);
    return handle;
}

void DescriptorAllocator::Initialize(
    ID3D12Device& device,
    const D3D12_DESCRIPTOR_HEAP_TYPE type,
    const std::uint32_t capacity,
    const bool shaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = type;
    heapDesc.NumDescriptors = capacity;
    heapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heapDesc.NodeMask = 0;

    ThrowIfFailed(device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap_)), "ID3D12Device::CreateDescriptorHeap");

    capacity_ = capacity;
    used_ = 0;
    shaderVisible_ = shaderVisible;
    descriptorSize_ = device.GetDescriptorHandleIncrementSize(type);
}

DescriptorAllocation DescriptorAllocator::Allocate(const std::uint32_t count)
{
    if (used_ + count > capacity_)
    {
        throw std::runtime_error("DescriptorAllocator capacity exceeded.");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = heap_->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += static_cast<SIZE_T>(used_) * static_cast<SIZE_T>(descriptorSize_);

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};
    if (shaderVisible_)
    {
        gpuHandle = heap_->GetGPUDescriptorHandleForHeapStart();
        gpuHandle.ptr += static_cast<UINT64>(used_) * static_cast<UINT64>(descriptorSize_);
    }

    used_ += count;
    return DescriptorAllocation(cpuHandle, gpuHandle, descriptorSize_, count, shaderVisible_);
}

ID3D12DescriptorHeap* DescriptorAllocator::GetHeap() const noexcept
{
    return heap_.Get();
}

std::uint32_t DescriptorAllocator::GetDescriptorSize() const noexcept
{
    return descriptorSize_;
}
} // namespace ugc_renderer
