#pragma once

#include <cstdint>
#include <stdexcept>

#include <d3d12.h>
#include <wrl/client.h>

namespace ugc_renderer
{
class DescriptorAllocation
{
public:
    DescriptorAllocation() = default;

    DescriptorAllocation(
        D3D12_CPU_DESCRIPTOR_HANDLE cpuStart,
        D3D12_GPU_DESCRIPTOR_HANDLE gpuStart,
        std::uint32_t descriptorSize,
        std::uint32_t count,
        bool shaderVisible) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(std::uint32_t offset = 0) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(std::uint32_t offset = 0) const;

private:
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart_ = {};
    std::uint32_t descriptorSize_ = 0;
    std::uint32_t count_ = 0;
    bool shaderVisible_ = false;
};

class DescriptorAllocator
{
public:
    void Initialize(
        ID3D12Device& device,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        std::uint32_t capacity,
        bool shaderVisible);

    DescriptorAllocation Allocate(std::uint32_t count = 1);

    ID3D12DescriptorHeap* GetHeap() const noexcept;
    std::uint32_t GetDescriptorSize() const noexcept;

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    std::uint32_t capacity_ = 0;
    std::uint32_t used_ = 0;
    std::uint32_t descriptorSize_ = 0;
    bool shaderVisible_ = false;
};
} // namespace ugc_renderer
