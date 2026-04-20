#include "ugc_renderer/render/constant_buffer.h"
#include "ugc_renderer/render/descriptor_allocator.h"
#include "ugc_renderer/render/d3d12_renderer.h"

#include "ugc_renderer/asset/gltf_loader.h"
#include "ugc_renderer/asset/gltf_mesh_builder.h"
#include "ugc_renderer/asset/gltf_scene_builder.h"
#include "ugc_renderer/core/logger.h"
#include "ugc_renderer/core/throw_if_failed.h"
#include "ugc_renderer/platform/window.h"
#include "ugc_renderer/render/gpu_debug_marker.h"
#include "ugc_renderer/render/image_loader.h"
#include "ugc_renderer/render/material_manager.h"
#include "ugc_renderer/render/mesh.h"
#include "ugc_renderer/render/render_graph.h"
#include "ugc_renderer/render/shader_compiler.h"
#include "ugc_renderer/render/texture2d.h"
#include "ugc_renderer/render/texture_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <DirectXMath.h>
#include <wincodec.h>

namespace ugc_renderer
{
using Microsoft::WRL::ComPtr;

namespace
{
constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kBloomFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr std::uint32_t kBloomDownsampleFactor = 2;
constexpr std::uint32_t kInvalidPhysicalResourceIndex = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint64_t kRenderGraphFrameMarkerColor = 0xFF8E8E93ull;
constexpr std::uint64_t kGraphicsPassMarkerColor = 0xFF5E81ACull;
constexpr std::uint64_t kFullscreenPassMarkerColor = 0xFFB48EADull;
constexpr std::uint64_t kPresentPassMarkerColor = 0xFFA3BE8Cull;
constexpr std::uint64_t kGenericPassMarkerColor = 0xFFD8DEE9ull;

struct LinearColor
{
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

[[nodiscard]] bool HasBindFlag(
    const RenderGraph::ResourceBindFlags bindFlags,
    const RenderGraph::ResourceBindFlags flag) noexcept
{
    return (static_cast<std::uint32_t>(bindFlags) & static_cast<std::uint32_t>(flag)) != 0;
}

[[nodiscard]] RuntimeRenderSettings ClampRuntimeRenderSettings(RuntimeRenderSettings settings) noexcept
{
    settings.environmentIntensity = std::clamp(settings.environmentIntensity, 0.0f, 2.5f);
    settings.exposure = std::clamp(settings.exposure, 0.25f, 2.5f);
    settings.shadowBias = std::clamp(settings.shadowBias, 0.0005f, 0.02f);
    settings.bloomThreshold = std::clamp(settings.bloomThreshold, 0.25f, 2.5f);
    settings.bloomSoftKnee = std::clamp(settings.bloomSoftKnee, 0.05f, 1.0f);
    settings.bloomIntensity = std::clamp(settings.bloomIntensity, 0.0f, 1.5f);
    settings.bloomRadius = std::clamp(settings.bloomRadius, 0.5f, 2.5f);
    settings.iblDiffuseIntensity = std::clamp(settings.iblDiffuseIntensity, 0.0f, 2.0f);
    settings.iblSpecularIntensity = std::clamp(settings.iblSpecularIntensity, 0.0f, 2.0f);
    settings.iblSpecularBlend = std::clamp(settings.iblSpecularBlend, 0.0f, 1.5f);
    return settings;
}

[[nodiscard]] MaterialDesc ClampRuntimeMaterialDesc(MaterialDesc material) noexcept
{
    material.constants.baseColorFactor.x = std::clamp(material.constants.baseColorFactor.x, 0.0f, 2.0f);
    material.constants.baseColorFactor.y = std::clamp(material.constants.baseColorFactor.y, 0.0f, 2.0f);
    material.constants.baseColorFactor.z = std::clamp(material.constants.baseColorFactor.z, 0.0f, 2.0f);
    material.constants.baseColorFactor.w = std::clamp(material.constants.baseColorFactor.w, 0.0f, 1.0f);
    material.constants.emissiveFactorAndMetallic.x =
        std::clamp(material.constants.emissiveFactorAndMetallic.x, 0.0f, 4.0f);
    material.constants.emissiveFactorAndMetallic.y =
        std::clamp(material.constants.emissiveFactorAndMetallic.y, 0.0f, 4.0f);
    material.constants.emissiveFactorAndMetallic.z =
        std::clamp(material.constants.emissiveFactorAndMetallic.z, 0.0f, 4.0f);
    material.constants.emissiveFactorAndMetallic.w =
        std::clamp(material.constants.emissiveFactorAndMetallic.w, 0.0f, 1.0f);
    material.constants.roughnessUvScaleAlphaCutoff.x =
        std::clamp(material.constants.roughnessUvScaleAlphaCutoff.x, 0.0f, 1.0f);
    material.constants.roughnessUvScaleAlphaCutoff.w =
        std::clamp(material.constants.roughnessUvScaleAlphaCutoff.w, 0.0f, 1.0f);
    material.constants.textureControls.x = std::clamp(material.constants.textureControls.x, 0.0f, 4.0f);
    material.constants.textureControls.y = std::clamp(material.constants.textureControls.y, 0.0f, 1.0f);

    switch (material.alphaMode)
    {
    case MaterialAlphaMode::Opaque:
    case MaterialAlphaMode::Mask:
    case MaterialAlphaMode::Blend:
        break;
    default:
        material.alphaMode = MaterialAlphaMode::Opaque;
        break;
    }

    material.constants.textureControls.z = static_cast<float>(static_cast<std::uint32_t>(material.alphaMode));
    material.constants.textureControls.w = material.doubleSided ? 1.0f : 0.0f;
    return material;
}

[[nodiscard]] std::string BuildRuntimeMaterialName(const std::string& sourceName, const std::uint32_t materialIndex)
{
    if (!sourceName.empty())
    {
        return std::to_string(materialIndex) + ": " + sourceName;
    }

    return "Material " + std::to_string(materialIndex);
}

[[nodiscard]] std::uint32_t GetBloomWidth(const std::uint32_t width) noexcept
{
    return std::max(width / kBloomDownsampleFactor, 1u);
}

[[nodiscard]] std::uint32_t GetBloomHeight(const std::uint32_t height) noexcept
{
    return std::max(height / kBloomDownsampleFactor, 1u);
}

[[nodiscard]] float SRgbChannelToLinear(const std::byte value)
{
    return std::pow(static_cast<float>(std::to_integer<std::uint8_t>(value)) / 255.0f, 2.2f);
}

[[nodiscard]] std::byte LinearChannelToSrgb(const float value)
{
    const float encoded = std::pow(std::clamp(value, 0.0f, 1.0f), 1.0f / 2.2f);
    return static_cast<std::byte>(std::clamp(static_cast<int>(std::round(encoded * 255.0f)), 0, 255));
}

[[nodiscard]] LinearColor LoadLinearColor(
    const ImageData& imageData,
    const std::int32_t x,
    const std::int32_t y)
{
    const std::int32_t wrappedX =
        ((x % static_cast<std::int32_t>(imageData.width)) + static_cast<std::int32_t>(imageData.width))
        % static_cast<std::int32_t>(imageData.width);
    const std::int32_t clampedY = std::clamp(y, 0, static_cast<std::int32_t>(imageData.height) - 1);
    const std::size_t pixelIndex =
        (static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(imageData.width)
         + static_cast<std::size_t>(wrappedX))
        * 4;

    return LinearColor {
        .red = SRgbChannelToLinear(imageData.pixels[pixelIndex + 0]),
        .green = SRgbChannelToLinear(imageData.pixels[pixelIndex + 1]),
        .blue = SRgbChannelToLinear(imageData.pixels[pixelIndex + 2]),
    };
}

[[nodiscard]] LinearColor Lerp(const LinearColor& left, const LinearColor& right, const float factor)
{
    return LinearColor {
        .red = std::lerp(left.red, right.red, factor),
        .green = std::lerp(left.green, right.green, factor),
        .blue = std::lerp(left.blue, right.blue, factor),
    };
}

[[nodiscard]] LinearColor SamplePanoramaBilinear(
    const ImageData& imageData,
    const float u,
    const float v)
{
    const float wrappedU = u - std::floor(u);
    const float clampedV = std::clamp(v, 0.0f, 1.0f);
    const float sourceX = wrappedU * static_cast<float>(imageData.width) - 0.5f;
    const float sourceY = clampedV * static_cast<float>(imageData.height) - 0.5f;
    const std::int32_t x0 = static_cast<std::int32_t>(std::floor(sourceX));
    const std::int32_t y0 = static_cast<std::int32_t>(std::floor(sourceY));
    const std::int32_t x1 = x0 + 1;
    const std::int32_t y1 = y0 + 1;
    const float tx = sourceX - static_cast<float>(x0);
    const float ty = sourceY - static_cast<float>(y0);

    const LinearColor sample00 = LoadLinearColor(imageData, x0, y0);
    const LinearColor sample10 = LoadLinearColor(imageData, x1, y0);
    const LinearColor sample01 = LoadLinearColor(imageData, x0, y1);
    const LinearColor sample11 = LoadLinearColor(imageData, x1, y1);
    return Lerp(Lerp(sample00, sample10, tx), Lerp(sample01, sample11, tx), ty);
}

[[nodiscard]] std::vector<LinearColor> CreateDownsampledPanorama(
    const ImageData& imageData,
    const std::uint32_t width,
    const std::uint32_t height)
{
    std::vector<LinearColor> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] =
                SamplePanoramaBilinear(imageData, u, v);
        }
    }

    return pixels;
}

void BoxBlurPanoramaHorizontal(
    std::vector<LinearColor>& pixels,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::int32_t radius)
{
    std::vector<LinearColor> blurred(pixels.size());
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            LinearColor accumulated = {};
            float totalWeight = 0.0f;
            for (std::int32_t offset = -radius; offset <= radius; ++offset)
            {
                const std::int32_t wrappedX =
                    (static_cast<std::int32_t>(x) + offset + static_cast<std::int32_t>(width))
                    % static_cast<std::int32_t>(width);
                const float normalizedDistance =
                    static_cast<float>(std::abs(offset)) / static_cast<float>(std::max(radius, 1));
                const float weight = 1.0f - normalizedDistance * 0.65f;
                const LinearColor& sample =
                    pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + wrappedX];
                accumulated.red += sample.red * weight;
                accumulated.green += sample.green * weight;
                accumulated.blue += sample.blue * weight;
                totalWeight += weight;
            }

            blurred[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] = {
                accumulated.red / std::max(totalWeight, 1e-4f),
                accumulated.green / std::max(totalWeight, 1e-4f),
                accumulated.blue / std::max(totalWeight, 1e-4f),
            };
        }
    }

    pixels = std::move(blurred);
}

void BoxBlurPanoramaVertical(
    std::vector<LinearColor>& pixels,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::int32_t radius)
{
    std::vector<LinearColor> blurred(pixels.size());
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            LinearColor accumulated = {};
            float totalWeight = 0.0f;
            for (std::int32_t offset = -radius; offset <= radius; ++offset)
            {
                const std::int32_t clampedY = std::clamp(
                    static_cast<std::int32_t>(y) + offset,
                    0,
                    static_cast<std::int32_t>(height) - 1);
                const float normalizedDistance =
                    static_cast<float>(std::abs(offset)) / static_cast<float>(std::max(radius, 1));
                const float weight = 1.0f - normalizedDistance * 0.65f;
                const LinearColor& sample =
                    pixels[static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(width) + x];
                accumulated.red += sample.red * weight;
                accumulated.green += sample.green * weight;
                accumulated.blue += sample.blue * weight;
                totalWeight += weight;
            }

            blurred[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] = {
                accumulated.red / std::max(totalWeight, 1e-4f),
                accumulated.green / std::max(totalWeight, 1e-4f),
                accumulated.blue / std::max(totalWeight, 1e-4f),
            };
        }
    }

    pixels = std::move(blurred);
}

[[nodiscard]] ImageData EncodePanoramaToImageData(
    const std::vector<LinearColor>& pixels,
    const std::uint32_t width,
    const std::uint32_t height)
{
    ImageData imageData = {};
    imageData.width = width;
    imageData.height = height;
    imageData.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    imageData.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);

    for (std::size_t pixelIndex = 0; pixelIndex < pixels.size(); ++pixelIndex)
    {
        const std::size_t byteOffset = pixelIndex * 4;
        imageData.pixels[byteOffset + 0] = LinearChannelToSrgb(pixels[pixelIndex].red);
        imageData.pixels[byteOffset + 1] = LinearChannelToSrgb(pixels[pixelIndex].green);
        imageData.pixels[byteOffset + 2] = LinearChannelToSrgb(pixels[pixelIndex].blue);
        imageData.pixels[byteOffset + 3] = static_cast<std::byte>(255);
    }

    return imageData;
}

[[nodiscard]] ImageData CreateFilteredPanorama(
    const ImageData& sourceImage,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::int32_t blurRadius,
    const std::int32_t blurPasses)
{
    std::vector<LinearColor> pixels = CreateDownsampledPanorama(sourceImage, width, height);
    for (std::int32_t passIndex = 0; passIndex < blurPasses; ++passIndex)
    {
        BoxBlurPanoramaHorizontal(pixels, width, height, blurRadius);
        BoxBlurPanoramaVertical(pixels, width, height, blurRadius);
    }

    return EncodePanoramaToImageData(pixels, width, height);
}

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

