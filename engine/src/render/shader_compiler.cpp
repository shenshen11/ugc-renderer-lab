#include "ugc_renderer/render/shader_compiler.h"

#include "ugc_renderer/core/logger.h"
#include "ugc_renderer/core/throw_if_failed.h"

#include <Windows.h>

#include <array>
#include <string>

#include <d3dcompiler.h>

namespace ugc_renderer
{
std::filesystem::path ShaderCompiler::ResolveShaderPath(const std::wstring_view fileName)
{
    std::array<wchar_t, MAX_PATH> modulePath = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length == modulePath.size())
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "GetModuleFileNameW");
    }

    return std::filesystem::path(modulePath.data()).parent_path() / L"shaders" / std::filesystem::path(fileName);
}

Microsoft::WRL::ComPtr<ID3DBlob> ShaderCompiler::CompileFromFile(
    const std::filesystem::path& path,
    const std::string_view entryPoint,
    const std::string_view target)
{
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    Microsoft::WRL::ComPtr<ID3DBlob> shaderBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    const HRESULT result = D3DCompileFromFile(
        path.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.data(),
        target.data(),
        compileFlags,
        0,
        &shaderBytecode,
        &errorBlob);

    if (FAILED(result))
    {
        if (errorBlob != nullptr)
        {
            Logger::Error(std::string(
                static_cast<const char*>(errorBlob->GetBufferPointer()),
                errorBlob->GetBufferSize()));
        }

        ThrowIfFailed(result, "D3DCompileFromFile");
    }

    return shaderBytecode;
}
} // namespace ugc_renderer
