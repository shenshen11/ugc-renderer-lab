#include "ugc_renderer/render/constant_buffer.h"
#include "ugc_renderer/render/descriptor_allocator.h"
#include "ugc_renderer/render/d3d12_renderer.h"

#include "ugc_renderer/asset/gltf_loader.h"
#include "ugc_renderer/asset/gltf_mesh_builder.h"
#include "ugc_renderer/asset/gltf_scene_builder.h"
#include "ugc_renderer/core/logger.h"
#include "ugc_renderer/core/throw_if_failed.h"
#include "ugc_renderer/platform/window.h"
#include "ugc_renderer/render/material_manager.h"
#include "ugc_renderer/render/mesh.h"
#include "ugc_renderer/render/render_graph.h"
#include "ugc_renderer/render/shader_compiler.h"
#include "ugc_renderer/render/texture_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <span>
#include <string>

#include <DirectXMath.h>

namespace ugc_renderer
{
using Microsoft::WRL::ComPtr;

namespace
{
std::filesystem::path GetAssetPath(const std::wstring_view relativePath)
{
    std::array<wchar_t, MAX_PATH> modulePath = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length == modulePath.size())
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "GetModuleFileNameW");
    }

    return std::filesystem::path(modulePath.data()).parent_path() / L"assets" / std::filesystem::path(relativePath);
}

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4X4 worldViewProjection;
    DirectX::XMFLOAT4X4 worldInverseTranspose;
};

