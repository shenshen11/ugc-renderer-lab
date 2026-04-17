#include "ugc_renderer/render/constant_buffer.h"
#include "ugc_renderer/render/descriptor_allocator.h"
#include "ugc_renderer/render/d3d12_renderer.h"

#include "ugc_renderer/core/logger.h"
#include "ugc_renderer/core/throw_if_failed.h"
#include "ugc_renderer/platform/window.h"
#include "ugc_renderer/render/gpu_buffer.h"
#include "ugc_renderer/render/shader_compiler.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>

#include <DirectXMath.h>

namespace ugc_renderer
{
using Microsoft::WRL::ComPtr;

namespace
{
struct Vertex
{
    float position[3];
    float color[4];
};

struct SceneConstants
{
    DirectX::XMFLOAT4X4 mvp;
};
} // namespace

D3D12Renderer::D3D12Renderer(Window& window)
    : window_(window)
{
    EnableDebugLayerIfAvailable();
    CreateFactory();
    CreateDevice();
    CreateCommandObjects();
    CreateSwapChain();
    CreateDescriptorHeap();
    CreateRenderTargets();
    CreateFence();
    CreateConstantBuffer();
    CreateTrianglePipeline();
    CreateTriangleGeometry();
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

void D3D12Renderer::Render()
{
    auto& commandAllocator = commandAllocators_[frameIndex_];
    ThrowIfFailed(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
    ThrowIfFailed(commandList_->Reset(commandAllocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset");
    UpdateSceneConstants();

    D3D12_RESOURCE_BARRIER toRenderTarget = {};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Transition.pResource = renderTargets_[frameIndex_].Get();
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList_->ResourceBarrier(1, &toRenderTarget);
    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvAllocation_->GetCpuHandle(frameIndex_);

    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    constexpr std::array<float, 4> clearColor = {0.07f, 0.12f, 0.18f, 1.0f};
    commandList_->ClearRenderTargetView(rtvHandle, clearColor.data(), 0, nullptr);
    ID3D12DescriptorHeap* descriptorHeaps[] = {cbvAllocator_->GetHeap()};
    commandList_->SetDescriptorHeaps(static_cast<UINT>(std::size(descriptorHeaps)), descriptorHeaps);
    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->SetPipelineState(pipelineState_.Get());
    commandList_->SetGraphicsRootDescriptorTable(0, cbvAllocation_->GetGpuHandle());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const auto& vertexBufferView = vertexBuffer_->GetVertexBufferView();
    commandList_->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList_->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER toPresent = toRenderTarget;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList_->ResourceBarrier(1, &toPresent);

    ThrowIfFailed(commandList_->Close(), "ID3D12GraphicsCommandList::Close");

    ID3D12CommandList* commandLists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, commandLists);

    ThrowIfFailed(swapChain_->Present(1, 0), "IDXGISwapChain::Present");
    MoveToNextFrame();
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
    CreateRenderTargets();
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

    cbvAllocator_ = std::make_unique<DescriptorAllocator>();
    cbvAllocator_->Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    cbvAllocation_ = std::make_unique<DescriptorAllocation>(cbvAllocator_->Allocate(1));
}

void D3D12Renderer::CreateRenderTargets()
{
    for (std::uint32_t index = 0; index < kFrameCount; ++index)
    {
        ThrowIfFailed(swapChain_->GetBuffer(index, IID_PPV_ARGS(&renderTargets_[index])), "IDXGISwapChain::GetBuffer");
        device_->CreateRenderTargetView(renderTargets_[index].Get(), nullptr, rtvAllocation_->GetCpuHandle(index));
    }
}

void D3D12Renderer::CreateFence()
{
    ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "ID3D12Device::CreateFence");
    fenceValues_[frameIndex_] = 1;

    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent_ == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "CreateEventW");
    }
}

void D3D12Renderer::CreateConstantBuffer()
{
    sceneConstantBuffer_ = std::make_unique<ConstantBuffer>();
    sceneConstantBuffer_->Initialize(*device_.Get(), sizeof(SceneConstants));

    D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc = {};
    constantBufferViewDesc.BufferLocation = sceneConstantBuffer_->GetGpuVirtualAddress();
    constantBufferViewDesc.SizeInBytes = sceneConstantBuffer_->GetAlignedSizeInBytes();

    device_->CreateConstantBufferView(&constantBufferViewDesc, cbvAllocation_->GetCpuHandle());
}

void D3D12Renderer::CreateTrianglePipeline()
{
    const auto shaderPath = ShaderCompiler::ResolveShaderPath(L"triangle.hlsl");
    const auto vertexShader = ShaderCompiler::CompileFromFile(shaderPath, "VSMain", "vs_5_0");
    const auto pixelShader = ShaderCompiler::CompileFromFile(shaderPath, "PSMain", "ps_5_0");

    D3D12_DESCRIPTOR_RANGE descriptorRange = {};
    descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptorRange.NumDescriptors = 1;
    descriptorRange.BaseShaderRegister = 0;
    descriptorRange.RegisterSpace = 0;
    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameter = {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = 1;
    rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRange;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 1;
    rootSignatureDesc.pParameters = &rootParameter;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
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
    };

    D3D12_BLEND_DESC blendState = {};
    blendState.AlphaToCoverageEnable = FALSE;
    blendState.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
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
    for (auto& renderTarget : blendState.RenderTarget)
    {
        renderTarget = defaultRenderTargetBlendDesc;
    }

    D3D12_RASTERIZER_DESC rasterizerState = {};
    rasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerState.FrontCounterClockwise = FALSE;
    rasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizerState.DepthClipEnable = TRUE;
    rasterizerState.MultisampleEnable = FALSE;
    rasterizerState.AntialiasedLineEnable = FALSE;
    rasterizerState.ForcedSampleCount = 0;
    rasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};
    pipelineStateDesc.pRootSignature = rootSignature_.Get();
    pipelineStateDesc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    pipelineStateDesc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    pipelineStateDesc.BlendState = blendState;
    pipelineStateDesc.SampleMask = UINT_MAX;
    pipelineStateDesc.RasterizerState = rasterizerState;
    pipelineStateDesc.DepthStencilState.DepthEnable = FALSE;
    pipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
    pipelineStateDesc.InputLayout = {inputElements, static_cast<UINT>(std::size(inputElements))};
    pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateDesc.NumRenderTargets = 1;
    pipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipelineStateDesc.SampleDesc.Count = 1;

