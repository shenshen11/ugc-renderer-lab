#include "ugc_renderer/render/image_loader.h"

#include "ugc_renderer/core/throw_if_failed.h"

#include <stdexcept>

#include <wincodec.h>
#include <wrl/client.h>

namespace ugc_renderer
{
ImageData ImageLoader::LoadRgba8(const std::filesystem::path& path)
{
    Microsoft::WRL::ComPtr<IWICImagingFactory> imagingFactory;
    ThrowIfFailed(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imagingFactory)),
        "CoCreateInstance(CLSID_WICImagingFactory)");

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    ThrowIfFailed(
        imagingFactory->CreateDecoderFromFilename(
            path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder),
        "IWICImagingFactory::CreateDecoderFromFilename");

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, &frame), "IWICBitmapDecoder::GetFrame");

    UINT width = 0;
    UINT height = 0;
    ThrowIfFailed(frame->GetSize(&width, &height), "IWICBitmapFrameDecode::GetSize");

    WICPixelFormatGUID sourceFormat = {};
    ThrowIfFailed(frame->GetPixelFormat(&sourceFormat), "IWICBitmapFrameDecode::GetPixelFormat");

    Microsoft::WRL::ComPtr<IWICFormatConverter> formatConverter;
    ThrowIfFailed(imagingFactory->CreateFormatConverter(&formatConverter), "IWICImagingFactory::CreateFormatConverter");

    BOOL canConvert = FALSE;
    ThrowIfFailed(
        formatConverter->CanConvert(sourceFormat, GUID_WICPixelFormat32bppRGBA, &canConvert),
        "IWICFormatConverter::CanConvert");
    if (canConvert == FALSE)
    {
        throw std::runtime_error("WIC cannot convert image to RGBA8.");
    }

    ThrowIfFailed(
        formatConverter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom),
        "IWICFormatConverter::Initialize");

    ImageData imageData = {};
    imageData.width = width;
    imageData.height = height;
    imageData.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    imageData.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);

    const UINT stride = width * 4;
    const UINT bufferSize = stride * height;
    ThrowIfFailed(
        formatConverter->CopyPixels(
            nullptr,
            stride,
            bufferSize,
            reinterpret_cast<BYTE*>(imageData.pixels.data())),
        "IWICBitmapSource::CopyPixels");

    return imageData;
}
} // namespace ugc_renderer
