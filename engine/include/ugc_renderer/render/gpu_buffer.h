#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <d3d12.h>
#include <wrl/client.h>

namespace ugc_renderer
{
class GpuBuffer
{
public:
    GpuBuffer() = default;

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&&) = default;
    GpuBuffer& operator=(GpuBuffer&&) = default;

    void InitializeVertexBuffer(
        ID3D12Device& device,
        ID3D12GraphicsCommandList& commandList,
        std::span<const std::byte> initialData,
        std::uint32_t strideInBytes);

    ID3D12Resource* GetResource() const noexcept;
    const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const noexcept;
    void ReleaseUploadResource() noexcept;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_ = {};
};
} // namespace ugc_renderer
