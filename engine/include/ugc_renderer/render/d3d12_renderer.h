#pragma once

#include <Windows.h>

#include "ugc_renderer/render/camera.h"
#include "ugc_renderer/render/material.h"
#include "ugc_renderer/render/render_graph_profile.h"
#include "ugc_renderer/render/render_graph.h"
#include "ugc_renderer/render/render_item.h"
#include "ugc_renderer/render/runtime_render_settings.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace ugc_renderer
{
class ConstantBuffer;
class DescriptorAllocation;
class DescriptorAllocator;
struct GltfDocument;
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
    [[nodiscard]] const RuntimeRenderSettings& GetRuntimeRenderSettings() const noexcept;
    [[nodiscard]] const std::vector<RuntimeMaterialInfo>& GetRuntimeMaterials() const noexcept;
    void SetRuntimeRenderSettings(const RuntimeRenderSettings& settings);
    bool UpdateRuntimeMaterial(std::uint32_t runtimeMaterialIndex, const MaterialDesc& desc);
    bool ReloadShaders();
    void RequestRenderGraphSnapshotExport() noexcept;
    void RequestScreenshotCapture() noexcept;
    [[nodiscard]] const RenderGraphProfileSnapshot& GetRenderGraphProfileSnapshot() const noexcept;
    [[nodiscard]] bool IsAutoShaderReloadEnabled() const noexcept;
    void SetAutoShaderReloadEnabled(bool enabled);
    [[nodiscard]] const std::string& GetShaderReloadStatus() const noexcept;

private:
    struct ScenePrimitiveAsset
    {
        std::unique_ptr<Mesh> mesh;
        std::uint32_t material = 0;
    };

    struct GraphPhysicalResource
    {
        RenderGraph::ResourceDesc desc = {};
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        DescriptorAllocation rtvAllocation = {};
        DescriptorAllocation srvAllocation = {};
        DescriptorAllocation dsvAllocation = {};
    };

    struct FrameRenderContext;
    struct ShaderSourceFile;

    void EnableDebugLayerIfAvailable();
    void CreateFactory();
    void CreateDevice();
    void CreateCommandObjects();
    void CreateGpuProfilerResources();
    void CreateSwapChain();
    void CreateDescriptorHeap();
    void CreateRenderTargets();
    void CreateFence();
    void CreatePipeline();
    void LoadSceneAsset();
    void CreateSceneGeometry();
    void CreateSceneConstants();
    void CreateTextureAssets();
    void CreateMaterials();
    void CreateRenderItems();
    void RebuildRenderItemMaterialBuckets();
    void UpdateSceneConstants();
    void UpdateCamera(float deltaTimeSeconds);
    void UpdatePostProcessDebugMode();
    void InitializeShaderHotReloadTracking();
    void RefreshShaderHotReloadWriteTimes();
    void UpdateShaderHotReload(float deltaTimeSeconds);
    void UpdateRenderItemConstants(
        RenderItem& renderItem,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        float elapsedSeconds);
    RenderGraph BuildFrameRenderGraph(FrameRenderContext& context);
    void RecordShadowDepthPass(const FrameRenderContext& context);
    void RecordMainColorBeginPass(const FrameRenderContext& context);
    void RecordSkyboxPass();
    void RecordOpaqueGeometryPass(FrameRenderContext& context);
    void RecordTransparentGeometryPass(FrameRenderContext& context);
    void RecordBloomPrefilterPass(const FrameRenderContext& context);
    void RecordBloomBlurHorizontalPass(const FrameRenderContext& context);
    void RecordBloomBlurVerticalPass(const FrameRenderContext& context);
    void RecordPostProcessPass(const FrameRenderContext& context);
    void RecordPresentTransitionPass(const FrameRenderContext& context);
    void BindCommonGraphicsState();
    void DrawRenderItem(
        RenderItem& renderItem,
        std::uint32_t& currentPipelineStateIndex,
        D3D12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle);
    void EnsureGraphPhysicalResources(const RenderGraph::CompileResult& compileResult);
    void ExportRenderGraphSnapshot(const RenderGraph& frameGraph, const RenderGraph::CompileResult& compiledGraph);
    void ExportScreenshotCapture(
        ID3D12Resource& readbackResource,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
        std::uint32_t width,
        std::uint32_t height);
    void UpdateRenderGraphCpuTimingSnapshot(
        const RenderGraph::CompileResult& compiledGraph,
        const std::vector<double>& passCpuTimesMs);
    void ReadbackRenderGraphGpuTimingSnapshot(
        const RenderGraph::CompileResult& compiledGraph,
        std::uint32_t profiledPassCount);
    void ReleaseGraphPhysicalResources() noexcept;
    std::uint64_t Signal();
    void WaitForFenceValue(std::uint64_t fenceValue);
    void ExecuteImmediateCommands();
    void UpdateViewport(std::uint32_t width, std::uint32_t height);
    void MoveToNextFrame();

    static constexpr std::uint32_t kFrameCount = 2;
    static constexpr std::uint32_t kPipelineStateCount = 4;
    static constexpr std::uint32_t kShadowMapSize = 2048;
    static constexpr std::uint32_t kMaxGpuProfiledPassCount = 64;
    static constexpr std::uint32_t kGpuTimestampQueryCount = kMaxGpuProfiledPassCount * 2;
    static constexpr std::uint64_t kGpuProfileSampleIntervalFrames = 60;
    static constexpr float kShaderReloadDebounceSeconds = 0.35f;

    Window& window_;

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> commandAllocators_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> renderTargets_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> gpuTimestampQueryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> gpuTimestampReadbackBuffer_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kPipelineStateCount> pipelineStates_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skyboxPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomPrefilterPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomBlurHorizontalPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomBlurVerticalPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> postProcessPipelineState_;
    std::unique_ptr<DescriptorAllocator> rtvAllocator_;
    std::unique_ptr<DescriptorAllocator> dsvAllocator_;
    std::unique_ptr<DescriptorAllocator> cbvAllocator_;
    std::unique_ptr<DescriptorAllocation> rtvAllocation_;
    std::unique_ptr<ConstantBuffer> sceneConstantBuffer_;
    DescriptorAllocation sceneCbvAllocation_ = {};
    std::unique_ptr<TextureManager> textureManager_;
    std::vector<GraphPhysicalResource> graphPhysicalResources_;
    std::unique_ptr<MaterialManager> materialManager_;
    std::unique_ptr<GltfDocument> sceneDocument_;
    std::vector<ScenePrimitiveAsset> scenePrimitiveAssets_;
    std::vector<std::vector<std::uint32_t>> sceneMeshPrimitiveAssetIndices_;
    std::vector<RenderItem> renderItems_;
    std::vector<std::uint32_t> opaqueRenderItemIndices_;
    std::vector<std::uint32_t> transparentRenderItemIndices_;
    std::vector<std::uint32_t> runtimeTextureIndices_;
    std::vector<std::uint32_t> runtimeMaterialIndices_;
    std::vector<RuntimeMaterialInfo> runtimeMaterials_;
    std::uint32_t environmentTextureIndex_ = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t iblDiffuseTextureIndex_ = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t iblSpecularTextureIndex_ = std::numeric_limits<std::uint32_t>::max();
    Camera camera_ = {};
    DirectX::XMFLOAT3 cameraTarget_ = {0.0f, 0.0f, 0.65f};
    float cameraOrbitYaw_ = 0.0f;
    float cameraOrbitPitch_ = 0.0f;
    float cameraDistance_ = 3.0f;
    RuntimeRenderSettings runtimeRenderSettings_ = {};
    bool renderGraphLogged_ = false;
    bool renderGraphProfilingLogged_ = false;
    bool renderGraphGpuProfilingLogged_ = false;
    bool renderGraphSnapshotExportRequested_ = false;
    bool screenshotCaptureRequested_ = false;
    std::uint64_t gpuTimestampFrequency_ = 0;
    std::uint64_t renderFrameCounter_ = 0;
    RenderGraphProfileSnapshot renderGraphProfileSnapshot_ = {};
    std::vector<ShaderSourceFile> shaderSourceFiles_;
    std::filesystem::path pendingShaderReloadPath_;
    std::string shaderReloadStatus_;
    float shaderReloadDebounceSeconds_ = 0.0f;
    bool autoShaderReloadEnabled_ = true;
    bool shaderReloadPending_ = false;

    std::array<std::uint64_t, kFrameCount> frameFenceValues_ = {};
    std::uint64_t nextFenceValue_ = 1;
    HANDLE fenceEvent_ = nullptr;
    std::uint32_t frameIndex_ = 0;
    D3D12_VIEWPORT viewport_ = {};
    D3D12_RECT scissorRect_ = {};
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();
};
} // namespace ugc_renderer
