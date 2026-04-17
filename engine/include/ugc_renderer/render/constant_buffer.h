#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <d3d12.h>
#include <wrl/client.h>

namespace ugc_renderer
{
class ConstantBuffer
{
public:
    ConstantBuffer() = default;
    ~ConstantBuffer();

    ConstantBuffer(const ConstantBuffer&) = delete;
    ConstantBuffer& operator=(const ConstantBuffer&) = delete;
    ConstantBuffer(ConstantBuffer&&) = delete;
    ConstantBuffer& operator=(ConstantBuffer&&) = delete;

    void Initialize(ID3D12Device& device, std::uint32_t sizeInBytes);
    void Update(std::span<const std::byte> data);

    ID3D12Resource* GetResource() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const noexcept;
    std::uint32_t GetAlignedSizeInBytes() const noexcept;

private:
    static std::uint32_t AlignConstantBufferSize(std::uint32_t sizeInBytes) noexcept;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    std::byte* mappedData_ = nullptr;
    std::uint32_t alignedSizeInBytes_ = 0;
};
} // namespace ugc_renderer