std::filesystem::path GetWorkflowPath(const std::wstring_view relativePath)
{
    std::array<wchar_t, MAX_PATH> modulePath = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length == modulePath.size())
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "GetModuleFileNameW");
    }

    return std::filesystem::path(modulePath.data()).parent_path() / L"workflow" / std::filesystem::path(relativePath);
}

bool WriteTextFile(const std::filesystem::path& path, const std::string_view contents)
{
    std::error_code errorCode;
    std::filesystem::create_directories(path.parent_path(), errorCode);
    if (errorCode)
    {
        Logger::Error(std::string("Failed to create workflow output directory: ") + path.parent_path().string());
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        Logger::Error(std::string("Failed to open workflow output file: ") + path.string());
        return false;
    }

    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output.good())
    {
        Logger::Error(std::string("Failed to write workflow output file: ") + path.string());
        return false;
    }

    return true;
}

std::wstring MakeScreenshotBaseName()
{
    SYSTEMTIME localTime = {};
    GetLocalTime(&localTime);

    wchar_t buffer[64] = {};
    swprintf_s(
        buffer,
        L"screenshot_%04u%02u%02u_%02u%02u%02u_%03u",
        localTime.wYear,
        localTime.wMonth,
        localTime.wDay,
        localTime.wHour,
        localTime.wMinute,
        localTime.wSecond,
        localTime.wMilliseconds);
    return buffer;
}

std::string SerializeRuntimeRenderSettings(const RuntimeRenderSettings& settings)
{
    std::ostringstream output;
    output << "version=1\n";
    output << "environmentIntensity=" << settings.environmentIntensity << '\n';
    output << "exposure=" << settings.exposure << '\n';
    output << "shadowBias=" << settings.shadowBias << '\n';
    output << "bloomThreshold=" << settings.bloomThreshold << '\n';
    output << "bloomSoftKnee=" << settings.bloomSoftKnee << '\n';
    output << "bloomIntensity=" << settings.bloomIntensity << '\n';
    output << "bloomRadius=" << settings.bloomRadius << '\n';
    output << "iblDiffuseIntensity=" << settings.iblDiffuseIntensity << '\n';
    output << "iblSpecularIntensity=" << settings.iblSpecularIntensity << '\n';
    output << "iblSpecularBlend=" << settings.iblSpecularBlend << '\n';
    output << "debugView=" << ToString(settings.debugView) << '\n';
    return output.str();
}

bool SaveScreenshotPng(
    const std::filesystem::path& path,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const std::byte> rgbaPixels,
    const std::size_t sourceRowPitch)
{
    std::error_code errorCode;
    std::filesystem::create_directories(path.parent_path(), errorCode);
    if (errorCode)
    {
        Logger::Error(std::string("Failed to create screenshot output directory: ") + path.parent_path().string());
        return false;
    }

    const std::size_t destinationRowPitch = static_cast<std::size_t>(width) * 4u;
    std::vector<std::byte> bgraPixels(static_cast<std::size_t>(height) * destinationRowPitch);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const std::byte* sourceRow = rgbaPixels.data() + static_cast<std::size_t>(y) * sourceRowPitch;
        std::byte* destinationRow = bgraPixels.data() + static_cast<std::size_t>(y) * destinationRowPitch;
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::size_t pixelOffset = static_cast<std::size_t>(x) * 4u;
            destinationRow[pixelOffset + 0] = sourceRow[pixelOffset + 2];
            destinationRow[pixelOffset + 1] = sourceRow[pixelOffset + 1];
            destinationRow[pixelOffset + 2] = sourceRow[pixelOffset + 0];
            destinationRow[pixelOffset + 3] = sourceRow[pixelOffset + 3];
        }
    }

    ComPtr<IWICImagingFactory> imagingFactory;
    ThrowIfFailed(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imagingFactory)),
        "CoCreateInstance(CLSID_WICImagingFactory)");

    ComPtr<IWICStream> stream;
    ThrowIfFailed(imagingFactory->CreateStream(&stream), "IWICImagingFactory::CreateStream");
    ThrowIfFailed(
        stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE),
        "IWICStream::InitializeFromFilename");

    ComPtr<IWICBitmapEncoder> encoder;
    ThrowIfFailed(
        imagingFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder),
        "IWICImagingFactory::CreateEncoder(PNG)");
    ThrowIfFailed(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "IWICBitmapEncoder::Initialize");

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> propertyBag;
    ThrowIfFailed(encoder->CreateNewFrame(&frame, &propertyBag), "IWICBitmapEncoder::CreateNewFrame");
    ThrowIfFailed(frame->Initialize(propertyBag.Get()), "IWICBitmapFrameEncode::Initialize");
    ThrowIfFailed(frame->SetSize(width, height), "IWICBitmapFrameEncode::SetSize");

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    ThrowIfFailed(frame->SetPixelFormat(&pixelFormat), "IWICBitmapFrameEncode::SetPixelFormat");
    if (!IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA))
    {
        Logger::Error("PNG encoder did not accept 32bpp BGRA screenshot pixels.");
        return false;
    }

    ThrowIfFailed(
        frame->WritePixels(
            height,
            static_cast<UINT>(destinationRowPitch),
            static_cast<UINT>(bgraPixels.size()),
            reinterpret_cast<BYTE*>(bgraPixels.data())),
        "IWICBitmapFrameEncode::WritePixels");
    ThrowIfFailed(frame->Commit(), "IWICBitmapFrameEncode::Commit");
    ThrowIfFailed(encoder->Commit(), "IWICBitmapEncoder::Commit");
    return true;
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
    DirectX::XMFLOAT4 bloomParams = {1.15f, 0.35f, 0.18f, 1.0f};
    DirectX::XMFLOAT4 iblParams = {1.0f, 1.0f, 0.85f, 0.0f};
    DirectX::XMFLOAT4 frameBufferParams = {1.0f, 1.0f, 1.0f, 1.0f};
    DirectX::XMFLOAT4 postProcessParams = {0.0f, 0.0f, 0.0f, 0.0f};
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

D3D12_RESOURCE_STATES ToD3D12ResourceState(const RenderGraph::ResourceState state)
{
    switch (state)
    {
    case RenderGraph::ResourceState::ShaderRead:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case RenderGraph::ResourceState::RenderTarget:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case RenderGraph::ResourceState::DepthWrite:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case RenderGraph::ResourceState::Present:
        return D3D12_RESOURCE_STATE_PRESENT;
    case RenderGraph::ResourceState::Unknown:
        break;
    }

    return D3D12_RESOURCE_STATE_COMMON;
}

[[nodiscard]] std::uint64_t GetRenderGraphPassMarkerColor(const RenderGraph::PassType passType) noexcept
{
    switch (passType)
    {
    case RenderGraph::PassType::Graphics:
        return kGraphicsPassMarkerColor;
    case RenderGraph::PassType::Fullscreen:
        return kFullscreenPassMarkerColor;
    case RenderGraph::PassType::Present:
        return kPresentPassMarkerColor;
    case RenderGraph::PassType::Generic:
        return kGenericPassMarkerColor;
    }

    return kGenericPassMarkerColor;
}

[[nodiscard]] const char* GetRenderGraphPassMarkerLabel(const RenderGraph::CompiledPass& pass) noexcept
{
    if (pass.name == "ShadowDepth")
    {
        return "RG: Shadow Depth";
    }
    if (pass.name == "MainColorBegin")
    {
        return "RG: Main Color Begin";
    }
    if (pass.name == "Skybox")
    {
        return "RG: Skybox";
    }
    if (pass.name == "OpaqueGeometry")
    {
        return "RG: Opaque Geometry";
    }
    if (pass.name == "TransparentGeometry")
    {
        return "RG: Transparent Geometry";
    }
    if (pass.name == "BloomPrefilter")
    {
        return "RG: Bloom Prefilter";
    }
    if (pass.name == "BloomBlurHorizontal")
    {
        return "RG: Bloom Blur H";
    }
    if (pass.name == "BloomBlurVertical")
    {
        return "RG: Bloom Blur V";
    }
    if (pass.name == "PostProcess")
    {
        return "RG: ToneMap Composite";
    }
    if (pass.name == "PresentTransition")
    {
        return "RG: Present Transition";
    }

    return "RG: Pass";
}

struct CompiledRendererShaders
{
    ComPtr<ID3DBlob> materialVertexShader;
    ComPtr<ID3DBlob> materialPixelShader;
    ComPtr<ID3DBlob> skyboxVertexShader;
    ComPtr<ID3DBlob> skyboxPixelShader;
    ComPtr<ID3DBlob> shadowVertexShader;
    ComPtr<ID3DBlob> shadowPixelShader;
    ComPtr<ID3DBlob> bloomVertexShader;
    ComPtr<ID3DBlob> bloomPrefilterPixelShader;
    ComPtr<ID3DBlob> bloomBlurHorizontalPixelShader;
    ComPtr<ID3DBlob> bloomBlurVerticalPixelShader;
    ComPtr<ID3DBlob> postProcessVertexShader;
    ComPtr<ID3DBlob> postProcessPixelShader;
};

struct PipelineStateCollection
{
    std::array<ComPtr<ID3D12PipelineState>, 4> materialPipelineStates;
    ComPtr<ID3D12PipelineState> skyboxPipelineState;
    ComPtr<ID3D12PipelineState> shadowPipelineState;
    ComPtr<ID3D12PipelineState> bloomPrefilterPipelineState;
    ComPtr<ID3D12PipelineState> bloomBlurHorizontalPipelineState;
    ComPtr<ID3D12PipelineState> bloomBlurVerticalPipelineState;
    ComPtr<ID3D12PipelineState> postProcessPipelineState;
};

[[nodiscard]] D3D12_SHADER_BYTECODE ToShaderBytecode(ID3DBlob* shaderBlob) noexcept
{
    if (shaderBlob == nullptr)
    {
        return {};
    }

    return {shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize()};
}

[[nodiscard]] const std::array<D3D12_INPUT_ELEMENT_DESC, 5>& GetRendererInputElements() noexcept
{
    static const std::array<D3D12_INPUT_ELEMENT_DESC, 5> inputElements = {{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    }};
    return inputElements;
}

[[nodiscard]] D3D12_BLEND_DESC CreateRendererBlendState(const bool alphaBlendEnabled) noexcept
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
}

[[nodiscard]] D3D12_RASTERIZER_DESC CreateRendererRasterizerState(const bool doubleSided) noexcept
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
}

[[nodiscard]] D3D12_DEPTH_STENCIL_DESC CreateRendererDepthStencilState(const bool alphaBlendEnabled) noexcept
{
    D3D12_DEPTH_STENCIL_DESC depthStencilState = {};
    depthStencilState.DepthEnable = TRUE;
    depthStencilState.DepthWriteMask = alphaBlendEnabled ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depthStencilState.StencilEnable = FALSE;
    return depthStencilState;
}

[[nodiscard]] CompiledRendererShaders CompileRendererShaders()
{
    CompiledRendererShaders shaders = {};

    const auto materialShaderPath = ShaderCompiler::ResolveShaderPath(L"triangle.hlsl");
    shaders.materialVertexShader = ShaderCompiler::CompileFromFile(materialShaderPath, "VSMain", "vs_5_0");
    shaders.materialPixelShader = ShaderCompiler::CompileFromFile(materialShaderPath, "PSMain", "ps_5_0");

    const auto skyboxShaderPath = ShaderCompiler::ResolveShaderPath(L"skybox.hlsl");
    shaders.skyboxVertexShader = ShaderCompiler::CompileFromFile(skyboxShaderPath, "VSMain", "vs_5_0");
    shaders.skyboxPixelShader = ShaderCompiler::CompileFromFile(skyboxShaderPath, "PSMain", "ps_5_0");

    const auto shadowShaderPath = ShaderCompiler::ResolveShaderPath(L"shadow_depth.hlsl");
    shaders.shadowVertexShader = ShaderCompiler::CompileFromFile(shadowShaderPath, "VSMain", "vs_5_0");
    shaders.shadowPixelShader = ShaderCompiler::CompileFromFile(shadowShaderPath, "PSMain", "ps_5_0");

    const auto bloomShaderPath = ShaderCompiler::ResolveShaderPath(L"bloom.hlsl");
    shaders.bloomVertexShader = ShaderCompiler::CompileFromFile(bloomShaderPath, "VSMain", "vs_5_0");
    shaders.bloomPrefilterPixelShader = ShaderCompiler::CompileFromFile(bloomShaderPath, "PSPrefilter", "ps_5_0");
    shaders.bloomBlurHorizontalPixelShader =
        ShaderCompiler::CompileFromFile(bloomShaderPath, "PSBlurHorizontal", "ps_5_0");
    shaders.bloomBlurVerticalPixelShader =
        ShaderCompiler::CompileFromFile(bloomShaderPath, "PSBlurVertical", "ps_5_0");

    const auto postProcessShaderPath = ShaderCompiler::ResolveShaderPath(L"post_process.hlsl");
    shaders.postProcessVertexShader = ShaderCompiler::CompileFromFile(postProcessShaderPath, "VSMain", "vs_5_0");
    shaders.postProcessPixelShader = ShaderCompiler::CompileFromFile(postProcessShaderPath, "PSMain", "ps_5_0");
    return shaders;
}