struct SceneConstants
{
    DirectX::XMFLOAT4 cameraPositionAndEnvironmentIntensity = {0.0f, 0.0f, 0.0f, 0.55f};
    DirectX::XMFLOAT4 directionalLightDirectionAndIntensity = {0.35f, 0.8f, -0.45f, 4.75f};
    DirectX::XMFLOAT4 directionalLightColorAndExposure = {1.0f, 0.96f, 0.92f, 1.0f};
    DirectX::XMFLOAT4 cameraRightAndTanHalfFovX = {1.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 cameraUpAndTanHalfFovY = {0.0f, 1.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 cameraForward = {0.0f, 0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT4X4 lightViewProjection;
    DirectX::XMFLOAT4 shadowParams = {1.0f / 2048.0f, 1.0f / 2048.0f, 0.0035f, 0.0f};
};

enum class PipelineStateKind : std::uint32_t
{
    OpaqueCullBack = 0,
    OpaqueDoubleSided = 1,
    BlendCullBack = 2,
    BlendDoubleSided = 3,
};

MaterialAlphaMode ParseAlphaMode(const std::string& alphaMode)
{
    if (alphaMode == "MASK")
    {
        return MaterialAlphaMode::Mask;
    }

    if (alphaMode == "BLEND")
    {
        return MaterialAlphaMode::Blend;
    }

    return MaterialAlphaMode::Opaque;
}

bool IsBlendAlphaMode(const MaterialAlphaMode alphaMode)
{
    return alphaMode == MaterialAlphaMode::Blend;
}

std::uint32_t GetPipelineStateIndex(const MaterialDesc& material)
{
    const bool blendEnabled = IsBlendAlphaMode(material.alphaMode);
    const bool doubleSided = material.doubleSided;

    if (blendEnabled)
    {
        return static_cast<std::uint32_t>(
            doubleSided ? PipelineStateKind::BlendDoubleSided : PipelineStateKind::BlendCullBack);
    }

    return static_cast<std::uint32_t>(
        doubleSided ? PipelineStateKind::OpaqueDoubleSided : PipelineStateKind::OpaqueCullBack);
}
} // namespace

struct D3D12Renderer::FrameRenderContext
{
    D3D12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    D3D12_RESOURCE_BARRIER shadowMapToDepthWrite = {};
    D3D12_RESOURCE_BARRIER toRenderTarget = {};
    D3D12_VIEWPORT shadowViewport = {};
    D3D12_RECT shadowScissorRect = {};
    std::array<float, 4> clearColor = {0.07f, 0.12f, 0.18f, 1.0f};
    std::vector<std::uint32_t> transparentDrawOrder;
    std::uint32_t currentPipelineStateIndex = std::numeric_limits<std::uint32_t>::max();
};

D3D12Renderer::D3D12Renderer(Window& window)
    : window_(window)
{
    EnableDebugLayerIfAvailable();
    CreateFactory();
    CreateDevice();
    CreateCommandObjects();
    CreateSwapChain();
    CreateDescriptorHeap();
    CreateSceneConstants();
    CreateRenderTargets();
    CreateDepthStencil();
    CreateShadowMap();
    CreateFence();
    CreatePipeline();
    LoadSceneAsset();
    CreateSceneGeometry();
    CreateTextureAssets();
    CreateMaterials();
    CreateRenderItems();
    UpdateViewport(window_.GetClientWidth(), window_.GetClientHeight());
    Logger::Info("D3D12 renderer initialized.");
}

D3D12Renderer::~D3D12Renderer()
{
    WaitForIdle();

    if (fenceEvent_ != nullptr)
    {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void D3D12Renderer::Update(const float deltaTimeSeconds)
{
    UpdateCamera(deltaTimeSeconds);
}

void D3D12Renderer::Render()
{
    auto& commandAllocator = commandAllocators_[frameIndex_];
    ThrowIfFailed(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
    ThrowIfFailed(commandList_->Reset(commandAllocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset");
    UpdateSceneConstants();

    const float width = static_cast<float>(std::max(window_.GetClientWidth(), 1u));
    const float height = static_cast<float>(std::max(window_.GetClientHeight(), 1u));
    const float aspect = width / height;
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(
        DirectX::XMLoadFloat3(&camera_.position),
        DirectX::XMLoadFloat3(&camera_.target),
        DirectX::XMLoadFloat3(&camera_.up));
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        camera_.verticalFovRadians,
        aspect,
        camera_.nearPlane,
        camera_.farPlane);

    for (auto& renderItem : renderItems_)
    {
        UpdateRenderItemConstants(renderItem, view, projection, elapsedSeconds);
    }

    FrameRenderContext frameContext = {};
    frameContext.transparentDrawOrder = transparentRenderItemIndices_;
    std::stable_sort(
        frameContext.transparentDrawOrder.begin(),
        frameContext.transparentDrawOrder.end(),
        [&](const std::uint32_t leftIndex, const std::uint32_t rightIndex)
        {
            const auto& leftCenter = renderItems_[leftIndex].worldCenter;
            const auto& rightCenter = renderItems_[rightIndex].worldCenter;
            const float leftDeltaX = leftCenter.x - camera_.position.x;
            const float leftDeltaY = leftCenter.y - camera_.position.y;
            const float leftDeltaZ = leftCenter.z - camera_.position.z;
            const float rightDeltaX = rightCenter.x - camera_.position.x;
            const float rightDeltaY = rightCenter.y - camera_.position.y;
            const float rightDeltaZ = rightCenter.z - camera_.position.z;
            const float leftDistanceSquared =
                leftDeltaX * leftDeltaX + leftDeltaY * leftDeltaY + leftDeltaZ * leftDeltaZ;
            const float rightDistanceSquared =
                rightDeltaX * rightDeltaX + rightDeltaY * rightDeltaY + rightDeltaZ * rightDeltaZ;
            return leftDistanceSquared > rightDistanceSquared;
        });

    frameContext.shadowDsvHandle = shadowDsvAllocation_.GetCpuHandle();
    frameContext.rtvHandle = rtvAllocation_->GetCpuHandle(frameIndex_);
    frameContext.dsvHandle = dsvAllocation_->GetCpuHandle();
    frameContext.shadowMapToDepthWrite.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    frameContext.shadowMapToDepthWrite.Transition.pResource = shadowMap_.Get();
    frameContext.shadowMapToDepthWrite.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    frameContext.shadowMapToDepthWrite.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    frameContext.shadowMapToDepthWrite.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    frameContext.toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    frameContext.toRenderTarget.Transition.pResource = renderTargets_[frameIndex_].Get();
    frameContext.toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    frameContext.toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    frameContext.toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    frameContext.shadowViewport.TopLeftX = 0.0f;
    frameContext.shadowViewport.TopLeftY = 0.0f;
    frameContext.shadowViewport.Width = static_cast<float>(kShadowMapSize);
    frameContext.shadowViewport.Height = static_cast<float>(kShadowMapSize);
    frameContext.shadowViewport.MinDepth = 0.0f;
    frameContext.shadowViewport.MaxDepth = 1.0f;
    frameContext.shadowScissorRect.left = 0;
    frameContext.shadowScissorRect.top = 0;
    frameContext.shadowScissorRect.right = static_cast<LONG>(kShadowMapSize);
    frameContext.shadowScissorRect.bottom = static_cast<LONG>(kShadowMapSize);

    RenderGraph frameGraph = BuildFrameRenderGraph(frameContext);
    frameGraph.Execute();

    ThrowIfFailed(commandList_->Close(), "ID3D12GraphicsCommandList::Close");

    ID3D12CommandList* commandLists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, commandLists);

    ThrowIfFailed(swapChain_->Present(1, 0), "IDXGISwapChain::Present");
    MoveToNextFrame();
}

RenderGraph D3D12Renderer::BuildFrameRenderGraph(FrameRenderContext& context)
{
    RenderGraph frameGraph;
    frameGraph.AddPass(
        "ShadowDepth",
        [this, &context]()
        {
            RecordShadowDepthPass(context);
        });
    frameGraph.AddPass(
        "MainColorBegin",
        [this, &context]()
        {
            RecordMainColorBeginPass(context);
        });
    frameGraph.AddPass(
        "Skybox",
        [this]()
        {
            RecordSkyboxPass();
        });
    frameGraph.AddPass(
        "OpaqueGeometry",
        [this, &context]()
        {
            RecordOpaqueGeometryPass(context);
        });
    frameGraph.AddPass(
        "TransparentGeometry",
        [this, &context]()
        {
            RecordTransparentGeometryPass(context);
        });
    frameGraph.AddPass(
        "PresentTransition",
        [this, &context]()
        {
            RecordPresentTransitionPass(context);
        });
    return frameGraph;
}

void D3D12Renderer::RecordShadowDepthPass(const FrameRenderContext& context)
{
    BindCommonGraphicsState();
    commandList_->ResourceBarrier(1, &context.shadowMapToDepthWrite);
    commandList_->RSSetViewports(1, &context.shadowViewport);
    commandList_->RSSetScissorRects(1, &context.shadowScissorRect);
    commandList_->OMSetRenderTargets(0, nullptr, FALSE, &context.shadowDsvHandle);
    commandList_->ClearDepthStencilView(context.shadowDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList_->SetPipelineState(shadowPipelineState_.Get());

    for (const std::uint32_t renderItemIndex : opaqueRenderItemIndices_)
    {
        auto& renderItem = renderItems_[renderItemIndex];
        const auto& material = materialManager_->GetMaterial(renderItem.materialIndex);
        const std::uint32_t baseColorTextureIndex =
            textureManager_->ResolveTextureIndex(material.desc.textures.baseColor, DefaultTextureKind::White);
        commandList_->SetGraphicsRootDescriptorTable(0, sceneCbvAllocation_.GetGpuHandle());
        commandList_->SetGraphicsRootDescriptorTable(1, renderItem.objectCbvAllocation.GetGpuHandle());
        commandList_->SetGraphicsRootDescriptorTable(2, material.cbvAllocation.GetGpuHandle());
        commandList_->SetGraphicsRootDescriptorTable(
            3,
            textureManager_->GetSrvAllocation(baseColorTextureIndex).GetGpuHandle());
        const auto& vertexBufferView = renderItem.mesh->GetVertexBufferView();
        const auto& indexBufferView = renderItem.mesh->GetIndexBufferView();
        commandList_->IASetVertexBuffers(0, 1, &vertexBufferView);
        commandList_->IASetIndexBuffer(&indexBufferView);
        commandList_->DrawIndexedInstanced(renderItem.mesh->GetIndexCount(), 1, 0, 0, 0);
    }

    D3D12_RESOURCE_BARRIER shadowMapToShaderResource = context.shadowMapToDepthWrite;
    shadowMapToShaderResource.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    shadowMapToShaderResource.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList_->ResourceBarrier(1, &shadowMapToShaderResource);
}

void D3D12Renderer::RecordMainColorBeginPass(const FrameRenderContext& context)
{
    commandList_->ResourceBarrier(1, &context.toRenderTarget);
    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);
    commandList_->OMSetRenderTargets(1, &context.rtvHandle, FALSE, &context.dsvHandle);
    commandList_->ClearRenderTargetView(context.rtvHandle, context.clearColor.data(), 0, nullptr);
    commandList_->ClearDepthStencilView(context.dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    BindCommonGraphicsState();
}

void D3D12Renderer::RecordSkyboxPass()
{
    commandList_->SetPipelineState(skyboxPipelineState_.Get());
    commandList_->SetGraphicsRootDescriptorTable(0, sceneCbvAllocation_.GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(
        8,
        textureManager_
            ->GetSrvAllocation(textureManager_->ResolveTextureIndex(environmentTextureIndex_, DefaultTextureKind::Black))
            .GetGpuHandle());
    commandList_->DrawInstanced(3, 1, 0, 0);
}

void D3D12Renderer::RecordOpaqueGeometryPass(FrameRenderContext& context)
{
    context.currentPipelineStateIndex = std::numeric_limits<std::uint32_t>::max();
    for (const std::uint32_t renderItemIndex : opaqueRenderItemIndices_)
    {
        DrawRenderItem(renderItems_[renderItemIndex], context.currentPipelineStateIndex);
    }
}

void D3D12Renderer::RecordTransparentGeometryPass(FrameRenderContext& context)
{
    for (const std::uint32_t renderItemIndex : context.transparentDrawOrder)
    {
        DrawRenderItem(renderItems_[renderItemIndex], context.currentPipelineStateIndex);
    }
}

void D3D12Renderer::RecordPresentTransitionPass(const FrameRenderContext& context)
{
    D3D12_RESOURCE_BARRIER toPresent = context.toRenderTarget;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList_->ResourceBarrier(1, &toPresent);
}

void D3D12Renderer::BindCommonGraphicsState()
{
    ID3D12DescriptorHeap* descriptorHeaps[] = {cbvAllocator_->GetHeap()};
    commandList_->SetDescriptorHeaps(static_cast<UINT>(std::size(descriptorHeaps)), descriptorHeaps);
    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void D3D12Renderer::DrawRenderItem(RenderItem& renderItem, std::uint32_t& currentPipelineStateIndex)
{
    const auto& material = materialManager_->GetMaterial(renderItem.materialIndex);
    const std::uint32_t pipelineStateIndex = GetPipelineStateIndex(material.desc);
    if (pipelineStateIndex != currentPipelineStateIndex)
    {
        commandList_->SetPipelineState(pipelineStates_[pipelineStateIndex].Get());
        currentPipelineStateIndex = pipelineStateIndex;
    }

    const std::uint32_t baseColorTextureIndex =
        textureManager_->ResolveTextureIndex(material.desc.textures.baseColor, DefaultTextureKind::White);
    const std::uint32_t normalTextureIndex =
        textureManager_->ResolveTextureIndex(material.desc.textures.normal, DefaultTextureKind::FlatNormal);
    const std::uint32_t metallicRoughnessTextureIndex =
        textureManager_->ResolveTextureIndex(material.desc.textures.metallicRoughness, DefaultTextureKind::White);
    const std::uint32_t occlusionTextureIndex =
        textureManager_->ResolveTextureIndex(material.desc.textures.occlusion, DefaultTextureKind::White);
    const std::uint32_t emissiveTextureIndex =
        textureManager_->ResolveTextureIndex(material.desc.textures.emissive, DefaultTextureKind::White);

    commandList_->SetGraphicsRootDescriptorTable(0, sceneCbvAllocation_.GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(
        1,
        renderItem.objectCbvAllocation.GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(
        2,
        material.cbvAllocation.GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(
        3,
        textureManager_->GetSrvAllocation(baseColorTextureIndex).GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(
        4,
        textureManager_->GetSrvAllocation(normalTextureIndex).GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(
        5,
        textureManager_->GetSrvAllocation(metallicRoughnessTextureIndex).GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(
        6,
        textureManager_->GetSrvAllocation(occlusionTextureIndex).GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(
        7,
        textureManager_->GetSrvAllocation(emissiveTextureIndex).GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(
        8,
        textureManager_
            ->GetSrvAllocation(textureManager_->ResolveTextureIndex(environmentTextureIndex_, DefaultTextureKind::Black))
            .GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(9, shadowSrvAllocation_.GetGpuHandle());
    const auto& vertexBufferView = renderItem.mesh->GetVertexBufferView();
    const auto& indexBufferView = renderItem.mesh->GetIndexBufferView();
    commandList_->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList_->IASetIndexBuffer(&indexBufferView);
    commandList_->DrawIndexedInstanced(renderItem.mesh->GetIndexCount(), 1, 0, 0, 0);
}

void D3D12Renderer::Resize(const std::uint32_t width, const std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return;
    }

    WaitForIdle();

    for (auto& renderTarget : renderTargets_)
    {
        renderTarget.Reset();
    }
    depthStencil_.Reset();

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    ThrowIfFailed(swapChain_->GetDesc(&swapChainDesc), "IDXGISwapChain::GetDesc");
    ThrowIfFailed(
        swapChain_->ResizeBuffers(
            kFrameCount,
            width,
            height,
            swapChainDesc.BufferDesc.Format,
            swapChainDesc.Flags),
        "IDXGISwapChain::ResizeBuffers");

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    frameFenceValues_.fill(fence_->GetCompletedValue());
    CreateRenderTargets();
    CreateDepthStencil();
    UpdateViewport(width, height);
}

void D3D12Renderer::WaitForIdle()
{
    if (commandQueue_ == nullptr || fence_ == nullptr || fenceEvent_ == nullptr)
    {
        return;
    }

    WaitForFenceValue(Signal());
}

void D3D12Renderer::EnableDebugLayerIfAvailable()
{
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        Logger::Info("Enabled D3D12 debug layer.");
    }
#endif
}

void D3D12Renderer::CreateFactory()
{
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    HRESULT result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));

#if defined(_DEBUG)
    if (FAILED(result) && factoryFlags == DXGI_CREATE_FACTORY_DEBUG)
    {
        Logger::Info("DXGI debug factory creation failed; retrying without DXGI_CREATE_FACTORY_DEBUG.");
        result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
    }
#endif

    ThrowIfFailed(result, "CreateDXGIFactory2");
}

void D3D12Renderer::CreateDevice()
{
    ComPtr<IDXGIAdapter1> adapter;

    for (UINT adapterIndex = 0; factory_->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND; ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 adapterDesc = {};
        ThrowIfFailed(adapter->GetDesc1(&adapterDesc), "IDXGIAdapter::GetDesc1");

        if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            adapter.Reset();
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_))))
        {
            Logger::Info("Created D3D12 device on hardware adapter.");
            return;
        }

        adapter.Reset();
    }

    ThrowIfFailed(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "IDXGIFactory4::EnumWarpAdapter");
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)), "D3D12CreateDevice");
    Logger::Info("Created D3D12 device on WARP adapter.");
}

void D3D12Renderer::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    ThrowIfFailed(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_)), "ID3D12Device::CreateCommandQueue");

    for (auto& allocator : commandAllocators_)
    {
        ThrowIfFailed(
            device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
            "ID3D12Device::CreateCommandAllocator");
    }

    ThrowIfFailed(
        device_->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocators_[0].Get(),
            nullptr,
            IID_PPV_ARGS(&commandList_)),
        "ID3D12Device::CreateCommandList");

    ThrowIfFailed(commandList_->Close(), "ID3D12GraphicsCommandList::Close");
}

void D3D12Renderer::CreateSwapChain()
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = window_.GetClientWidth();
    swapChainDesc.Height = window_.GetClientHeight();
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = kFrameCount;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(
        factory_->CreateSwapChainForHwnd(
            commandQueue_.Get(),
            window_.GetNativeHandle(),
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain),
        "IDXGIFactory4::CreateSwapChainForHwnd");

    ThrowIfFailed(factory_->MakeWindowAssociation(window_.GetNativeHandle(), DXGI_MWA_NO_ALT_ENTER), "IDXGIFactory4::MakeWindowAssociation");
    ThrowIfFailed(swapChain.As(&swapChain_), "IDXGISwapChain1::As");

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void D3D12Renderer::CreateDescriptorHeap()
{
    rtvAllocator_ = std::make_unique<DescriptorAllocator>();
    rtvAllocator_->Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kFrameCount, false);
    rtvAllocation_ = std::make_unique<DescriptorAllocation>(rtvAllocator_->Allocate(kFrameCount));

    dsvAllocator_ = std::make_unique<DescriptorAllocator>();
    dsvAllocator_->Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);
    dsvAllocation_ = std::make_unique<DescriptorAllocation>(dsvAllocator_->Allocate(1));

