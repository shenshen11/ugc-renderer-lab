#pragma once

#include <Windows.h>

#include "ugc_renderer/render/camera.h"
#include "ugc_renderer/render/render_item.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace ugc_renderer
{
class DescriptorAllocation;
class DescriptorAllocator;
class MaterialManager;
class Mesh;
class TextureManager;
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

    void Update(float deltaTimeSeconds);
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
    void CreateDepthStencil();
    void CreateFence();
    void CreatePipeline();
    void CreateSceneGeometry();
    void CreateTextureAssets();
    void CreateMaterials();
    void CreateRenderItems();
    void UpdateCamera(float deltaTimeSeconds);
    void UpdateRenderItemConstants(RenderItem& renderItem);
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
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> commandAllocators_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> renderTargets_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencil_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    std::unique_ptr<DescriptorAllocator> rtvAllocator_;
    std::unique_ptr<DescriptorAllocator> dsvAllocator_;
    std::unique_ptr<DescriptorAllocator> cbvAllocator_;
    std::unique_ptr<DescriptorAllocation> rtvAllocation_;
    std::unique_ptr<DescriptorAllocation> dsvAllocation_;
    std::unique_ptr<Mesh> mesh_;
    std::unique_ptr<TextureManager> textureManager_;
    std::unique_ptr<MaterialManager> materialManager_;
    std::vector<RenderItem> renderItems_;
    Camera camera_ = {};
    DirectX::XMFLOAT3 cameraTarget_ = {0.0f, 0.0f, 0.65f};
    float cameraOrbitYaw_ = 0.0f;
    float cameraOrbitPitch_ = 0.0f;
    float cameraDistance_ = 3.0f;

    std::array<std::uint64_t, kFrameCount> frameFenceValues_ = {};
    std::uint64_t nextFenceValue_ = 1;
    HANDLE fenceEvent_ = nullptr;
    std::uint32_t frameIndex_ = 0;
    D3D12_VIEWPORT viewport_ = {};
    D3D12_RECT scissorRect_ = {};
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();
};
} // namespace ugc_renderer