[[nodiscard]] ComPtr<ID3D12RootSignature> CreateRendererRootSignature(ID3D12Device& device)
{
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

    std::array<D3D12_DESCRIPTOR_RANGE, 9> textureRanges = {};
    for (std::uint32_t textureSlot = 0; textureSlot < textureRanges.size(); ++textureSlot)
    {
        textureRanges[textureSlot] = srvDescriptorRange;
        textureRanges[textureSlot].BaseShaderRegister = textureSlot;
    }

    std::array<D3D12_ROOT_PARAMETER, 12> rootParameters = {};
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
    const HRESULT result = D3D12SerializeRootSignature(
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

    ComPtr<ID3D12RootSignature> rootSignature;
    ThrowIfFailed(
        device.CreateRootSignature(
            0,
            serializedRootSignature->GetBufferPointer(),
            serializedRootSignature->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature)),
        "ID3D12Device::CreateRootSignature");
    return rootSignature;
}

[[nodiscard]] PipelineStateCollection CreateRendererPipelineStates(
    ID3D12Device& device,
    ID3D12RootSignature& rootSignature,
    const CompiledRendererShaders& shaders)
{
    PipelineStateCollection pipelineStates = {};
    const auto& inputElements = GetRendererInputElements();

    auto createMaterialPipelineState = [&](const PipelineStateKind pipelineStateKind,
                                           const bool alphaBlendEnabled,
                                           const bool doubleSided)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};
        pipelineStateDesc.pRootSignature = &rootSignature;
        pipelineStateDesc.VS = ToShaderBytecode(shaders.materialVertexShader.Get());
        pipelineStateDesc.PS = ToShaderBytecode(shaders.materialPixelShader.Get());
        pipelineStateDesc.BlendState = CreateRendererBlendState(alphaBlendEnabled);
        pipelineStateDesc.SampleMask = UINT_MAX;
        pipelineStateDesc.RasterizerState = CreateRendererRasterizerState(doubleSided);
        pipelineStateDesc.DepthStencilState = CreateRendererDepthStencilState(alphaBlendEnabled);
        pipelineStateDesc.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
        pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineStateDesc.NumRenderTargets = 1;
        pipelineStateDesc.RTVFormats[0] = kSceneColorFormat;
        pipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pipelineStateDesc.SampleDesc.Count = 1;

        ThrowIfFailed(
            device.CreateGraphicsPipelineState(
                &pipelineStateDesc,
                IID_PPV_ARGS(&pipelineStates.materialPipelineStates[static_cast<std::uint32_t>(pipelineStateKind)])),
            "ID3D12Device::CreateGraphicsPipelineState(material)");
    };

    createMaterialPipelineState(PipelineStateKind::OpaqueCullBack, false, false);
    createMaterialPipelineState(PipelineStateKind::OpaqueDoubleSided, false, true);
    createMaterialPipelineState(PipelineStateKind::BlendCullBack, true, false);
    createMaterialPipelineState(PipelineStateKind::BlendDoubleSided, true, true);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skyboxPipelineStateDesc = {};
    skyboxPipelineStateDesc.pRootSignature = &rootSignature;
    skyboxPipelineStateDesc.VS = ToShaderBytecode(shaders.skyboxVertexShader.Get());
    skyboxPipelineStateDesc.PS = ToShaderBytecode(shaders.skyboxPixelShader.Get());
    skyboxPipelineStateDesc.BlendState = CreateRendererBlendState(false);
    skyboxPipelineStateDesc.SampleMask = UINT_MAX;
    skyboxPipelineStateDesc.RasterizerState = CreateRendererRasterizerState(true);
    skyboxPipelineStateDesc.DepthStencilState.DepthEnable = FALSE;
    skyboxPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    skyboxPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    skyboxPipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
    skyboxPipelineStateDesc.InputLayout = {nullptr, 0};
    skyboxPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    skyboxPipelineStateDesc.NumRenderTargets = 1;
    skyboxPipelineStateDesc.RTVFormats[0] = kSceneColorFormat;
    skyboxPipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    skyboxPipelineStateDesc.SampleDesc.Count = 1;

    ThrowIfFailed(
        device.CreateGraphicsPipelineState(&skyboxPipelineStateDesc, IID_PPV_ARGS(&pipelineStates.skyboxPipelineState)),
        "ID3D12Device::CreateGraphicsPipelineState(skybox)");

    D3D12_RASTERIZER_DESC shadowRasterizerState = CreateRendererRasterizerState(true);
    shadowRasterizerState.DepthBias = 1000;
    shadowRasterizerState.SlopeScaledDepthBias = 1.5f;
    shadowRasterizerState.DepthBiasClamp = 0.01f;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPipelineStateDesc = {};
    shadowPipelineStateDesc.pRootSignature = &rootSignature;
    shadowPipelineStateDesc.VS = ToShaderBytecode(shaders.shadowVertexShader.Get());
    shadowPipelineStateDesc.PS = ToShaderBytecode(shaders.shadowPixelShader.Get());
    shadowPipelineStateDesc.BlendState = CreateRendererBlendState(false);
    shadowPipelineStateDesc.SampleMask = UINT_MAX;
    shadowPipelineStateDesc.RasterizerState = shadowRasterizerState;
    shadowPipelineStateDesc.DepthStencilState.DepthEnable = TRUE;
    shadowPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadowPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadowPipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
    shadowPipelineStateDesc.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
    shadowPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowPipelineStateDesc.NumRenderTargets = 0;
    shadowPipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowPipelineStateDesc.SampleDesc.Count = 1;

    ThrowIfFailed(
        device.CreateGraphicsPipelineState(&shadowPipelineStateDesc, IID_PPV_ARGS(&pipelineStates.shadowPipelineState)),
        "ID3D12Device::CreateGraphicsPipelineState(shadow)");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC postProcessPipelineStateDesc = {};
    postProcessPipelineStateDesc.pRootSignature = &rootSignature;
    postProcessPipelineStateDesc.VS = ToShaderBytecode(shaders.postProcessVertexShader.Get());
    postProcessPipelineStateDesc.PS = ToShaderBytecode(shaders.postProcessPixelShader.Get());
    postProcessPipelineStateDesc.BlendState = CreateRendererBlendState(false);
    postProcessPipelineStateDesc.SampleMask = UINT_MAX;
    postProcessPipelineStateDesc.RasterizerState = CreateRendererRasterizerState(true);
    postProcessPipelineStateDesc.DepthStencilState.DepthEnable = FALSE;
    postProcessPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    postProcessPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    postProcessPipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
    postProcessPipelineStateDesc.InputLayout = {nullptr, 0};
    postProcessPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    postProcessPipelineStateDesc.NumRenderTargets = 1;
    postProcessPipelineStateDesc.RTVFormats[0] = kBackBufferFormat;
    postProcessPipelineStateDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    postProcessPipelineStateDesc.SampleDesc.Count = 1;

    ThrowIfFailed(
        device.CreateGraphicsPipelineState(
            &postProcessPipelineStateDesc,
            IID_PPV_ARGS(&pipelineStates.postProcessPipelineState)),
        "ID3D12Device::CreateGraphicsPipelineState(post process)");

    auto createFullscreenPipeline = [&](ID3DBlob* vertexShaderBlob,
                                        ID3DBlob* pixelShaderBlob,
                                        const DXGI_FORMAT renderTargetFormat,
                                        ComPtr<ID3D12PipelineState>& pipelineState)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {};
        pipelineStateDesc.pRootSignature = &rootSignature;
        pipelineStateDesc.VS = ToShaderBytecode(vertexShaderBlob);
        pipelineStateDesc.PS = ToShaderBytecode(pixelShaderBlob);
        pipelineStateDesc.BlendState = CreateRendererBlendState(false);
        pipelineStateDesc.SampleMask = UINT_MAX;
        pipelineStateDesc.RasterizerState = CreateRendererRasterizerState(true);
        pipelineStateDesc.DepthStencilState.DepthEnable = FALSE;
        pipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipelineStateDesc.DepthStencilState.StencilEnable = FALSE;
        pipelineStateDesc.InputLayout = {nullptr, 0};
        pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineStateDesc.NumRenderTargets = 1;
        pipelineStateDesc.RTVFormats[0] = renderTargetFormat;
        pipelineStateDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        pipelineStateDesc.SampleDesc.Count = 1;

        ThrowIfFailed(
            device.CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(&pipelineState)),
            "ID3D12Device::CreateGraphicsPipelineState(fullscreen)");
    };

    createFullscreenPipeline(
        shaders.bloomVertexShader.Get(),
        shaders.bloomPrefilterPixelShader.Get(),
        kBloomFormat,
        pipelineStates.bloomPrefilterPipelineState);
    createFullscreenPipeline(
        shaders.bloomVertexShader.Get(),
        shaders.bloomBlurHorizontalPixelShader.Get(),
        kBloomFormat,
        pipelineStates.bloomBlurHorizontalPipelineState);
    createFullscreenPipeline(
        shaders.bloomVertexShader.Get(),
        shaders.bloomBlurVerticalPixelShader.Get(),
        kBloomFormat,
        pipelineStates.bloomBlurVerticalPipelineState);

    return pipelineStates;
}
} // namespace

struct D3D12Renderer::FrameRenderContext
{
    D3D12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE sceneColorRtvHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE bloomPrefilterRtvHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE bloomPrefilterSrvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE bloomBlurTempRtvHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE bloomBlurTempSrvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE bloomResultRtvHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE bloomResultSrvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    D3D12_VIEWPORT shadowViewport = {};
    D3D12_VIEWPORT bloomViewport = {};
    D3D12_RECT shadowScissorRect = {};
    D3D12_RECT bloomScissorRect = {};
    std::array<float, 4> clearColor = {0.07f, 0.12f, 0.18f, 1.0f};
    std::vector<std::uint32_t> transparentDrawOrder;
    std::uint32_t currentPipelineStateIndex = std::numeric_limits<std::uint32_t>::max();
};

struct D3D12Renderer::ShaderSourceFile
{
    std::filesystem::path path;
    std::filesystem::file_time_type lastWriteTime = {};
    bool hasKnownWriteTime = false;
};