    cbvAllocator_ = std::make_unique<DescriptorAllocator>();
    cbvAllocator_->Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
}

void D3D12Renderer::CreateSceneConstants()
{
    sceneConstantBuffer_ = std::make_unique<ConstantBuffer>();
    sceneConstantBuffer_->Initialize(*device_.Get(), sizeof(SceneConstants));
    sceneCbvAllocation_ = cbvAllocator_->Allocate(1);

    D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc = {};
    constantBufferViewDesc.BufferLocation = sceneConstantBuffer_->GetGpuVirtualAddress();
    constantBufferViewDesc.SizeInBytes = sceneConstantBuffer_->GetAlignedSizeInBytes();
    device_->CreateConstantBufferView(&constantBufferViewDesc, sceneCbvAllocation_.GetCpuHandle());

    UpdateSceneConstants();
}

void D3D12Renderer::CreateRenderTargets()
{
    for (std::uint32_t index = 0; index < kFrameCount; ++index)
    {
        ThrowIfFailed(swapChain_->GetBuffer(index, IID_PPV_ARGS(&renderTargets_[index])), "IDXGISwapChain::GetBuffer");
        device_->CreateRenderTargetView(renderTargets_[index].Get(), nullptr, rtvAllocation_->GetCpuHandle(index));
    }
}

