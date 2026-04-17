#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>
#include <memory>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace ugc_renderer
{
class GpuBuffer;
class Window;

class D3D12Renderer
{
public:
    explicit D3D12Renderer(Window& window);
    ~D3D12Renderer();

    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;
    D3D12Renderer(D3D12Renderer&&) = delete;
    D3D12Renderer& operator=(D3D12Renderer&&) = delete;

    void Render();
    void Resize(std::uint32_t width, std::uint32_t height);
    void WaitForIdle();

private:
    void EnableDebugLayerIfAvailable();
    void CreateFactory();
    void CreateDevice();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateDescriptorHeap();
    void CreateRenderTargets();
    void CreateFence();
    void CreateTrianglePipeline();
    void CreateTriangleGeometry();
    std::uint64_t Signal();
    void WaitForFenceValue(std::uint64_t fenceValue);
    void ExecuteImmediateCommands();
    void UpdateViewport(std::uint32_t width, std::uint32_t height);
    void MoveToNextFrame();

    static constexpr std::uint32_t kFrameCount = 2;

    Window& window_;

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> commandAllocators_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> renderTargets_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    std::unique_ptr<GpuBuffer> vertexBuffer_;

    std::array<std::uint64_t, kFrameCount> fenceValues_ = {};
    HANDLE fenceEvent_ = nullptr;
    std::uint32_t frameIndex_ = 0;
    UINT rtvDescriptorSize_ = 0;
    D3D12_VIEWPORT viewport_ = {};
    D3D12_RECT scissorRect_ = {};
};
} // namespace ugc_renderer