D3D12Renderer::D3D12Renderer(Window& window)
    : window_(window)
{
    EnableDebugLayerIfAvailable();
    CreateFactory();
    CreateDevice();
    CreateCommandObjects();
    CreateGpuProfilerResources();
    CreateSwapChain();
    CreateDescriptorHeap();
    CreateSceneConstants();
    CreateRenderTargets();
    CreateFence();
    CreatePipeline();
    InitializeShaderHotReloadTracking();
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
    ReleaseGraphPhysicalResources();

    if (fenceEvent_ != nullptr)
    {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

const RuntimeRenderSettings& D3D12Renderer::GetRuntimeRenderSettings() const noexcept
{
    return runtimeRenderSettings_;
}

const std::vector<RuntimeMaterialInfo>& D3D12Renderer::GetRuntimeMaterials() const noexcept
{
    return runtimeMaterials_;
}

void D3D12Renderer::SetRuntimeRenderSettings(const RuntimeRenderSettings& settings)
{
    runtimeRenderSettings_ = ClampRuntimeRenderSettings(settings);
}

bool D3D12Renderer::UpdateRuntimeMaterial(const std::uint32_t runtimeMaterialIndex, const MaterialDesc& desc)
{
    if (materialManager_ == nullptr)
    {
        return false;
    }

    const auto runtimeMaterialIterator =
        std::find_if(
            runtimeMaterials_.begin(),
            runtimeMaterials_.end(),
            [&](const RuntimeMaterialInfo& material)
            {
                return material.runtimeIndex == runtimeMaterialIndex;
            });
    if (runtimeMaterialIterator == runtimeMaterials_.end())
    {
        return false;
    }

    const MaterialDesc clampedDesc = ClampRuntimeMaterialDesc(desc);
    materialManager_->UpdateMaterial(runtimeMaterialIndex, clampedDesc);
    runtimeMaterialIterator->desc = clampedDesc;
    RebuildRenderItemMaterialBuckets();
    return true;
}

void D3D12Renderer::RequestRenderGraphSnapshotExport() noexcept
{
    renderGraphSnapshotExportRequested_ = true;
}

void D3D12Renderer::RequestScreenshotCapture() noexcept
{
    screenshotCaptureRequested_ = true;
}

const RenderGraphProfileSnapshot& D3D12Renderer::GetRenderGraphProfileSnapshot() const noexcept
{
    return renderGraphProfileSnapshot_;
}

bool D3D12Renderer::IsAutoShaderReloadEnabled() const noexcept
{
    return autoShaderReloadEnabled_;
}

void D3D12Renderer::SetAutoShaderReloadEnabled(const bool enabled)
{
    if (autoShaderReloadEnabled_ == enabled)
    {
        return;
    }

    autoShaderReloadEnabled_ = enabled;
    shaderReloadPending_ = false;
    shaderReloadDebounceSeconds_ = 0.0f;
    if (autoShaderReloadEnabled_)
    {
        RefreshShaderHotReloadWriteTimes();
        shaderReloadStatus_ = "Auto shader reload enabled.";
        Logger::Info(shaderReloadStatus_);
    }
    else
    {
        shaderReloadStatus_ = "Auto shader reload disabled.";
        Logger::Info(shaderReloadStatus_);
    }
}

const std::string& D3D12Renderer::GetShaderReloadStatus() const noexcept
{
    return shaderReloadStatus_;
}

void D3D12Renderer::Update(const float deltaTimeSeconds)
{
    UpdateCamera(deltaTimeSeconds);
    UpdatePostProcessDebugMode();
    UpdateShaderHotReload(deltaTimeSeconds);
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

    frameContext.backBufferRtvHandle = rtvAllocation_->GetCpuHandle(frameIndex_);
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
    frameContext.bloomViewport.TopLeftX = 0.0f;
    frameContext.bloomViewport.TopLeftY = 0.0f;
    frameContext.bloomViewport.Width = static_cast<float>(GetBloomWidth(static_cast<std::uint32_t>(width)));
    frameContext.bloomViewport.Height = static_cast<float>(GetBloomHeight(static_cast<std::uint32_t>(height)));
    frameContext.bloomViewport.MinDepth = 0.0f;
    frameContext.bloomViewport.MaxDepth = 1.0f;
    frameContext.bloomScissorRect.left = 0;
    frameContext.bloomScissorRect.top = 0;
    frameContext.bloomScissorRect.right = static_cast<LONG>(GetBloomWidth(static_cast<std::uint32_t>(width)));
    frameContext.bloomScissorRect.bottom = static_cast<LONG>(GetBloomHeight(static_cast<std::uint32_t>(height)));

    RenderGraph frameGraph = BuildFrameRenderGraph(frameContext);
    const RenderGraph::CompileResult compiledGraph = frameGraph.Compile();
    EnsureGraphPhysicalResources(compiledGraph);

    auto findCompiledResource = [&](const std::string_view resourceName)
        -> const RenderGraph::CompileResult::CompiledResource&
    {
        const auto resourceIterator = std::find_if(
            compiledGraph.resources.begin(),
            compiledGraph.resources.end(),
            [&](const RenderGraph::CompileResult::CompiledResource& compiledResource)
            {
                return compiledResource.name == resourceName;
            });
        if (resourceIterator == compiledGraph.resources.end())
        {
            std::ostringstream error;
            error << "RenderGraph resource '" << resourceName << "' was not compiled.";
            throw std::runtime_error(error.str());
        }

        return *resourceIterator;
    };

    auto bindGraphResource = [&](const std::string_view resourceName,
                                 D3D12_CPU_DESCRIPTOR_HANDLE* rtvHandle,
                                 D3D12_GPU_DESCRIPTOR_HANDLE* srvHandle,
                                 D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle)
    {
        const RenderGraph::CompileResult::CompiledResource& compiledResource = findCompiledResource(resourceName);
        if (compiledResource.physicalResourceIndex == kInvalidPhysicalResourceIndex)
        {
            std::ostringstream error;
            error << "RenderGraph resource '" << resourceName << "' has no physical resource allocation.";
            throw std::runtime_error(error.str());
        }

        const GraphPhysicalResource& graphPhysicalResource =
            graphPhysicalResources_.at(compiledResource.physicalResourceIndex);
        if (rtvHandle != nullptr)
        {
            if (!graphPhysicalResource.rtvAllocation.IsValid())
            {
                std::ostringstream error;
                error << "RenderGraph resource '" << resourceName << "' has no RTV allocation.";
                throw std::runtime_error(error.str());
            }

            *rtvHandle = graphPhysicalResource.rtvAllocation.GetCpuHandle();
        }
        if (srvHandle != nullptr)
        {
            if (!graphPhysicalResource.srvAllocation.IsValid())
            {
                std::ostringstream error;
                error << "RenderGraph resource '" << resourceName << "' has no SRV allocation.";
                throw std::runtime_error(error.str());
            }

            *srvHandle = graphPhysicalResource.srvAllocation.GetGpuHandle();
        }
        if (dsvHandle != nullptr)
        {
            if (!graphPhysicalResource.dsvAllocation.IsValid())
            {
                std::ostringstream error;
                error << "RenderGraph resource '" << resourceName << "' has no DSV allocation.";
                throw std::runtime_error(error.str());
            }

            *dsvHandle = graphPhysicalResource.dsvAllocation.GetCpuHandle();
        }
    };

    bindGraphResource("ShadowMap", nullptr, &frameContext.shadowSrvHandle, &frameContext.shadowDsvHandle);
    bindGraphResource("SceneDepth", nullptr, nullptr, &frameContext.dsvHandle);
    bindGraphResource("SceneColor", &frameContext.sceneColorRtvHandle, &frameContext.sceneColorSrvHandle, nullptr);
    bindGraphResource(
        "BloomPrefilter",
        &frameContext.bloomPrefilterRtvHandle,
        &frameContext.bloomPrefilterSrvHandle,
        nullptr);
    bindGraphResource(
        "BloomBlurTemp",
        &frameContext.bloomBlurTempRtvHandle,
        &frameContext.bloomBlurTempSrvHandle,
        nullptr);
    bindGraphResource(
        "BloomResult",
        &frameContext.bloomResultRtvHandle,
        &frameContext.bloomResultSrvHandle,
        nullptr);

    if (!renderGraphLogged_)
    {
        Logger::Info(frameGraph.Describe(compiledGraph));
        renderGraphLogged_ = true;
    }

    if (renderGraphSnapshotExportRequested_)
    {
        ExportRenderGraphSnapshot(frameGraph, compiledGraph);
        renderGraphSnapshotExportRequested_ = false;
    }

    const auto& registeredPasses = frameGraph.GetPasses();
    auto resolveGraphResource = [&](const std::string_view resourceName) -> ID3D12Resource*
    {
        if (resourceName == "BackBuffer")
        {
            return renderTargets_[frameIndex_].Get();
        }
        const RenderGraph::CompileResult::CompiledResource& compiledResource = findCompiledResource(resourceName);
        if (compiledResource.physicalResourceIndex != kInvalidPhysicalResourceIndex)
        {
            return graphPhysicalResources_.at(compiledResource.physicalResourceIndex).resource.Get();
        }

        return nullptr;
    };

    auto applyTransitions = [&](const std::vector<RenderGraph::CompiledPass::ResourceTransition>& transitions)
    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve(transitions.size());
        for (const RenderGraph::CompiledPass::ResourceTransition& transition : transitions)
        {
            ID3D12Resource* resource = resolveGraphResource(transition.resourceName);
            if (resource == nullptr)
            {
                std::ostringstream error;
                error << "No D3D12 resource mapping found for RenderGraph resource '"
                      << transition.resourceName << "'.";
                throw std::runtime_error(error.str());
            }

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = ToD3D12ResourceState(transition.beforeState);
            barrier.Transition.StateAfter = ToD3D12ResourceState(transition.afterState);
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers.push_back(barrier);
        }

        if (!barriers.empty())
        {
            commandList_->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }
    };

    const std::uint32_t profiledPassCount =
        static_cast<std::uint32_t>(compiledGraph.executionPassIndices.size());
    const bool shouldCaptureGpuTimings =
        !renderGraphGpuProfilingLogged_ ||
        renderFrameCounter_ % kGpuProfileSampleIntervalFrames == 0;
    const bool canCaptureGpuTimings =
        shouldCaptureGpuTimings && profiledPassCount > 0 && gpuTimestampQueryHeap_ != nullptr &&
        gpuTimestampReadbackBuffer_ != nullptr && gpuTimestampFrequency_ != 0 &&
        profiledPassCount <= kMaxGpuProfiledPassCount;
    if (!renderGraphGpuProfilingLogged_ && profiledPassCount > kMaxGpuProfiledPassCount)
    {
        Logger::Error("RenderGraph GPU pass timing skipped: pass count exceeds timestamp query capacity.");
        renderGraphGpuProfilingLogged_ = true;
    }

    std::vector<double> passCpuTimesMs;
    passCpuTimesMs.reserve(compiledGraph.executionPassIndices.size());
    {
        const GpuDebugEventScope renderGraphMarker(
            commandList_.Get(),
            "RenderGraph Frame",
            kRenderGraphFrameMarkerColor);
        for (std::size_t timingIndex = 0; timingIndex < compiledGraph.executionPassIndices.size(); ++timingIndex)
        {
            const std::uint32_t passIndex = compiledGraph.executionPassIndices[timingIndex];
            const RenderGraph::CompiledPass& compiledPass = compiledGraph.passes[passIndex];
            const GpuDebugEventScope passMarker(
                commandList_.Get(),
                GetRenderGraphPassMarkerLabel(compiledPass),
                GetRenderGraphPassMarkerColor(compiledPass.metadata.type));
            applyTransitions(compiledPass.transitions);

            if (canCaptureGpuTimings)
            {
                const UINT startQueryIndex = static_cast<UINT>(timingIndex * 2u);
                commandList_->EndQuery(gpuTimestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startQueryIndex);
            }

            const auto passStart = std::chrono::steady_clock::now();
            registeredPasses[compiledPass.sourcePassIndex].execute();
            const auto passEnd = std::chrono::steady_clock::now();
            passCpuTimesMs.push_back(std::chrono::duration<double, std::milli>(passEnd - passStart).count());

            if (canCaptureGpuTimings)
            {
                const UINT endQueryIndex = static_cast<UINT>(timingIndex * 2u + 1u);
                commandList_->EndQuery(gpuTimestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, endQueryIndex);
            }
        }
    }

    UpdateRenderGraphCpuTimingSnapshot(compiledGraph, passCpuTimesMs);

    if (canCaptureGpuTimings && profiledPassCount > 0)
    {
        commandList_->ResolveQueryData(
            gpuTimestampQueryHeap_.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            0,
            profiledPassCount * 2u,
            gpuTimestampReadbackBuffer_.Get(),
            0);
    }

    ComPtr<ID3D12Resource> screenshotReadbackResource;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT screenshotFootprint = {};
    std::uint32_t screenshotWidth = 0;
    std::uint32_t screenshotHeight = 0;
    if (screenshotCaptureRequested_)
    {
        screenshotCaptureRequested_ = false;
        try
        {
            ID3D12Resource* backBuffer = renderTargets_[frameIndex_].Get();
            const D3D12_RESOURCE_DESC backBufferDesc = backBuffer->GetDesc();
            if (backBufferDesc.Format != kBackBufferFormat)
            {
                Logger::Error("Screenshot capture only supports the current R8G8B8A8 back buffer format.");
            }
            else
            {
                UINT numRows = 0;
                UINT64 rowSizeInBytes = 0;
                UINT64 totalBytes = 0;
                device_->GetCopyableFootprints(
                    &backBufferDesc,
                    0,
                    1,
                    0,
                    &screenshotFootprint,
                    &numRows,
                    &rowSizeInBytes,
                    &totalBytes);
                (void)numRows;
                (void)rowSizeInBytes;

                D3D12_HEAP_PROPERTIES readbackHeapProperties = {};
                readbackHeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
                readbackHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                readbackHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                readbackHeapProperties.CreationNodeMask = 1;
                readbackHeapProperties.VisibleNodeMask = 1;

                D3D12_RESOURCE_DESC readbackResourceDesc = {};
                readbackResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                readbackResourceDesc.Alignment = 0;
                readbackResourceDesc.Width = totalBytes;
                readbackResourceDesc.Height = 1;
                readbackResourceDesc.DepthOrArraySize = 1;
                readbackResourceDesc.MipLevels = 1;
                readbackResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
                readbackResourceDesc.SampleDesc.Count = 1;
                readbackResourceDesc.SampleDesc.Quality = 0;
                readbackResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                readbackResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

                ThrowIfFailed(
                    device_->CreateCommittedResource(
                        &readbackHeapProperties,
                        D3D12_HEAP_FLAG_NONE,
                        &readbackResourceDesc,
                        D3D12_RESOURCE_STATE_COPY_DEST,
                        nullptr,
                        IID_PPV_ARGS(&screenshotReadbackResource)),
                    "ID3D12Device::CreateCommittedResource(screenshot readback)");

                D3D12_RESOURCE_BARRIER beforeCopyBarrier = {};
                beforeCopyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                beforeCopyBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                beforeCopyBarrier.Transition.pResource = backBuffer;
                beforeCopyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                beforeCopyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                beforeCopyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                commandList_->ResourceBarrier(1, &beforeCopyBarrier);

                D3D12_TEXTURE_COPY_LOCATION sourceLocation = {};
                sourceLocation.pResource = backBuffer;
                sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                sourceLocation.SubresourceIndex = 0;

                D3D12_TEXTURE_COPY_LOCATION destinationLocation = {};
                destinationLocation.pResource = screenshotReadbackResource.Get();
                destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                destinationLocation.PlacedFootprint = screenshotFootprint;

                commandList_->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);

                D3D12_RESOURCE_BARRIER afterCopyBarrier = beforeCopyBarrier;
                afterCopyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                afterCopyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                commandList_->ResourceBarrier(1, &afterCopyBarrier);

                screenshotWidth = static_cast<std::uint32_t>(backBufferDesc.Width);
                screenshotHeight = backBufferDesc.Height;
            }
        }
        catch (const std::exception& exception)
        {
            Logger::Error(std::string("Screenshot capture setup failed. ") + exception.what());
            screenshotReadbackResource.Reset();
        }
    }

    if (!renderGraphProfilingLogged_)
    {
        std::ostringstream timings;
        timings << "RenderGraph CPU pass timings (first frame):";
        for (const RenderGraphPassProfileTiming& passTiming : renderGraphProfileSnapshot_.passTimings)
        {
            timings << "\n  " << passTiming.passName << ": " << passTiming.cpuMilliseconds << " ms";
        }
        timings << "\n  Total: " << renderGraphProfileSnapshot_.totalCpuMilliseconds << " ms";
        Logger::Info(timings.str());
        renderGraphProfilingLogged_ = true;
    }

    ThrowIfFailed(commandList_->Close(), "ID3D12GraphicsCommandList::Close");

    ID3D12CommandList* commandLists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, commandLists);

    if (canCaptureGpuTimings || screenshotReadbackResource != nullptr)
    {
        WaitForFenceValue(Signal());
    }

    if (canCaptureGpuTimings)
    {
        ReadbackRenderGraphGpuTimingSnapshot(compiledGraph, profiledPassCount);
    }

    if (screenshotReadbackResource != nullptr)
    {
        ExportScreenshotCapture(
            *screenshotReadbackResource.Get(),
            screenshotFootprint,
            screenshotWidth,
            screenshotHeight);
    }

    ThrowIfFailed(swapChain_->Present(1, 0), "IDXGISwapChain::Present");
    MoveToNextFrame();
    ++renderFrameCounter_;
}

