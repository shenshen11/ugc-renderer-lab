#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <d3d12.h>
#include <dxgiformat.h>

namespace ugc_renderer
{
class GpuBuffer;

class Mesh
{
public:
    Mesh();
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = delete;
    Mesh& operator=(Mesh&&) = delete;

    void Initialize(
        ID3D12Device& device,
        ID3D12GraphicsCommandList& commandList,
        std::span<const std::byte> vertexData,
        std::uint32_t vertexStride,
        std::span<const std::byte> indexData,
        DXGI_FORMAT indexFormat,
        std::uint32_t indexCount);

    const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const noexcept;
    const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const noexcept;
    std::uint32_t GetIndexCount() const noexcept;
    void ReleaseUploadResources() noexcept;

private:
    std::unique_ptr<GpuBuffer> vertexBuffer_;
    std::unique_ptr<GpuBuffer> indexBuffer_;
    std::uint32_t indexCount_ = 0;
};
} // namespace ugc_renderer
