#include "ugc_renderer/render/mesh.h"

#include "ugc_renderer/render/gpu_buffer.h"

#include <memory>

namespace ugc_renderer
{
Mesh::Mesh()
    : vertexBuffer_(std::make_unique<GpuBuffer>())
    , indexBuffer_(std::make_unique<GpuBuffer>())
{
}

Mesh::~Mesh() = default;

void Mesh::Initialize(
    ID3D12Device& device,
    ID3D12GraphicsCommandList& commandList,
    const std::span<const std::byte> vertexData,
    const std::uint32_t vertexStride,
    const std::span<const std::byte> indexData,
    const DXGI_FORMAT indexFormat,
    const std::uint32_t indexCount)
{
    vertexBuffer_->InitializeVertexBuffer(device, commandList, vertexData, vertexStride);
    indexBuffer_->InitializeIndexBuffer(device, commandList, indexData, indexFormat);
    indexCount_ = indexCount;
}

const D3D12_VERTEX_BUFFER_VIEW& Mesh::GetVertexBufferView() const noexcept
{
    return vertexBuffer_->GetVertexBufferView();
}

const D3D12_INDEX_BUFFER_VIEW& Mesh::GetIndexBufferView() const noexcept
{
    return indexBuffer_->GetIndexBufferView();
}

std::uint32_t Mesh::GetIndexCount() const noexcept
{
    return indexCount_;
}

void Mesh::ReleaseUploadResources() noexcept
{
    vertexBuffer_->ReleaseUploadResource();
    indexBuffer_->ReleaseUploadResource();
}
} // namespace ugc_renderer