void D3D12Renderer::ExportRenderGraphSnapshot(
    const RenderGraph& frameGraph,
    const RenderGraph::CompileResult& compiledGraph)
{
    const std::filesystem::path textPath = GetWorkflowPath(L"render_graph_snapshot.txt");
    const std::filesystem::path dotPath = GetWorkflowPath(L"render_graph_snapshot.dot");

    const std::string textSnapshot = frameGraph.Describe(compiledGraph);
    const std::string dotSnapshot = frameGraph.DescribeGraphviz(compiledGraph);

    const bool textWritten = WriteTextFile(textPath, textSnapshot);
    const bool dotWritten = WriteTextFile(dotPath, dotSnapshot);

    if (textWritten && dotWritten)
    {
        Logger::Info(std::string("Exported Render Graph snapshot to: ") + textPath.string());
        Logger::Info(std::string("Exported Render Graph Graphviz to: ") + dotPath.string());
    }
}

void D3D12Renderer::ExportScreenshotCapture(
    ID3D12Resource& readbackResource,
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
    const std::uint32_t width,
    const std::uint32_t height)
{
    try
    {
        const std::wstring screenshotBaseName = MakeScreenshotBaseName();
        const std::filesystem::path screenshotPath =
            GetWorkflowPath(L"screenshots") / (screenshotBaseName + L".png");
        const std::filesystem::path settingsPath =
            GetWorkflowPath(L"screenshots") / (screenshotBaseName + L".cfg");

        std::vector<std::byte> rgbaPixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

        void* mappedData = nullptr;
        const D3D12_RANGE readRange = {
            static_cast<SIZE_T>(footprint.Offset),
            static_cast<SIZE_T>(footprint.Offset + static_cast<UINT64>(footprint.Footprint.RowPitch) * height)};
        ThrowIfFailed(readbackResource.Map(0, &readRange, &mappedData), "ID3D12Resource::Map(screenshot readback)");

        const auto* sourcePixels = static_cast<const std::byte*>(mappedData) + footprint.Offset;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            const std::byte* sourceRow =
                sourcePixels + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch;
            std::byte* destinationRow = rgbaPixels.data() + static_cast<std::size_t>(y) * width * 4u;
            std::copy_n(sourceRow, static_cast<std::size_t>(width) * 4u, destinationRow);
        }

        readbackResource.Unmap(0, nullptr);

        const bool screenshotWritten =
            SaveScreenshotPng(screenshotPath, width, height, std::span<const std::byte>(rgbaPixels), width * 4u);
        const bool settingsWritten = WriteTextFile(settingsPath, SerializeRuntimeRenderSettings(runtimeRenderSettings_));

        if (screenshotWritten && settingsWritten)
        {
            Logger::Info(std::string("Captured screenshot to: ") + screenshotPath.string());
            Logger::Info(std::string("Captured screenshot settings to: ") + settingsPath.string());
        }
    }
    catch (const std::exception& exception)
    {
        Logger::Error(std::string("Screenshot capture export failed. ") + exception.what());
    }
}

void D3D12Renderer::UpdateRenderGraphCpuTimingSnapshot(
    const RenderGraph::CompileResult& compiledGraph,
    const std::vector<double>& passCpuTimesMs)
{
    const std::vector<RenderGraphPassProfileTiming> previousTimings = renderGraphProfileSnapshot_.passTimings;
    const bool hadGpuTimings = renderGraphProfileSnapshot_.hasGpuTimings;
    bool preservedGpuTimings = hadGpuTimings;
    double preservedTotalGpuTimeMs = 0.0;

    renderGraphProfileSnapshot_.passTimings.clear();
    renderGraphProfileSnapshot_.passTimings.reserve(compiledGraph.executionPassIndices.size());
    renderGraphProfileSnapshot_.totalCpuMilliseconds = 0.0;
    renderGraphProfileSnapshot_.cpuFrameIndex = renderFrameCounter_;

    for (std::size_t timingIndex = 0; timingIndex < compiledGraph.executionPassIndices.size(); ++timingIndex)
    {
        const std::uint32_t passIndex = compiledGraph.executionPassIndices[timingIndex];
        RenderGraphPassProfileTiming passTiming = {};
        passTiming.passName = compiledGraph.passes[passIndex].name;
        if (timingIndex < passCpuTimesMs.size())
        {
            passTiming.cpuMilliseconds = passCpuTimesMs[timingIndex];
        }

        if (hadGpuTimings)
        {
            const auto previousTiming = std::find_if(
                previousTimings.begin(),
                previousTimings.end(),
                [&](const RenderGraphPassProfileTiming& timing)
                {
                    return timing.passName == passTiming.passName;
                });
            if (previousTiming != previousTimings.end())
            {
                passTiming.gpuMilliseconds = previousTiming->gpuMilliseconds;
                preservedTotalGpuTimeMs += passTiming.gpuMilliseconds;
            }
            else
            {
                preservedGpuTimings = false;
            }
        }

        renderGraphProfileSnapshot_.totalCpuMilliseconds += passTiming.cpuMilliseconds;
        renderGraphProfileSnapshot_.passTimings.push_back(std::move(passTiming));
    }

    if (preservedGpuTimings)
    {
        renderGraphProfileSnapshot_.totalGpuMilliseconds = preservedTotalGpuTimeMs;
        renderGraphProfileSnapshot_.hasGpuTimings = hadGpuTimings;
    }
    else
    {
        renderGraphProfileSnapshot_.totalGpuMilliseconds = 0.0;
        renderGraphProfileSnapshot_.gpuFrameIndex = 0;
        renderGraphProfileSnapshot_.hasGpuTimings = false;
    }
}

void D3D12Renderer::ReadbackRenderGraphGpuTimingSnapshot(
    const RenderGraph::CompileResult& compiledGraph,
    const std::uint32_t profiledPassCount)
{
    try
    {
        if (gpuTimestampReadbackBuffer_ == nullptr || gpuTimestampFrequency_ == 0 || profiledPassCount == 0)
        {
            return;
        }

        const std::uint32_t passCount = std::min(
            profiledPassCount,
            static_cast<std::uint32_t>(compiledGraph.executionPassIndices.size()));
        const SIZE_T readbackSize =
            static_cast<SIZE_T>(passCount) * 2u * sizeof(std::uint64_t);

        void* mappedData = nullptr;
        const D3D12_RANGE readRange = {0, readbackSize};
        ThrowIfFailed(
            gpuTimestampReadbackBuffer_->Map(0, &readRange, &mappedData),
            "ID3D12Resource::Map(timestamp readback)");

        const auto* timestamps = static_cast<const std::uint64_t*>(mappedData);
        double totalGpuTimeMs = 0.0;
        std::ostringstream timings;
        timings << std::fixed << std::setprecision(3);
        timings << "RenderGraph GPU pass timings (first frame):";
        for (std::uint32_t timingIndex = 0; timingIndex < passCount; ++timingIndex)
        {
            const std::uint64_t beginTimestamp = timestamps[timingIndex * 2u];
            const std::uint64_t endTimestamp = timestamps[timingIndex * 2u + 1u];
            const std::uint64_t elapsedTicks =
                endTimestamp >= beginTimestamp ? endTimestamp - beginTimestamp : 0u;
            const double elapsedMs =
                static_cast<double>(elapsedTicks) * 1000.0 / static_cast<double>(gpuTimestampFrequency_);

            const std::uint32_t passIndex = compiledGraph.executionPassIndices[timingIndex];
            totalGpuTimeMs += elapsedMs;
            if (timingIndex < renderGraphProfileSnapshot_.passTimings.size())
            {
                renderGraphProfileSnapshot_.passTimings[timingIndex].passName = compiledGraph.passes[passIndex].name;
                renderGraphProfileSnapshot_.passTimings[timingIndex].gpuMilliseconds = elapsedMs;
            }
            timings << "\n  " << compiledGraph.passes[passIndex].name << ": " << elapsedMs << " ms";
        }
        timings << "\n  Total: " << totalGpuTimeMs << " ms";

        renderGraphProfileSnapshot_.totalGpuMilliseconds = totalGpuTimeMs;
        renderGraphProfileSnapshot_.gpuFrameIndex = renderFrameCounter_;
        renderGraphProfileSnapshot_.hasGpuTimings = true;

        const D3D12_RANGE writeRange = {0, 0};
        gpuTimestampReadbackBuffer_->Unmap(0, &writeRange);

        if (!renderGraphGpuProfilingLogged_)
        {
            Logger::Info(timings.str());
            renderGraphGpuProfilingLogged_ = true;
        }
    }
    catch (const std::exception& exception)
    {
        Logger::Error(std::string("RenderGraph GPU pass timing readback failed. ") + exception.what());
    }
}

