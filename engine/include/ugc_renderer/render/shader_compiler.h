#pragma once

#include <filesystem>
#include <string_view>

#include <d3dcommon.h>
#include <wrl/client.h>

namespace ugc_renderer
{
class ShaderCompiler
{
public:
    static std::filesystem::path ResolveShaderPath(std::wstring_view fileName);
    static Microsoft::WRL::ComPtr<ID3DBlob> CompileFromFile(
        const std::filesystem::path& path,
        std::string_view entryPoint,
        std::string_view target);
};
} // namespace ugc_renderer