void D3D12Renderer::CreateDepthStencil()
{
    const std::uint32_t width = std::max(window_.GetClientWidth(), 1u);
    const std::uint32_t height = std::max(window_.GetClientHeight(), 1u);

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_D32_FLOAT;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    ThrowIfFailed(
        device_->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(&depthStencil_)),
        "ID3D12Device::CreateCommittedResource(depth stencil)");

    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = {};
    depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;

    device_->CreateDepthStencilView(depthStencil_.Get(), &depthStencilViewDesc, dsvAllocation_->GetCpuHandle());
}

void D3D12Renderer::CreateShadowMap()
{
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = kShadowMapSize;
    resourceDesc.Height = kShadowMapSize;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    ThrowIfFailed(
        device_->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue,
            IID_PPV_ARGS(&shadowMap_)),
        "ID3D12Device::CreateCommittedResource(shadow map)");

    shadowDsvAllocation_ = dsvAllocator_->Allocate(1);
    shadowSrvAllocation_ = cbvAllocator_->Allocate(1);

    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = {};
    depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;
    device_->CreateDepthStencilView(shadowMap_.Get(), &depthStencilViewDesc, shadowDsvAllocation_.GetCpuHandle());

    D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
    shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shaderResourceViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
    shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
    shaderResourceViewDesc.Texture2D.MipLevels = 1;
    shaderResourceViewDesc.Texture2D.PlaneSlice = 0;
    shaderResourceViewDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->CreateShaderResourceView(shadowMap_.Get(), &shaderResourceViewDesc, shadowSrvAllocation_.GetCpuHandle());
}

void D3D12Renderer::CreateFence()
{
    ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "ID3D12Device::CreateFence");
    frameFenceValues_.fill(0);
    nextFenceValue_ = 1;

    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent_ == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "CreateEventW");
    }
}

