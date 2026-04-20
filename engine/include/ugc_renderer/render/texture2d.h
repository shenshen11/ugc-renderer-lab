#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>

namespace ugc_renderer
{
class Texture2D
{
public:
    Texture2D() = default;

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&&) = default;
    Texture2D& operator=(Texture2D&&) = default;

    void Initialize(
        ID3D12Device& device,
        ID3D12GraphicsCommandList& commandList,
        std::uint32_t width,
        std::uint32_t height,
        DXGI_FORMAT format,
        std::span<const std::byte> pixelData);

    ID3D12Resource* GetResource() const noexcept;
    [[nodiscard]] std::uint32_t GetWidth() const noexcept;
    [[nodiscard]] std::uint32_t GetHeight() const noexcept;
    [[nodiscard]] DXGI_FORMAT GetFormat() const noexcept;
    void ReleaseUploadResource() noexcept;

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource_;
};
} // namespace ugc_renderer