    ThrowIfFailed(
        device_->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(&pipelineState_)),
        "ID3D12Device::CreateGraphicsPipelineState");
}

void D3D12Renderer::CreateTriangleGeometry()
{
    constexpr std::array<Vertex, 3> vertices = {{
        {{0.0f, 0.35f, 0.0f}, {1.0f, 0.2f, 0.2f, 1.0f}},
        {{0.35f, -0.35f, 0.0f}, {0.2f, 1.0f, 0.2f, 1.0f}},
        {{-0.35f, -0.35f, 0.0f}, {0.2f, 0.4f, 1.0f, 1.0f}},
    }};

    auto& commandAllocator = commandAllocators_[frameIndex_];
    ThrowIfFailed(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
    ThrowIfFailed(commandList_->Reset(commandAllocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset");

    vertexBuffer_ = std::make_unique<GpuBuffer>();
    vertexBuffer_->InitializeVertexBuffer(
        *device_.Get(),
        *commandList_.Get(),
        std::as_bytes(std::span(vertices)),
        sizeof(Vertex));

    ExecuteImmediateCommands();
    vertexBuffer_->ReleaseUploadResource();
}

void D3D12Renderer::UpdateSceneConstants()
{
    const float width = static_cast<float>(std::max(window_.GetClientWidth(), 1u));
    const float height = static_cast<float>(std::max(window_.GetClientHeight(), 1u));
    const float aspect = width / height;

    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();

    const DirectX::XMMATRIX aspectCorrection = DirectX::XMMatrixScaling(1.0f / aspect, 1.0f, 1.0f);
    const DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationZ(elapsedSeconds * 0.8f);
    const DirectX::XMMATRIX transform = rotation * aspectCorrection;

    SceneConstants constants = {};
    DirectX::XMStoreFloat4x4(&constants.mvp, DirectX::XMMatrixTranspose(transform));
    sceneConstantBuffer_->Update(std::as_bytes(std::span {&constants, 1}));
}

std::uint64_t D3D12Renderer::Signal()
{
    const std::uint64_t fenceValue = fenceValues_[frameIndex_];
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue), "ID3D12CommandQueue::Signal");
    ++fenceValues_[frameIndex_];
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
    const std::uint64_t currentFenceValue = fenceValues_[frameIndex_];
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), currentFenceValue), "ID3D12CommandQueue::Signal");

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    if (fence_->GetCompletedValue() < fenceValues_[frameIndex_])
    {
        ThrowIfFailed(
            fence_->SetEventOnCompletion(fenceValues_[frameIndex_], fenceEvent_),
            "ID3D12Fence::SetEventOnCompletion");
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    fenceValues_[frameIndex_] = currentFenceValue + 1;
}
} // namespace ugc_renderer