void D3D12Renderer::CreatePipeline()
{
    const auto materialShaderPath = ShaderCompiler::ResolveShaderPath(L"triangle.hlsl");
    const auto vertexShader = ShaderCompiler::CompileFromFile(materialShaderPath, "VSMain", "vs_5_0");
    const auto pixelShader = ShaderCompiler::CompileFromFile(materialShaderPath, "PSMain", "ps_5_0");
    const auto skyboxShaderPath = ShaderCompiler::ResolveShaderPath(L"skybox.hlsl");
    const auto skyboxVertexShader = ShaderCompiler::CompileFromFile(skyboxShaderPath, "VSMain", "vs_5_0");
    const auto skyboxPixelShader = ShaderCompiler::CompileFromFile(skyboxShaderPath, "PSMain", "ps_5_0");
    const auto shadowShaderPath = ShaderCompiler::ResolveShaderPath(L"shadow_depth.hlsl");
    const auto shadowVertexShader = ShaderCompiler::CompileFromFile(shadowShaderPath, "VSMain", "vs_5_0");
    const auto shadowPixelShader = ShaderCompiler::CompileFromFile(shadowShaderPath, "PSMain", "ps_5_0");

    D3D12_DESCRIPTOR_RANGE objectCbvDescriptorRange = {};
    objectCbvDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    objectCbvDescriptorRange.NumDescriptors = 1;
    objectCbvDescriptorRange.BaseShaderRegister = 1;
    objectCbvDescriptorRange.RegisterSpace = 0;
    objectCbvDescriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE materialCbvDescriptorRange = {};
    materialCbvDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    materialCbvDescriptorRange.NumDescriptors = 1;
    materialCbvDescriptorRange.BaseShaderRegister = 2;
    materialCbvDescriptorRange.RegisterSpace = 0;
    materialCbvDescriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE srvDescriptorRange = {};
    srvDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvDescriptorRange.NumDescriptors = 1;
    srvDescriptorRange.BaseShaderRegister = 0;
    srvDescriptorRange.RegisterSpace = 0;
    srvDescriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_DESCRIPTOR_RANGE, 7> textureRanges = {};
    for (std::uint32_t textureSlot = 0; textureSlot < textureRanges.size(); ++textureSlot)
    {
        textureRanges[textureSlot] = srvDescriptorRange;
        textureRanges[textureSlot].BaseShaderRegister = textureSlot;
    }

    std::array<D3D12_ROOT_PARAMETER, 10> rootParameters = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    D3D12_DESCRIPTOR_RANGE sceneCbvDescriptorRange = {};
    sceneCbvDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    sceneCbvDescriptorRange.NumDescriptors = 1;
    sceneCbvDescriptorRange.BaseShaderRegister = 0;
    sceneCbvDescriptorRange.RegisterSpace = 0;
    sceneCbvDescriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rootParameters[0].DescriptorTable.pDescriptorRanges = &sceneCbvDescriptorRange;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &objectCbvDescriptorRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].DescriptorTable.pDescriptorRanges = &materialCbvDescriptorRange;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    for (std::uint32_t textureSlot = 0; textureSlot < textureRanges.size(); ++textureSlot)
    {
        rootParameters[textureSlot + 3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[textureSlot + 3].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[textureSlot + 3].DescriptorTable.pDescriptorRanges = &textureRanges[textureSlot];
        rootParameters[textureSlot + 3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    std::array<D3D12_STATIC_SAMPLER_DESC, 2> staticSamplers = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].MipLODBias = 0.0f;
    staticSamplers[0].MaxAnisotropy = 1;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[0].MinLOD = 0.0f;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].RegisterSpace = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].MipLODBias = 0.0f;
    staticSamplers[1].MaxAnisotropy = 1;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[1].MinLOD = 0.0f;
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = 1;
    staticSamplers[1].RegisterSpace = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = static_cast<UINT>(rootParameters.size());
    rootSignatureDesc.pParameters = rootParameters.data();
    rootSignatureDesc.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
    rootSignatureDesc.pStaticSamplers = staticSamplers.data();
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT result = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSignature,
        &errorBlob);

    if (FAILED(result))
    {
        if (errorBlob != nullptr)
        {
            Logger::Error(std::string(
                static_cast<const char*>(errorBlob->GetBufferPointer()),
                errorBlob->GetBufferSize()));
        }

        ThrowIfFailed(result, "D3D12SerializeRootSignature");
    }

    ThrowIfFailed(
        device_->CreateRootSignature(
            0,
            serializedRootSignature->GetBufferPointer(),
            serializedRootSignature->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature_)),
        "ID3D12Device::CreateRootSignature");

    constexpr D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    auto createBlendState = [](const bool alphaBlendEnabled)
    {
        D3D12_BLEND_DESC blendState = {};
        blendState.AlphaToCoverageEnable = FALSE;
        blendState.IndependentBlendEnable = FALSE;

        D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {
            FALSE,
            FALSE,
            D3D12_BLEND_ONE,
            D3D12_BLEND_ZERO,
            D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE,
            D3D12_BLEND_ZERO,
            D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL};

        if (alphaBlendEnabled)
        {
            renderTargetBlendDesc.BlendEnable = TRUE;
            renderTargetBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            renderTargetBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            renderTargetBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
            renderTargetBlendDesc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        }

        for (auto& renderTarget : blendState.RenderTarget)
        {
            renderTarget = renderTargetBlendDesc;
        }

        return blendState;
    };

    auto createRasterizerState = [](const bool doubleSided)
    {
        D3D12_RASTERIZER_DESC rasterizerState = {};
        rasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizerState.CullMode = doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
        rasterizerState.FrontCounterClockwise = TRUE;
        rasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterizerState.DepthClipEnable = TRUE;
        rasterizerState.MultisampleEnable = FALSE;
        rasterizerState.AntialiasedLineEnable = FALSE;
        rasterizerState.ForcedSampleCount = 0;
        rasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        return rasterizerState;
    };

    auto createDepthStencilState = [](const bool alphaBlendEnabled)
    {
        D3D12_DEPTH_STENCIL_DESC depthStencilState = {};
        depthStencilState.DepthEnable = TRUE;
        depthStencilState.DepthWriteMask =
            alphaBlendEnabled ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
        depthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        depthStencilState.StencilEnable = FALSE;
        return depthStencilState;
    };

    auto createPipelineState = [&](const PipelineStateKind pipelineStateKind,
                                   const bool alphaBlendEnabled,
                                   const bool doubleSided)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};
        pipelineStateDesc.pRootSignature = rootSignature_.Get();
        pipelineStateDesc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        pipelineStateDesc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
        pipelineStateDesc.BlendState = createBlendState(alphaBlendEnabled);
        pipelineStateDesc.SampleMask = UINT_MAX;
        pipelineStateDesc.RasterizerState = createRasterizerState(doubleSided);
        pipelineStateDesc.DepthStencilState = createDepthStencilState(alphaBlendEnabled);
        pipelineStateDesc.InputLayout = {inputElements, static_cast<UINT>(std::size(inputElements))};
        pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineStateDesc.NumRenderTargets = 1;
        pipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pipelineStateDesc.SampleDesc.Count = 1;

        ThrowIfFailed(
            device_->CreateGraphicsPipelineState(
                &pipelineStateDesc,
                IID_PPV_ARGS(&pipelineStates_[static_cast<std::uint32_t>(pipelineStateKind)])),
            "ID3D12Device::CreateGraphicsPipelineState");
    };

    createPipelineState(PipelineStateKind::OpaqueCullBack, false, false);
    createPipelineState(PipelineStateKind::OpaqueDoubleSided, false, true);
    createPipelineState(PipelineStateKind::BlendCullBack, true, false);
    createPipelineState(PipelineStateKind::BlendDoubleSided, true, true);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skyboxPipelineStateDesc = {};
    skyboxPipelineStateDesc.pRootSignature = rootSignature_.Get();
    skyboxPipelineStateDesc.VS = {skyboxVertexShader->GetBufferPointer(), skyboxVertexShader->GetBufferSize()};
    skyboxPipelineStateDesc.PS = {skyboxPixelShader->GetBufferPointer(), skyboxPixelShader->GetBufferSize()};
    skyboxPipelineStateDesc.BlendState = createBlendState(false);
    skyboxPipelineStateDesc.SampleMask = UINT_MAX;
    skyboxPipelineStateDesc.RasterizerState = createRasterizerState(true);
    skyboxPipelineStateDesc.DepthStencilState.DepthEnable = FALSE;
    skyboxPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    skyboxPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    skyboxPipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
    skyboxPipelineStateDesc.InputLayout = {nullptr, 0};
    skyboxPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    skyboxPipelineStateDesc.NumRenderTargets = 1;
    skyboxPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    skyboxPipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    skyboxPipelineStateDesc.SampleDesc.Count = 1;

    ThrowIfFailed(
        device_->CreateGraphicsPipelineState(&skyboxPipelineStateDesc, IID_PPV_ARGS(&skyboxPipelineState_)),
        "ID3D12Device::CreateGraphicsPipelineState(skybox)");

    D3D12_RASTERIZER_DESC shadowRasterizerState = createRasterizerState(true);
    shadowRasterizerState.DepthBias = 1000;
    shadowRasterizerState.SlopeScaledDepthBias = 1.5f;
    shadowRasterizerState.DepthBiasClamp = 0.01f;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPipelineStateDesc = {};
    shadowPipelineStateDesc.pRootSignature = rootSignature_.Get();
    shadowPipelineStateDesc.VS = {shadowVertexShader->GetBufferPointer(), shadowVertexShader->GetBufferSize()};
    shadowPipelineStateDesc.PS = {shadowPixelShader->GetBufferPointer(), shadowPixelShader->GetBufferSize()};
    shadowPipelineStateDesc.BlendState = createBlendState(false);
    shadowPipelineStateDesc.SampleMask = UINT_MAX;
    shadowPipelineStateDesc.RasterizerState = shadowRasterizerState;
    shadowPipelineStateDesc.DepthStencilState.DepthEnable = TRUE;
    shadowPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadowPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadowPipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
    shadowPipelineStateDesc.InputLayout = {inputElements, static_cast<UINT>(std::size(inputElements))};
    shadowPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowPipelineStateDesc.NumRenderTargets = 0;
    shadowPipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowPipelineStateDesc.SampleDesc.Count = 1;

    ThrowIfFailed(
        device_->CreateGraphicsPipelineState(&shadowPipelineStateDesc, IID_PPV_ARGS(&shadowPipelineState_)),
        "ID3D12Device::CreateGraphicsPipelineState(shadow)");
}