RenderGraph D3D12Renderer::BuildFrameRenderGraph(FrameRenderContext& context)
{
    const std::uint32_t sceneWidth = std::max(window_.GetClientWidth(), 1u);
    const std::uint32_t sceneHeight = std::max(window_.GetClientHeight(), 1u);
    const std::uint32_t bloomWidth = GetBloomWidth(sceneWidth);
    const std::uint32_t bloomHeight = GetBloomHeight(sceneHeight);
    const auto& environmentTexture = textureManager_->GetTextureAsset(environmentTextureIndex_);
    const auto& iblDiffuseTexture = textureManager_->GetTextureAsset(iblDiffuseTextureIndex_);
    const auto& iblSpecularTexture = textureManager_->GetTextureAsset(iblSpecularTextureIndex_);

    auto sceneColorDesc = RenderGraph::ResourceDesc::Texture2D(sceneWidth, sceneHeight, kSceneColorFormat);
    sceneColorDesc.AllowShaderRead().AllowRenderTarget(context.clearColor);
    auto backBufferDesc = RenderGraph::ResourceDesc::Texture2D(sceneWidth, sceneHeight, kBackBufferFormat);
    backBufferDesc.AllowRenderTarget(context.clearColor);
    auto sceneDepthDesc = RenderGraph::ResourceDesc::Texture2D(sceneWidth, sceneHeight, DXGI_FORMAT_D32_FLOAT);
    sceneDepthDesc.AllowDepthStencil();
    auto shadowMapDesc = RenderGraph::ResourceDesc::Texture2D(kShadowMapSize, kShadowMapSize, DXGI_FORMAT_R32_TYPELESS);
    shadowMapDesc.AllowShaderRead().SetShaderReadFormat(DXGI_FORMAT_R32_FLOAT);
    shadowMapDesc.AllowDepthStencil().SetDepthStencilFormat(DXGI_FORMAT_D32_FLOAT);
    auto environmentTextureDesc = RenderGraph::ResourceDesc::Texture2D(
        environmentTexture.texture->GetWidth(),
        environmentTexture.texture->GetHeight(),
        environmentTexture.texture->GetFormat());
    environmentTextureDesc.AllowShaderRead();
    auto iblDiffuseDesc = RenderGraph::ResourceDesc::Texture2D(
        iblDiffuseTexture.texture->GetWidth(),
        iblDiffuseTexture.texture->GetHeight(),
        iblDiffuseTexture.texture->GetFormat());
    iblDiffuseDesc.AllowShaderRead();
    auto iblSpecularDesc = RenderGraph::ResourceDesc::Texture2D(
        iblSpecularTexture.texture->GetWidth(),
        iblSpecularTexture.texture->GetHeight(),
        iblSpecularTexture.texture->GetFormat());
    iblSpecularDesc.AllowShaderRead();
    auto bloomDesc = RenderGraph::ResourceDesc::Texture2D(bloomWidth, bloomHeight, kBloomFormat);
    bloomDesc.AllowShaderRead().AllowRenderTarget({0.0f, 0.0f, 0.0f, 1.0f});

    RenderGraph frameGraph;
    frameGraph.ImportResource("EnvironmentTexture", environmentTextureDesc, RenderGraph::ResourceState::ShaderRead);
    frameGraph.ImportResource("IblDiffuseTexture", iblDiffuseDesc, RenderGraph::ResourceState::ShaderRead);
    frameGraph.ImportResource("IblSpecularTexture", iblSpecularDesc, RenderGraph::ResourceState::ShaderRead);
    frameGraph.ImportResource("BackBuffer", backBufferDesc, RenderGraph::ResourceState::Present);
    frameGraph.DeclareTransientResource("ShadowMap", shadowMapDesc, RenderGraph::ResourceState::ShaderRead);
    frameGraph.DeclareTransientResource("SceneDepth", sceneDepthDesc, RenderGraph::ResourceState::DepthWrite);
    frameGraph.DeclareTransientResource("SceneColor", sceneColorDesc, RenderGraph::ResourceState::ShaderRead);
    frameGraph.DeclareTransientResource("BloomPrefilter", bloomDesc, RenderGraph::ResourceState::ShaderRead);
    frameGraph.DeclareTransientResource("BloomBlurTemp", bloomDesc, RenderGraph::ResourceState::ShaderRead);
    frameGraph.DeclareTransientResource("BloomResult", bloomDesc, RenderGraph::ResourceState::ShaderRead);
    frameGraph.ExportResource("BackBuffer");
    frameGraph.AddPass(
        "ShadowDepth",
        {RenderGraph::Write("ShadowMap", RenderGraph::ResourceState::DepthWrite)},
        RenderGraph::PassMetadata::Graphics("Shadow Depth"),
        [this, &context]()
        {
            RecordShadowDepthPass(context);
        });
    frameGraph.AddPass(
        "MainColorBegin",
        {
            RenderGraph::Write("SceneColor", RenderGraph::ResourceState::RenderTarget),
            RenderGraph::Write("SceneDepth", RenderGraph::ResourceState::DepthWrite),
        },
        RenderGraph::PassMetadata::Graphics("Main Color Begin"),
        [this, &context]()
        {
            RecordMainColorBeginPass(context);
        });
    frameGraph.AddPass(
        "Skybox",
        {
            RenderGraph::Read("EnvironmentTexture", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Write("SceneColor", RenderGraph::ResourceState::RenderTarget),
        },
        RenderGraph::PassMetadata::Fullscreen("Skybox"),
        [this]()
        {
            RecordSkyboxPass();
        });
    frameGraph.AddPass(
        "OpaqueGeometry",
        {
            RenderGraph::Read("ShadowMap", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Read("EnvironmentTexture", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Read("IblDiffuseTexture", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Read("IblSpecularTexture", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Write("SceneColor", RenderGraph::ResourceState::RenderTarget),
            RenderGraph::ReadWrite("SceneDepth", RenderGraph::ResourceState::DepthWrite),
        },
        RenderGraph::PassMetadata::Graphics("Opaque Geometry"),
        [this, &context]()
        {
            RecordOpaqueGeometryPass(context);
        });
    frameGraph.AddPass(
        "TransparentGeometry",
        {
            RenderGraph::Read("ShadowMap", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Read("EnvironmentTexture", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Read("IblDiffuseTexture", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Read("IblSpecularTexture", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Read("SceneDepth", RenderGraph::ResourceState::DepthWrite),
            RenderGraph::ReadWrite("SceneColor", RenderGraph::ResourceState::RenderTarget),
        },
        RenderGraph::PassMetadata::Graphics("Transparent Geometry"),
        [this, &context]()
        {
            RecordTransparentGeometryPass(context);
        });
    frameGraph.AddPass(
        "BloomPrefilter",
        {
            RenderGraph::Read("SceneColor", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Write("BloomPrefilter", RenderGraph::ResourceState::RenderTarget),
        },
        RenderGraph::PassMetadata::Fullscreen("Bloom Prefilter"),
        [this, &context]()
        {
            RecordBloomPrefilterPass(context);
        });
    frameGraph.AddPass(
        "BloomBlurHorizontal",
        {
            RenderGraph::Read("BloomPrefilter", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Write("BloomBlurTemp", RenderGraph::ResourceState::RenderTarget),
        },
        RenderGraph::PassMetadata::Fullscreen("Bloom Blur H"),
        [this, &context]()
        {
            RecordBloomBlurHorizontalPass(context);
        });
    frameGraph.AddPass(
        "BloomBlurVertical",
        {
            RenderGraph::Read("BloomBlurTemp", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Write("BloomResult", RenderGraph::ResourceState::RenderTarget),
        },
        RenderGraph::PassMetadata::Fullscreen("Bloom Blur V"),
        [this, &context]()
        {
            RecordBloomBlurVerticalPass(context);
        });
    frameGraph.AddPass(
        "PostProcess",
        {
            RenderGraph::Read("SceneColor", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Read("BloomResult", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Read("ShadowMap", RenderGraph::ResourceState::ShaderRead),
            RenderGraph::Write("BackBuffer", RenderGraph::ResourceState::RenderTarget),
        },
        RenderGraph::PassMetadata::Fullscreen("ToneMap Composite"),
        [this, &context]()
        {
            RecordPostProcessPass(context);
        });
    frameGraph.AddPass(
        "PresentTransition",
        {RenderGraph::Read("BackBuffer", RenderGraph::ResourceState::Present)},
        RenderGraph::PassMetadata::Present("Present Transition"),
        [this, &context]()
        {
            RecordPresentTransitionPass(context);
        });
    return frameGraph;
}

void D3D12Renderer::RecordShadowDepthPass(const FrameRenderContext& context)
{
    BindCommonGraphicsState();
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
}

void D3D12Renderer::RecordMainColorBeginPass(const FrameRenderContext& context)
{
    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);
    commandList_->OMSetRenderTargets(1, &context.sceneColorRtvHandle, FALSE, &context.dsvHandle);
    commandList_->ClearRenderTargetView(context.sceneColorRtvHandle, context.clearColor.data(), 0, nullptr);
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
        DrawRenderItem(renderItems_[renderItemIndex], context.currentPipelineStateIndex, context.shadowSrvHandle);
    }
}

void D3D12Renderer::RecordTransparentGeometryPass(FrameRenderContext& context)
{
    for (const std::uint32_t renderItemIndex : context.transparentDrawOrder)
    {
        DrawRenderItem(renderItems_[renderItemIndex], context.currentPipelineStateIndex, context.shadowSrvHandle);
    }
}

void D3D12Renderer::RecordBloomPrefilterPass(const FrameRenderContext& context)
{
    commandList_->RSSetViewports(1, &context.bloomViewport);
    commandList_->RSSetScissorRects(1, &context.bloomScissorRect);
    commandList_->OMSetRenderTargets(1, &context.bloomPrefilterRtvHandle, FALSE, nullptr);
    commandList_->ClearRenderTargetView(context.bloomPrefilterRtvHandle, context.clearColor.data(), 0, nullptr);
    BindCommonGraphicsState();
    commandList_->SetPipelineState(bloomPrefilterPipelineState_.Get());
    commandList_->SetGraphicsRootDescriptorTable(0, sceneCbvAllocation_.GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(8, context.sceneColorSrvHandle);
    commandList_->DrawInstanced(3, 1, 0, 0);
}

void D3D12Renderer::RecordBloomBlurHorizontalPass(const FrameRenderContext& context)
{
    commandList_->RSSetViewports(1, &context.bloomViewport);
    commandList_->RSSetScissorRects(1, &context.bloomScissorRect);
    commandList_->OMSetRenderTargets(1, &context.bloomBlurTempRtvHandle, FALSE, nullptr);
    commandList_->ClearRenderTargetView(context.bloomBlurTempRtvHandle, context.clearColor.data(), 0, nullptr);
    BindCommonGraphicsState();
    commandList_->SetPipelineState(bloomBlurHorizontalPipelineState_.Get());
    commandList_->SetGraphicsRootDescriptorTable(0, sceneCbvAllocation_.GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(8, context.bloomPrefilterSrvHandle);
    commandList_->DrawInstanced(3, 1, 0, 0);
}

void D3D12Renderer::RecordBloomBlurVerticalPass(const FrameRenderContext& context)
{
    commandList_->RSSetViewports(1, &context.bloomViewport);
    commandList_->RSSetScissorRects(1, &context.bloomScissorRect);
    commandList_->OMSetRenderTargets(1, &context.bloomResultRtvHandle, FALSE, nullptr);
    commandList_->ClearRenderTargetView(context.bloomResultRtvHandle, context.clearColor.data(), 0, nullptr);
    BindCommonGraphicsState();
    commandList_->SetPipelineState(bloomBlurVerticalPipelineState_.Get());
    commandList_->SetGraphicsRootDescriptorTable(0, sceneCbvAllocation_.GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(8, context.bloomBlurTempSrvHandle);
    commandList_->DrawInstanced(3, 1, 0, 0);
}

void D3D12Renderer::RecordPostProcessPass(const FrameRenderContext& context)
{
    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);
    commandList_->OMSetRenderTargets(1, &context.backBufferRtvHandle, FALSE, nullptr);
    BindCommonGraphicsState();
    commandList_->SetPipelineState(postProcessPipelineState_.Get());
    commandList_->SetGraphicsRootDescriptorTable(0, sceneCbvAllocation_.GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(8, context.sceneColorSrvHandle);
    commandList_->SetGraphicsRootDescriptorTable(9, context.bloomResultSrvHandle);
    commandList_->SetGraphicsRootDescriptorTable(11, context.shadowSrvHandle);
    commandList_->DrawInstanced(3, 1, 0, 0);
}

void D3D12Renderer::RecordPresentTransitionPass(const FrameRenderContext& context)
{
    (void)context;
}

void D3D12Renderer::BindCommonGraphicsState()
{
    ID3D12DescriptorHeap* descriptorHeaps[] = {cbvAllocator_->GetHeap()};
    commandList_->SetDescriptorHeaps(static_cast<UINT>(std::size(descriptorHeaps)), descriptorHeaps);
    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void D3D12Renderer::DrawRenderItem(
    RenderItem& renderItem,
    std::uint32_t& currentPipelineStateIndex,
    const D3D12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle)
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
    commandList_->SetGraphicsRootDescriptorTable(
        9,
        textureManager_
            ->GetSrvAllocation(textureManager_->ResolveTextureIndex(iblDiffuseTextureIndex_, DefaultTextureKind::Black))
            .GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(
        10,
        textureManager_
            ->GetSrvAllocation(textureManager_->ResolveTextureIndex(iblSpecularTextureIndex_, DefaultTextureKind::Black))
            .GetGpuHandle());
    commandList_->SetGraphicsRootDescriptorTable(11, shadowSrvHandle);
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
    ReleaseGraphPhysicalResources();

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

void D3D12Renderer::CreateGpuProfilerResources()
{
    try
    {
        ThrowIfFailed(
            commandQueue_->GetTimestampFrequency(&gpuTimestampFrequency_),
            "ID3D12CommandQueue::GetTimestampFrequency");

        D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryHeapDesc.Count = kGpuTimestampQueryCount;
        queryHeapDesc.NodeMask = 0;

        ThrowIfFailed(
            device_->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&gpuTimestampQueryHeap_)),
            "ID3D12Device::CreateQueryHeap(timestamp)");

        D3D12_HEAP_PROPERTIES readbackHeapProperties = {};
        readbackHeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
        readbackHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        readbackHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        readbackHeapProperties.CreationNodeMask = 1;
        readbackHeapProperties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC readbackResourceDesc = {};
        readbackResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackResourceDesc.Alignment = 0;
        readbackResourceDesc.Width = static_cast<UINT64>(kGpuTimestampQueryCount) * sizeof(std::uint64_t);
        readbackResourceDesc.Height = 1;
        readbackResourceDesc.DepthOrArraySize = 1;
        readbackResourceDesc.MipLevels = 1;
        readbackResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        readbackResourceDesc.SampleDesc.Count = 1;
        readbackResourceDesc.SampleDesc.Quality = 0;
        readbackResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        readbackResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ThrowIfFailed(
            device_->CreateCommittedResource(
                &readbackHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &readbackResourceDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&gpuTimestampReadbackBuffer_)),
            "ID3D12Device::CreateCommittedResource(timestamp readback)");

        Logger::Info("Initialized Render Graph GPU timestamp profiler.");
    }
    catch (const std::exception& exception)
    {
        gpuTimestampQueryHeap_.Reset();
        gpuTimestampReadbackBuffer_.Reset();
        gpuTimestampFrequency_ = 0;
        renderGraphGpuProfilingLogged_ = true;
        Logger::Error(std::string("Render Graph GPU timestamp profiler disabled. ") + exception.what());
    }
}

void D3D12Renderer::CreateSwapChain()
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = window_.GetClientWidth();
    swapChainDesc.Height = window_.GetClientHeight();
    swapChainDesc.Format = kBackBufferFormat;
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
    rtvAllocator_->Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kFrameCount + 8, false);
    rtvAllocation_ = std::make_unique<DescriptorAllocation>(rtvAllocator_->Allocate(kFrameCount));

    dsvAllocator_ = std::make_unique<DescriptorAllocator>();
    dsvAllocator_->Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 8, false);

    cbvAllocator_ = std::make_unique<DescriptorAllocator>();
    cbvAllocator_->Initialize(*device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 192, true);
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

void D3D12Renderer::EnsureGraphPhysicalResources(const RenderGraph::CompileResult& compileResult)
{
    if (graphPhysicalResources_.size() < compileResult.physicalResources.size())
    {
        graphPhysicalResources_.resize(compileResult.physicalResources.size());
    }

    for (const RenderGraph::CompileResult::PhysicalResource& physicalResource : compileResult.physicalResources)
    {
        GraphPhysicalResource& graphPhysicalResource =
            graphPhysicalResources_.at(physicalResource.physicalResourceIndex);

        if (!(graphPhysicalResource.desc == physicalResource.desc) || graphPhysicalResource.resource == nullptr)
        {
            graphPhysicalResource.desc = physicalResource.desc;
            graphPhysicalResource.resource.Reset();

            if (graphPhysicalResource.desc.dimension != RenderGraph::ResourceDimension::Texture2D)
            {
                throw std::runtime_error("RenderGraph physical resource allocation only supports Texture2D.");
            }

            D3D12_HEAP_PROPERTIES heapProperties = {};
            heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
            heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            heapProperties.CreationNodeMask = 1;
            heapProperties.VisibleNodeMask = 1;

            D3D12_RESOURCE_DESC resourceDesc = {};
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            resourceDesc.Alignment = 0;
            resourceDesc.Width = graphPhysicalResource.desc.width;
            resourceDesc.Height = graphPhysicalResource.desc.height;
            resourceDesc.DepthOrArraySize = 1;
            resourceDesc.MipLevels = 1;
            resourceDesc.Format = graphPhysicalResource.desc.format;
            resourceDesc.SampleDesc.Count = 1;
            resourceDesc.SampleDesc.Quality = 0;
            resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            if (HasBindFlag(graphPhysicalResource.desc.bindFlags, RenderGraph::ResourceBindFlags::RenderTarget))
            {
                resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            }
            if (HasBindFlag(graphPhysicalResource.desc.bindFlags, RenderGraph::ResourceBindFlags::DepthStencil))
            {
                resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                if (!HasBindFlag(graphPhysicalResource.desc.bindFlags, RenderGraph::ResourceBindFlags::ShaderRead))
                {
                    resourceDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
                }
            }

            D3D12_CLEAR_VALUE clearValue = {};
            D3D12_CLEAR_VALUE* clearValuePointer = nullptr;
            if (graphPhysicalResource.desc.hasClearValue)
            {
                clearValuePointer = &clearValue;
                if (HasBindFlag(graphPhysicalResource.desc.bindFlags, RenderGraph::ResourceBindFlags::DepthStencil))
                {
                    clearValue.Format = graphPhysicalResource.desc.depthStencilFormat != DXGI_FORMAT_UNKNOWN
                        ? graphPhysicalResource.desc.depthStencilFormat
                        : graphPhysicalResource.desc.format;
                    clearValue.DepthStencil.Depth = graphPhysicalResource.desc.clearDepth;
                    clearValue.DepthStencil.Stencil = graphPhysicalResource.desc.clearStencil;
                }
                else
                {
                    clearValue.Format = graphPhysicalResource.desc.renderTargetFormat != DXGI_FORMAT_UNKNOWN
                        ? graphPhysicalResource.desc.renderTargetFormat
                        : graphPhysicalResource.desc.format;
                    clearValue.Color[0] = graphPhysicalResource.desc.clearColor[0];
                    clearValue.Color[1] = graphPhysicalResource.desc.clearColor[1];
                    clearValue.Color[2] = graphPhysicalResource.desc.clearColor[2];
                    clearValue.Color[3] = graphPhysicalResource.desc.clearColor[3];
                }
            }

            ThrowIfFailed(
                device_->CreateCommittedResource(
                    &heapProperties,
                    D3D12_HEAP_FLAG_NONE,
                    &resourceDesc,
                    ToD3D12ResourceState(physicalResource.initialState),
                    clearValuePointer,
                    IID_PPV_ARGS(&graphPhysicalResource.resource)),
                "ID3D12Device::CreateCommittedResource(render graph transient)");
        }

        if (HasBindFlag(graphPhysicalResource.desc.bindFlags, RenderGraph::ResourceBindFlags::RenderTarget))
        {
            if (!graphPhysicalResource.rtvAllocation.IsValid())
            {
                graphPhysicalResource.rtvAllocation = rtvAllocator_->Allocate(1);
            }

            D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {};
            renderTargetViewDesc.Format = graphPhysicalResource.desc.renderTargetFormat != DXGI_FORMAT_UNKNOWN
                ? graphPhysicalResource.desc.renderTargetFormat
                : graphPhysicalResource.desc.format;
            renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            renderTargetViewDesc.Texture2D.MipSlice = 0;
            renderTargetViewDesc.Texture2D.PlaneSlice = 0;
            device_->CreateRenderTargetView(
                graphPhysicalResource.resource.Get(),
                &renderTargetViewDesc,
                graphPhysicalResource.rtvAllocation.GetCpuHandle());
        }

        if (HasBindFlag(graphPhysicalResource.desc.bindFlags, RenderGraph::ResourceBindFlags::ShaderRead))
        {
            if (!graphPhysicalResource.srvAllocation.IsValid())
            {
                graphPhysicalResource.srvAllocation = cbvAllocator_->Allocate(1);
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
            shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            shaderResourceViewDesc.Format = graphPhysicalResource.desc.shaderReadFormat != DXGI_FORMAT_UNKNOWN
                ? graphPhysicalResource.desc.shaderReadFormat
                : graphPhysicalResource.desc.format;
            shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
            shaderResourceViewDesc.Texture2D.MipLevels = 1;
            shaderResourceViewDesc.Texture2D.PlaneSlice = 0;
            shaderResourceViewDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            device_->CreateShaderResourceView(
                graphPhysicalResource.resource.Get(),
                &shaderResourceViewDesc,
                graphPhysicalResource.srvAllocation.GetCpuHandle());
        }

        if (HasBindFlag(graphPhysicalResource.desc.bindFlags, RenderGraph::ResourceBindFlags::DepthStencil))
        {
            if (!graphPhysicalResource.dsvAllocation.IsValid())
            {
                graphPhysicalResource.dsvAllocation = dsvAllocator_->Allocate(1);
            }

            D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = {};
            depthStencilViewDesc.Format = graphPhysicalResource.desc.depthStencilFormat != DXGI_FORMAT_UNKNOWN
                ? graphPhysicalResource.desc.depthStencilFormat
                : graphPhysicalResource.desc.format;
            depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            depthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;
            device_->CreateDepthStencilView(
                graphPhysicalResource.resource.Get(),
                &depthStencilViewDesc,
                graphPhysicalResource.dsvAllocation.GetCpuHandle());
        }
    }
}

void D3D12Renderer::ReleaseGraphPhysicalResources() noexcept
{
    for (GraphPhysicalResource& graphPhysicalResource : graphPhysicalResources_)
    {
        graphPhysicalResource.resource.Reset();
    }
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
    const CompiledRendererShaders shaders = CompileRendererShaders();
    if (rootSignature_ == nullptr)
    {
        rootSignature_ = CreateRendererRootSignature(*device_.Get());
    }

    const PipelineStateCollection pipelineStates =
        CreateRendererPipelineStates(*device_.Get(), *rootSignature_.Get(), shaders);
    pipelineStates_ = pipelineStates.materialPipelineStates;
    skyboxPipelineState_ = pipelineStates.skyboxPipelineState;
    shadowPipelineState_ = pipelineStates.shadowPipelineState;
    bloomPrefilterPipelineState_ = pipelineStates.bloomPrefilterPipelineState;
    bloomBlurHorizontalPipelineState_ = pipelineStates.bloomBlurHorizontalPipelineState;
    bloomBlurVerticalPipelineState_ = pipelineStates.bloomBlurVerticalPipelineState;
    postProcessPipelineState_ = pipelineStates.postProcessPipelineState;
}

bool D3D12Renderer::ReloadShaders()
{
    Logger::Info("Reloading renderer shaders.");
    shaderReloadStatus_ = "Reloading renderer shaders...";

    try
    {
        WaitForIdle();

        const CompiledRendererShaders shaders = CompileRendererShaders();
        if (rootSignature_ == nullptr)
        {
            rootSignature_ = CreateRendererRootSignature(*device_.Get());
        }

        const PipelineStateCollection pipelineStates =
            CreateRendererPipelineStates(*device_.Get(), *rootSignature_.Get(), shaders);
        pipelineStates_ = pipelineStates.materialPipelineStates;
        skyboxPipelineState_ = pipelineStates.skyboxPipelineState;
        shadowPipelineState_ = pipelineStates.shadowPipelineState;
        bloomPrefilterPipelineState_ = pipelineStates.bloomPrefilterPipelineState;
        bloomBlurHorizontalPipelineState_ = pipelineStates.bloomBlurHorizontalPipelineState;
        bloomBlurVerticalPipelineState_ = pipelineStates.bloomBlurVerticalPipelineState;
        postProcessPipelineState_ = pipelineStates.postProcessPipelineState;

        RefreshShaderHotReloadWriteTimes();
        shaderReloadStatus_ = "Renderer shader reload succeeded.";
        Logger::Info("Renderer shader reload succeeded.");
        return true;
    }
    catch (const std::exception& exception)
    {
        RefreshShaderHotReloadWriteTimes();
        shaderReloadStatus_ = "Renderer shader reload failed; previous pipeline states remain active.";
        Logger::Error(std::string("Renderer shader reload failed; keeping previous pipeline states. ") + exception.what());
    }
    catch (...)
    {
        RefreshShaderHotReloadWriteTimes();
        shaderReloadStatus_ = "Renderer shader reload failed; previous pipeline states remain active.";
        Logger::Error("Renderer shader reload failed; keeping previous pipeline states.");
    }

    return false;
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
    iblDiffuseTextureIndex_ = kInvalidTextureIndex;
    iblSpecularTextureIndex_ = kInvalidTextureIndex;

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

    auto createTextureFromMemory = [&](const ImageData& imageData, const std::filesystem::path& sourcePath)
    {
        auto& commandAllocator = commandAllocators_[frameIndex_];
        ThrowIfFailed(commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
        ThrowIfFailed(commandList_->Reset(commandAllocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset");
        const std::uint32_t index = textureManager_->CreateFromMemory(
            *commandList_.Get(),
            std::span(imageData.pixels),
            imageData.width,
            imageData.height,
            imageData.format,
            sourcePath);
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

    const std::filesystem::path environmentPath = GetAssetPath(L"textures/environment_panorama.png");
    environmentTextureIndex_ = loadTexture(environmentPath);

    const ImageData environmentImage = ImageLoader::LoadRgba8(environmentPath);
    const ImageData diffuseIblImage = CreateFilteredPanorama(environmentImage, 64, 32, 5, 3);
    const ImageData specularIblImage = CreateFilteredPanorama(environmentImage, 256, 128, 3, 2);
    iblDiffuseTextureIndex_ = createTextureFromMemory(diffuseIblImage, L"__ibl_diffuse_panorama");
    iblSpecularTextureIndex_ = createTextureFromMemory(specularIblImage, L"__ibl_specular_panorama");
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
    runtimeMaterials_.clear();
    runtimeMaterials_.reserve(std::max<std::size_t>(sceneDocument_->materials.size(), 1));

    for (std::uint32_t gltfMaterialIndex = 0; gltfMaterialIndex < sceneDocument_->materials.size(); ++gltfMaterialIndex)
    {
        const GltfMaterial& gltfMaterial = sceneDocument_->materials[gltfMaterialIndex];
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
        material = ClampRuntimeMaterialDesc(material);

        const std::string runtimeMaterialName = BuildRuntimeMaterialName(gltfMaterial.name, gltfMaterialIndex);
        const std::uint32_t runtimeMaterialIndex = materialManager_->CreateMaterial(material, runtimeMaterialName);
        runtimeMaterialIndices_.push_back(runtimeMaterialIndex);
        runtimeMaterials_.push_back({runtimeMaterialIndex, runtimeMaterialName, material});
    }

    if (runtimeMaterialIndices_.empty())
    {
        MaterialDesc defaultMaterial = ClampRuntimeMaterialDesc(MaterialDesc {});
        constexpr std::uint32_t defaultMaterialIndex = 0;
        const std::string runtimeMaterialName = BuildRuntimeMaterialName("", defaultMaterialIndex);
        const std::uint32_t runtimeMaterialIndex = materialManager_->CreateMaterial(defaultMaterial, runtimeMaterialName);
        runtimeMaterialIndices_.push_back(runtimeMaterialIndex);
        runtimeMaterials_.push_back({runtimeMaterialIndex, runtimeMaterialName, defaultMaterial});
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

    RebuildRenderItemMaterialBuckets();
}

void D3D12Renderer::RebuildRenderItemMaterialBuckets()
{
    opaqueRenderItemIndices_.clear();
    transparentRenderItemIndices_.clear();
    opaqueRenderItemIndices_.reserve(renderItems_.size());
    transparentRenderItemIndices_.reserve(renderItems_.size());

    if (materialManager_ == nullptr)
    {
        return;
    }

    for (std::uint32_t renderItemIndex = 0; renderItemIndex < renderItems_.size(); ++renderItemIndex)
    {
        const auto& material = materialManager_->GetMaterial(renderItems_[renderItemIndex].materialIndex);
        if (IsBlendAlphaMode(material.desc.alphaMode))
        {
            transparentRenderItemIndices_.push_back(renderItemIndex);
        }
        else
        {
            opaqueRenderItemIndices_.push_back(renderItemIndex);
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

void D3D12Renderer::UpdatePostProcessDebugMode()
{
    struct DebugModeBinding
    {
        std::uint32_t virtualKey = 0;
        PostProcessDebugView mode = PostProcessDebugView::Final;
        const char* label = "";
    };

    constexpr std::array<DebugModeBinding, 8> bindings = {
        DebugModeBinding {'1', PostProcessDebugView::Final, "Final"},
        DebugModeBinding {'2', PostProcessDebugView::HdrScene, "HDR Scene"},
        DebugModeBinding {'3', PostProcessDebugView::Bloom, "Bloom"},
        DebugModeBinding {'4', PostProcessDebugView::Luminance, "Luminance"},
        DebugModeBinding {'5', PostProcessDebugView::ShadowMap, "Shadow Map"},
        DebugModeBinding {'6', PostProcessDebugView::Normal, "Normal"},
        DebugModeBinding {'7', PostProcessDebugView::Roughness, "Roughness"},
        DebugModeBinding {'8', PostProcessDebugView::Metallic, "Metallic"},
    };

    for (const DebugModeBinding& binding : bindings)
    {
        if (!window_.IsKeyDown(binding.virtualKey) || runtimeRenderSettings_.debugView == binding.mode)
        {
            continue;
        }

        runtimeRenderSettings_.debugView = binding.mode;
        std::ostringstream message;
        message << "Post-process debug view: " << binding.label
                << " (press 1 Final, 2 HDR Scene, 3 Bloom, 4 Luminance, 5 Shadow, 6 Normal, 7 Roughness, 8 Metallic).";
        Logger::Info(message.str());
        return;
    }
}

void D3D12Renderer::InitializeShaderHotReloadTracking()
{
    shaderSourceFiles_.clear();
    shaderSourceFiles_.reserve(5);
    shaderReloadStatus_ = "Shader hot reload tracking initialized.";

    const std::array<std::wstring_view, 5> shaderFileNames = {
        L"triangle.hlsl",
        L"skybox.hlsl",
        L"shadow_depth.hlsl",
        L"bloom.hlsl",
        L"post_process.hlsl",
    };

    for (const std::wstring_view shaderFileName : shaderFileNames)
    {
        ShaderSourceFile sourceFile = {};
        sourceFile.path = ShaderCompiler::ResolveShaderPath(shaderFileName);

        std::error_code errorCode;
        sourceFile.lastWriteTime = std::filesystem::last_write_time(sourceFile.path, errorCode);
        sourceFile.hasKnownWriteTime = !errorCode;
        if (errorCode)
        {
            Logger::Error(std::string("Shader hot reload cannot stat: ") + sourceFile.path.string());
        }

        shaderSourceFiles_.push_back(std::move(sourceFile));
    }

    Logger::Info(shaderReloadStatus_);
}

void D3D12Renderer::RefreshShaderHotReloadWriteTimes()
{
    for (ShaderSourceFile& sourceFile : shaderSourceFiles_)
    {
        std::error_code errorCode;
        const std::filesystem::file_time_type writeTime =
            std::filesystem::last_write_time(sourceFile.path, errorCode);
        if (errorCode)
        {
            sourceFile.hasKnownWriteTime = false;
            continue;
        }

        sourceFile.lastWriteTime = writeTime;
        sourceFile.hasKnownWriteTime = true;
    }
}

void D3D12Renderer::UpdateShaderHotReload(const float deltaTimeSeconds)
{
    if (!autoShaderReloadEnabled_)
    {
        return;
    }

    if (shaderSourceFiles_.empty())
    {
        InitializeShaderHotReloadTracking();
    }

    bool detectedChange = false;
    for (ShaderSourceFile& sourceFile : shaderSourceFiles_)
    {
        std::error_code errorCode;
        const std::filesystem::file_time_type writeTime =
            std::filesystem::last_write_time(sourceFile.path, errorCode);
        if (errorCode)
        {
            if (sourceFile.hasKnownWriteTime)
            {
                Logger::Error(std::string("Shader hot reload lost track of: ") + sourceFile.path.string());
            }

            sourceFile.hasKnownWriteTime = false;
            continue;
        }

        if (!sourceFile.hasKnownWriteTime || writeTime != sourceFile.lastWriteTime)
        {
            sourceFile.lastWriteTime = writeTime;
            sourceFile.hasKnownWriteTime = true;
            pendingShaderReloadPath_ = sourceFile.path;
            detectedChange = true;
        }
    }

    if (detectedChange)
    {
        shaderReloadPending_ = true;
        shaderReloadDebounceSeconds_ = kShaderReloadDebounceSeconds;
        shaderReloadStatus_ = std::string("Shader change detected: ") + pendingShaderReloadPath_.filename().string();
        Logger::Info(std::string("Shader file changed, scheduling reload: ") + pendingShaderReloadPath_.string());
    }

    if (!shaderReloadPending_)
    {
        return;
    }

    shaderReloadDebounceSeconds_ -= deltaTimeSeconds;
    if (shaderReloadDebounceSeconds_ > 0.0f)
    {
        return;
    }

    shaderReloadPending_ = false;
    shaderReloadStatus_ = std::string("Reloading shader: ") + pendingShaderReloadPath_.filename().string();
    Logger::Info(std::string("Auto reloading shaders after file change: ") + pendingShaderReloadPath_.string());
    const bool reloadSucceeded = ReloadShaders();
    RefreshShaderHotReloadWriteTimes();
    if (!reloadSucceeded)
    {
        shaderReloadStatus_ = std::string("Auto reload failed: ") + pendingShaderReloadPath_.filename().string();
        Logger::Error("Automatic shader reload failed; previous pipeline states remain active.");
    }
    else
    {
        shaderReloadStatus_ = std::string("Auto reload succeeded: ") + pendingShaderReloadPath_.filename().string();
    }
}

void D3D12Renderer::UpdateSceneConstants()
{
    const float width = static_cast<float>(std::max(window_.GetClientWidth(), 1u));
    const float height = static_cast<float>(std::max(window_.GetClientHeight(), 1u));
    const float aspect = width / height;
    const float bloomWidth = static_cast<float>(GetBloomWidth(static_cast<std::uint32_t>(width)));
    const float bloomHeight = static_cast<float>(GetBloomHeight(static_cast<std::uint32_t>(height)));
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
    const float shadowNearDistance = std::max(camera_.nearPlane, 0.35f);
    const float shadowFarDistance = std::min(camera_.farPlane, 8.5f);
    const float nearHalfHeight = tanHalfFovY * shadowNearDistance;
    const float nearHalfWidth = tanHalfFovX * shadowNearDistance;
    const float farHalfHeight = tanHalfFovY * shadowFarDistance;
    const float farHalfWidth = tanHalfFovX * shadowFarDistance;
    const DirectX::XMVECTOR nearCenter =
        DirectX::XMVectorAdd(cameraPosition, DirectX::XMVectorScale(cameraForwardVector, shadowNearDistance));
    const DirectX::XMVECTOR farCenter =
        DirectX::XMVectorAdd(cameraPosition, DirectX::XMVectorScale(cameraForwardVector, shadowFarDistance));

    const std::array<DirectX::XMVECTOR, 8> frustumCorners = {
        DirectX::XMVectorAdd(
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorScale(cameraRightVector, -nearHalfWidth)),
            DirectX::XMVectorScale(orthonormalUpVector, nearHalfHeight)),
        DirectX::XMVectorAdd(
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorScale(cameraRightVector, nearHalfWidth)),
            DirectX::XMVectorScale(orthonormalUpVector, nearHalfHeight)),
        DirectX::XMVectorAdd(
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorScale(cameraRightVector, nearHalfWidth)),
            DirectX::XMVectorScale(orthonormalUpVector, -nearHalfHeight)),
        DirectX::XMVectorAdd(
            DirectX::XMVectorAdd(nearCenter, DirectX::XMVectorScale(cameraRightVector, -nearHalfWidth)),
            DirectX::XMVectorScale(orthonormalUpVector, -nearHalfHeight)),
        DirectX::XMVectorAdd(
            DirectX::XMVectorAdd(farCenter, DirectX::XMVectorScale(cameraRightVector, -farHalfWidth)),
            DirectX::XMVectorScale(orthonormalUpVector, farHalfHeight)),
        DirectX::XMVectorAdd(
            DirectX::XMVectorAdd(farCenter, DirectX::XMVectorScale(cameraRightVector, farHalfWidth)),
            DirectX::XMVectorScale(orthonormalUpVector, farHalfHeight)),
        DirectX::XMVectorAdd(
            DirectX::XMVectorAdd(farCenter, DirectX::XMVectorScale(cameraRightVector, farHalfWidth)),
            DirectX::XMVectorScale(orthonormalUpVector, -farHalfHeight)),
        DirectX::XMVectorAdd(
            DirectX::XMVectorAdd(farCenter, DirectX::XMVectorScale(cameraRightVector, -farHalfWidth)),
            DirectX::XMVectorScale(orthonormalUpVector, -farHalfHeight)),
    };

    DirectX::XMVECTOR frustumCenter = DirectX::XMVectorZero();
    for (const DirectX::XMVECTOR corner : frustumCorners)
    {
        frustumCenter = DirectX::XMVectorAdd(frustumCenter, corner);
    }
    frustumCenter = DirectX::XMVectorScale(frustumCenter, 1.0f / static_cast<float>(frustumCorners.size()));

    const DirectX::XMVECTOR lightUp = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const DirectX::XMVECTOR lightPosition =
        DirectX::XMVectorAdd(frustumCenter, DirectX::XMVectorScale(lightDirection, shadowFarDistance + 4.0f));
    const DirectX::XMMATRIX lightView = DirectX::XMMatrixLookAtLH(lightPosition, frustumCenter, lightUp);

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for (const DirectX::XMVECTOR corner : frustumCorners)
    {
        const DirectX::XMVECTOR lightSpaceCorner = DirectX::XMVector3TransformCoord(corner, lightView);
        minX = std::min(minX, DirectX::XMVectorGetX(lightSpaceCorner));
        maxX = std::max(maxX, DirectX::XMVectorGetX(lightSpaceCorner));
        minY = std::min(minY, DirectX::XMVectorGetY(lightSpaceCorner));
        maxY = std::max(maxY, DirectX::XMVectorGetY(lightSpaceCorner));
        minZ = std::min(minZ, DirectX::XMVectorGetZ(lightSpaceCorner));
        maxZ = std::max(maxZ, DirectX::XMVectorGetZ(lightSpaceCorner));
    }

    const float radius = std::max(maxX - minX, maxY - minY) * 0.5f * 1.08f;
    const float centerX = (minX + maxX) * 0.5f;
    const float centerY = (minY + maxY) * 0.5f;
    const float texelWorldSize = (radius * 2.0f) / static_cast<float>(kShadowMapSize);
    const float snappedCenterX = std::floor(centerX / texelWorldSize + 0.5f) * texelWorldSize;
    const float snappedCenterY = std::floor(centerY / texelWorldSize + 0.5f) * texelWorldSize;
    const float depthPadding = 4.0f;
    const DirectX::XMMATRIX lightProjection = DirectX::XMMatrixOrthographicOffCenterLH(
        snappedCenterX - radius,
        snappedCenterX + radius,
        snappedCenterY - radius,
        snappedCenterY + radius,
        std::max(0.1f, minZ - depthPadding),
        maxZ + depthPadding);
    const DirectX::XMMATRIX lightViewProjection = lightView * lightProjection;

    SceneConstants constants = {};
    constants.cameraPositionAndEnvironmentIntensity = {
        camera_.position.x,
        camera_.position.y,
        camera_.position.z,
        runtimeRenderSettings_.environmentIntensity};
    constants.directionalLightDirectionAndIntensity = {0.35f, 0.8f, -0.45f, 4.25f};
    constants.directionalLightColorAndExposure = {1.0f, 0.96f, 0.92f, runtimeRenderSettings_.exposure};
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
        runtimeRenderSettings_.shadowBias,
        0.0f};
    constants.bloomParams = {
        runtimeRenderSettings_.bloomThreshold,
        runtimeRenderSettings_.bloomSoftKnee,
        runtimeRenderSettings_.bloomIntensity,
        runtimeRenderSettings_.bloomRadius};
    constants.iblParams = {
        runtimeRenderSettings_.iblDiffuseIntensity,
        runtimeRenderSettings_.iblSpecularIntensity,
        runtimeRenderSettings_.iblSpecularBlend,
        0.0f};
    constants.frameBufferParams = {1.0f / width, 1.0f / height, 1.0f / bloomWidth, 1.0f / bloomHeight};
    constants.postProcessParams = {static_cast<float>(runtimeRenderSettings_.debugView), 0.0f, 0.0f, 0.0f};
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