void D3D12Renderer::LoadSceneAsset()
{
    sceneDocument_ = std::make_unique<GltfDocument>(
        GltfLoader::LoadFromFile(GetAssetPath(L"gltf/sample_scene/sample_scene.gltf")));
}

void D3D12Renderer::CreateSceneGeometry()
{
    auto& commandAllocator = commandAllocators_[frameIndex_];
    ThrowIfFailed(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
    ThrowIfFailed(commandList_->Reset(commandAllocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset");

    scenePrimitiveAssets_.clear();
    sceneMeshPrimitiveAssetIndices_.clear();
    sceneMeshPrimitiveAssetIndices_.resize(sceneDocument_->meshes.size());

    for (std::uint32_t meshIndex = 0; meshIndex < sceneDocument_->meshes.size(); ++meshIndex)
    {
        const auto& meshDefinition = sceneDocument_->meshes[meshIndex];
        sceneMeshPrimitiveAssetIndices_[meshIndex].reserve(meshDefinition.primitives.size());

        for (std::uint32_t primitiveIndex = 0; primitiveIndex < meshDefinition.primitives.size(); ++primitiveIndex)
        {
            const GltfRuntimeMesh runtimeMesh = GltfMeshBuilder::BuildPrimitive(*sceneDocument_, meshIndex, primitiveIndex);

            ScenePrimitiveAsset primitiveAsset = {};
            primitiveAsset.material = runtimeMesh.material;
            primitiveAsset.mesh = std::make_unique<Mesh>();
            primitiveAsset.mesh->Initialize(
                *device_.Get(),
                *commandList_.Get(),
                std::as_bytes(std::span(runtimeMesh.vertices)),
                sizeof(GltfRuntimeVertex),
                std::as_bytes(std::span(runtimeMesh.indices)),
                DXGI_FORMAT_R16_UINT,
                static_cast<std::uint32_t>(runtimeMesh.indices.size()));

            const std::uint32_t primitiveAssetIndex = static_cast<std::uint32_t>(scenePrimitiveAssets_.size());
            sceneMeshPrimitiveAssetIndices_[meshIndex].push_back(primitiveAssetIndex);
            scenePrimitiveAssets_.push_back(std::move(primitiveAsset));
        }
    }

    ExecuteImmediateCommands();
    for (auto& primitiveAsset : scenePrimitiveAssets_)
    {
        primitiveAsset.mesh->ReleaseUploadResources();
    }
}

void D3D12Renderer::CreateTextureAssets()
{
    textureManager_ = std::make_unique<TextureManager>();
    textureManager_->Initialize(*device_.Get(), *cbvAllocator_);
    environmentTextureIndex_ = kInvalidTextureIndex;

    auto loadTexture = [&](const std::filesystem::path& path)
    {
        auto& commandAllocator = commandAllocators_[frameIndex_];
        ThrowIfFailed(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
        ThrowIfFailed(commandList_->Reset(commandAllocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset");
        const std::uint32_t index = textureManager_->LoadFromFile(*commandList_.Get(), path);
        ExecuteImmediateCommands();
        textureManager_->ReleaseUploadResources();
        return index;
    };

    {
        auto& commandAllocator = commandAllocators_[frameIndex_];
        ThrowIfFailed(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
        ThrowIfFailed(commandList_->Reset(commandAllocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset");
        textureManager_->CreateDefaultTextures(*commandList_.Get());
        ExecuteImmediateCommands();
        textureManager_->ReleaseUploadResources();
    }

    runtimeTextureIndices_.assign(sceneDocument_->textures.size(), kInvalidTextureIndex);
    for (std::uint32_t textureIndex = 0; textureIndex < sceneDocument_->textures.size(); ++textureIndex)
    {
        const GltfTexture& texture = sceneDocument_->textures[textureIndex];
        if (texture.source == kInvalidGltfIndex || texture.source >= sceneDocument_->images.size())
        {
            continue;
        }

        const GltfImage& image = sceneDocument_->images[texture.source];
        if (image.isDataUri || image.bufferView != kInvalidGltfIndex || image.resolvedPath.empty())
        {
            continue;
        }

        runtimeTextureIndices_[textureIndex] = loadTexture(image.resolvedPath);
    }

    environmentTextureIndex_ = loadTexture(GetAssetPath(L"textures/environment_panorama.png"));
}

void D3D12Renderer::CreateMaterials()
{
    materialManager_ = std::make_unique<MaterialManager>();
    materialManager_->Initialize(*device_.Get(), *cbvAllocator_);

    auto resolveTextureReference = [&](const GltfTextureReference& textureReference)
    {
        if (textureReference.texture == kInvalidGltfIndex || textureReference.texture >= runtimeTextureIndices_.size())
        {
            return kInvalidTextureIndex;
        }

        return runtimeTextureIndices_[textureReference.texture];
    };

    runtimeMaterialIndices_.clear();
    runtimeMaterialIndices_.reserve(std::max<std::size_t>(sceneDocument_->materials.size(), 1));

    for (const GltfMaterial& gltfMaterial : sceneDocument_->materials)
    {
        MaterialDesc material = {};
        material.alphaMode = ParseAlphaMode(gltfMaterial.alphaMode);
        material.doubleSided = gltfMaterial.doubleSided;
        material.constants.baseColorFactor = {
            gltfMaterial.baseColorFactor[0],
            gltfMaterial.baseColorFactor[1],
            gltfMaterial.baseColorFactor[2],
            gltfMaterial.baseColorFactor[3]};
        material.constants.emissiveFactorAndMetallic = {
            gltfMaterial.emissiveFactor[0],
            gltfMaterial.emissiveFactor[1],
            gltfMaterial.emissiveFactor[2],
            gltfMaterial.metallicFactor};
        material.constants.roughnessUvScaleAlphaCutoff = {
            gltfMaterial.roughnessFactor,
            1.0f,
            1.0f,
            gltfMaterial.alphaCutoff};
        material.constants.textureControls = {
            gltfMaterial.normalTexture.scale,
            gltfMaterial.occlusionTexture.strength,
            static_cast<float>(static_cast<std::uint32_t>(material.alphaMode)),
            material.doubleSided ? 1.0f : 0.0f};
        material.textures.baseColor = resolveTextureReference(gltfMaterial.baseColorTexture);
        material.textures.normal = resolveTextureReference(gltfMaterial.normalTexture);
        material.textures.metallicRoughness = resolveTextureReference(gltfMaterial.metallicRoughnessTexture);
        material.textures.occlusion = resolveTextureReference(gltfMaterial.occlusionTexture);
        material.textures.emissive = resolveTextureReference(gltfMaterial.emissiveTexture);

        runtimeMaterialIndices_.push_back(materialManager_->CreateMaterial(material));
    }

    if (runtimeMaterialIndices_.empty())
    {
        MaterialDesc defaultMaterial = {};
        runtimeMaterialIndices_.push_back(materialManager_->CreateMaterial(defaultMaterial));
    }
}

void D3D12Renderer::CreateRenderItems()
{
    const auto sceneInstances = GltfSceneBuilder::BuildMeshInstances(*sceneDocument_);

    renderItems_.clear();
    opaqueRenderItemIndices_.clear();
    transparentRenderItemIndices_.clear();
    std::size_t primitiveInstanceCount = 0;
    for (const GltfSceneMeshInstance& instance : sceneInstances)
    {
        if (instance.mesh < sceneMeshPrimitiveAssetIndices_.size())
        {
            primitiveInstanceCount += sceneMeshPrimitiveAssetIndices_[instance.mesh].size();
        }
    }
    renderItems_.reserve(primitiveInstanceCount);
    opaqueRenderItemIndices_.reserve(primitiveInstanceCount);
    transparentRenderItemIndices_.reserve(primitiveInstanceCount);

    auto createRenderItem = [&](Mesh* mesh,
                                const std::uint32_t materialIndex,
                                const DirectX::XMFLOAT4X4& baseTransform)
    {
        RenderItem renderItem = {};
        renderItem.mesh = mesh;
        renderItem.materialIndex = materialIndex;
        renderItem.baseTransform = baseTransform;
        renderItem.rotationSpeed = 0.0f;
        renderItem.objectConstantBuffer = std::make_unique<ConstantBuffer>();
        renderItem.objectConstantBuffer->Initialize(*device_.Get(), sizeof(ObjectConstants));
        renderItem.objectCbvAllocation = cbvAllocator_->Allocate(1);

        D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc = {};
        constantBufferViewDesc.BufferLocation = renderItem.objectConstantBuffer->GetGpuVirtualAddress();
        constantBufferViewDesc.SizeInBytes = renderItem.objectConstantBuffer->GetAlignedSizeInBytes();
        device_->CreateConstantBufferView(&constantBufferViewDesc, renderItem.objectCbvAllocation.GetCpuHandle());

        renderItems_.push_back(std::move(renderItem));
        const std::uint32_t renderItemIndex = static_cast<std::uint32_t>(renderItems_.size() - 1);
        const auto& material = materialManager_->GetMaterial(materialIndex);
        if (IsBlendAlphaMode(material.desc.alphaMode))
        {
            transparentRenderItemIndices_.push_back(renderItemIndex);
        }
        else
        {
            opaqueRenderItemIndices_.push_back(renderItemIndex);
        }
    };

    const std::uint32_t fallbackMaterialIndex = runtimeMaterialIndices_.front();
    for (const GltfSceneMeshInstance& instance : sceneInstances)
    {
        if (instance.mesh >= sceneMeshPrimitiveAssetIndices_.size())
        {
            continue;
        }

        for (const std::uint32_t primitiveAssetIndex : sceneMeshPrimitiveAssetIndices_[instance.mesh])
        {
            const ScenePrimitiveAsset& primitiveAsset = scenePrimitiveAssets_.at(primitiveAssetIndex);
            const std::uint32_t materialIndex =
                primitiveAsset.material != kInvalidGltfIndex && primitiveAsset.material < runtimeMaterialIndices_.size()
                    ? runtimeMaterialIndices_[primitiveAsset.material]
                    : fallbackMaterialIndex;
            createRenderItem(primitiveAsset.mesh.get(), materialIndex, instance.worldTransform);
        }
    }
}

void D3D12Renderer::UpdateCamera(const float deltaTimeSeconds)
{
    constexpr float orbitSpeed = 1.7f;
    constexpr float zoomSpeed = 2.2f;

    if (window_.IsKeyDown('A'))
    {
        cameraOrbitYaw_ -= orbitSpeed * deltaTimeSeconds;
    }
    if (window_.IsKeyDown('D'))
    {
        cameraOrbitYaw_ += orbitSpeed * deltaTimeSeconds;
    }
    if (window_.IsKeyDown('W'))
    {
        cameraOrbitPitch_ += orbitSpeed * deltaTimeSeconds;
    }
    if (window_.IsKeyDown('S'))
    {
        cameraOrbitPitch_ -= orbitSpeed * deltaTimeSeconds;
    }
    if (window_.IsKeyDown('Q'))
    {
        cameraDistance_ = std::max(1.2f, cameraDistance_ - zoomSpeed * deltaTimeSeconds);
    }
    if (window_.IsKeyDown('E'))
    {
        cameraDistance_ = std::min(8.0f, cameraDistance_ + zoomSpeed * deltaTimeSeconds);
    }
    if (window_.IsKeyDown('R'))
    {
        cameraOrbitYaw_ = 0.0f;
        cameraOrbitPitch_ = 0.0f;
        cameraDistance_ = 3.0f;
    }

    cameraOrbitPitch_ = std::clamp(cameraOrbitPitch_, -1.2f, 1.2f);

    const float cosPitch = std::cos(cameraOrbitPitch_);
    const float sinPitch = std::sin(cameraOrbitPitch_);
    const float cosYaw = std::cos(cameraOrbitYaw_);
    const float sinYaw = std::sin(cameraOrbitYaw_);

    camera_.target = cameraTarget_;
    camera_.up = {0.0f, 1.0f, 0.0f};
    camera_.position = {
        cameraTarget_.x + cosPitch * sinYaw * cameraDistance_,
        cameraTarget_.y + sinPitch * cameraDistance_,
        cameraTarget_.z - cosPitch * cosYaw * cameraDistance_};
}

void D3D12Renderer::UpdateSceneConstants()
{
    const float width = static_cast<float>(std::max(window_.GetClientWidth(), 1u));
    const float height = static_cast<float>(std::max(window_.GetClientHeight(), 1u));
    const float aspect = width / height;
    const float tanHalfFovY = std::tan(camera_.verticalFovRadians * 0.5f);
    const float tanHalfFovX = tanHalfFovY * aspect;
    const DirectX::XMVECTOR cameraPosition = DirectX::XMLoadFloat3(&camera_.position);
    const DirectX::XMVECTOR cameraTarget = DirectX::XMLoadFloat3(&camera_.target);
    const DirectX::XMVECTOR cameraUp = DirectX::XMLoadFloat3(&camera_.up);
    const DirectX::XMVECTOR cameraForwardVector =
        DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(cameraTarget, cameraPosition));
    const DirectX::XMVECTOR cameraRightVector =
        DirectX::XMVector3Normalize(DirectX::XMVector3Cross(cameraUp, cameraForwardVector));
    const DirectX::XMVECTOR orthonormalUpVector =
        DirectX::XMVector3Normalize(DirectX::XMVector3Cross(cameraForwardVector, cameraRightVector));
    const DirectX::XMVECTOR lightDirection = DirectX::XMVector3Normalize(DirectX::XMVectorSet(0.35f, 0.8f, -0.45f, 0.0f));
    const DirectX::XMVECTOR sceneCenter = DirectX::XMVectorSet(cameraTarget_.x, cameraTarget_.y, cameraTarget_.z, 1.0f);
    const DirectX::XMVECTOR lightPosition = DirectX::XMVectorAdd(sceneCenter, DirectX::XMVectorScale(lightDirection, 5.5f));
    const DirectX::XMVECTOR lightUp = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const DirectX::XMMATRIX lightView = DirectX::XMMatrixLookAtLH(lightPosition, sceneCenter, lightUp);
    const DirectX::XMMATRIX lightProjection = DirectX::XMMatrixOrthographicLH(6.0f, 6.0f, 0.1f, 12.0f);
    const DirectX::XMMATRIX lightViewProjection = lightView * lightProjection;

    SceneConstants constants = {};
    constants.cameraPositionAndEnvironmentIntensity = {
        camera_.position.x,
        camera_.position.y,
        camera_.position.z,
        1.05f};
    constants.directionalLightDirectionAndIntensity = {0.35f, 0.8f, -0.45f, 4.25f};
    constants.directionalLightColorAndExposure = {1.0f, 0.96f, 0.92f, 1.0f};
    DirectX::XMStoreFloat4(&constants.cameraRightAndTanHalfFovX, cameraRightVector);
    constants.cameraRightAndTanHalfFovX.w = tanHalfFovX;
    DirectX::XMStoreFloat4(&constants.cameraUpAndTanHalfFovY, orthonormalUpVector);
    constants.cameraUpAndTanHalfFovY.w = tanHalfFovY;
    DirectX::XMStoreFloat4(&constants.cameraForward, cameraForwardVector);
    constants.cameraForward.w = 0.0f;
    DirectX::XMStoreFloat4x4(&constants.lightViewProjection, DirectX::XMMatrixTranspose(lightViewProjection));
    constants.shadowParams = {
        1.0f / static_cast<float>(kShadowMapSize),
        1.0f / static_cast<float>(kShadowMapSize),
        0.0035f,
        0.0f};
    sceneConstantBuffer_->Update(std::as_bytes(std::span {&constants, 1}));
}

void D3D12Renderer::UpdateRenderItemConstants(
    RenderItem& renderItem,
    const DirectX::XMMATRIX& view,
    const DirectX::XMMATRIX& projection,
    const float elapsedSeconds)
{
    const DirectX::XMMATRIX scale =
        DirectX::XMMatrixScaling(renderItem.scale.x, renderItem.scale.y, renderItem.scale.z);
    const DirectX::XMMATRIX rotation =
        DirectX::XMMatrixRotationZ(renderItem.rotationOffset + elapsedSeconds * renderItem.rotationSpeed);
    const DirectX::XMMATRIX translation =
        DirectX::XMMatrixTranslation(renderItem.translation.x, renderItem.translation.y, renderItem.translation.z);
    const DirectX::XMMATRIX baseTransform = DirectX::XMLoadFloat4x4(&renderItem.baseTransform);
    const DirectX::XMMATRIX world = scale * rotation * translation * baseTransform;
    const DirectX::XMMATRIX worldViewProjection = world * view * projection;
    const DirectX::XMMATRIX worldInverseTranspose = DirectX::XMMatrixInverse(nullptr, world);
    const DirectX::XMVECTOR worldCenter = DirectX::XMVector3TransformCoord(DirectX::XMVectorZero(), world);

    ObjectConstants constants = {};
    DirectX::XMStoreFloat4x4(&constants.world, DirectX::XMMatrixTranspose(world));
    DirectX::XMStoreFloat4x4(&constants.worldViewProjection, DirectX::XMMatrixTranspose(worldViewProjection));
    DirectX::XMStoreFloat4x4(&constants.worldInverseTranspose, DirectX::XMMatrixTranspose(worldInverseTranspose));
    DirectX::XMStoreFloat3(&renderItem.worldCenter, worldCenter);
    renderItem.objectConstantBuffer->Update(std::as_bytes(std::span {&constants, 1}));
}

std::uint64_t D3D12Renderer::Signal()
{
    const std::uint64_t fenceValue = nextFenceValue_++;
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue), "ID3D12CommandQueue::Signal");
    return fenceValue;
}

void D3D12Renderer::WaitForFenceValue(const std::uint64_t fenceValue)
{
    if (fence_->GetCompletedValue() >= fenceValue)
    {
        return;
    }

    ThrowIfFailed(fence_->SetEventOnCompletion(fenceValue, fenceEvent_), "ID3D12Fence::SetEventOnCompletion");
    WaitForSingleObject(fenceEvent_, INFINITE);
}

void D3D12Renderer::ExecuteImmediateCommands()
{
    ThrowIfFailed(commandList_->Close(), "ID3D12GraphicsCommandList::Close");

    ID3D12CommandList* commandLists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, commandLists);

    WaitForFenceValue(Signal());
}

void D3D12Renderer::UpdateViewport(const std::uint32_t width, const std::uint32_t height)
{
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width);
    viewport_.Height = static_cast<float>(height);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left = 0;
    scissorRect_.top = 0;
    scissorRect_.right = static_cast<LONG>(width);
    scissorRect_.bottom = static_cast<LONG>(height);
}

void D3D12Renderer::MoveToNextFrame()
{
    frameFenceValues_[frameIndex_] = Signal();

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    WaitForFenceValue(frameFenceValues_[frameIndex_]);
}
} // namespace ugc_renderer
