#include "ugc_renderer/render/gpu_debug_marker.h"

#include <Windows.h>

#include <d3d12.h>

namespace ugc_renderer
{
namespace
{
using PixBeginEventOnCommandList = void(WINAPI*)(ID3D12GraphicsCommandList*, UINT64, PCSTR);
using PixEndEventOnCommandList = void(WINAPI*)(ID3D12GraphicsCommandList*);
using PixSetMarkerOnCommandList = void(WINAPI*)(ID3D12GraphicsCommandList*, UINT64, PCSTR);

struct PixRuntimeExports
{
    HMODULE module = nullptr;
    PixBeginEventOnCommandList beginEvent = nullptr;
    PixEndEventOnCommandList endEvent = nullptr;
    PixSetMarkerOnCommandList setMarker = nullptr;
};

[[nodiscard]] PixRuntimeExports LoadPixRuntime() noexcept
{
    PixRuntimeExports exports = {};
    exports.module = ::LoadLibraryW(L"WinPixEventRuntime.dll");
    if (exports.module == nullptr)
    {
        return {};
    }

    exports.beginEvent = reinterpret_cast<PixBeginEventOnCommandList>(
        ::GetProcAddress(exports.module, "PIXBeginEventOnCommandList"));
    exports.endEvent = reinterpret_cast<PixEndEventOnCommandList>(
        ::GetProcAddress(exports.module, "PIXEndEventOnCommandList"));
    exports.setMarker = reinterpret_cast<PixSetMarkerOnCommandList>(
        ::GetProcAddress(exports.module, "PIXSetMarkerOnCommandList"));

    if (exports.beginEvent == nullptr || exports.endEvent == nullptr || exports.setMarker == nullptr)
    {
        ::FreeLibrary(exports.module);
        return {};
    }

    return exports;
}

[[nodiscard]] const PixRuntimeExports& GetPixRuntime() noexcept
{
    static const PixRuntimeExports exports = LoadPixRuntime();
    return exports;
}
}

GpuDebugEventScope::GpuDebugEventScope(
    ID3D12GraphicsCommandList* commandList,
    const char* label,
    const std::uint64_t color) noexcept
{
    const PixRuntimeExports& pixRuntime = GetPixRuntime();
    if (pixRuntime.beginEvent == nullptr || commandList == nullptr || label == nullptr || label[0] == '\0')
    {
        return;
    }

    pixRuntime.beginEvent(commandList, color, label);
    commandList_ = commandList;
    active_ = true;
}

GpuDebugEventScope::~GpuDebugEventScope() noexcept
{
    if (!active_)
    {
        return;
    }

    const PixRuntimeExports& pixRuntime = GetPixRuntime();
    if (pixRuntime.endEvent != nullptr)
    {
        pixRuntime.endEvent(commandList_);
    }
}

void SetGpuDebugMarker(ID3D12GraphicsCommandList* commandList, const char* label, const std::uint64_t color) noexcept
{
    const PixRuntimeExports& pixRuntime = GetPixRuntime();
    if (pixRuntime.setMarker == nullptr || commandList == nullptr || label == nullptr || label[0] == '\0')
    {
        return;
    }

    pixRuntime.setMarker(commandList, color, label);
}

bool AreGpuDebugMarkersAvailable() noexcept
{
    return GetPixRuntime().beginEvent != nullptr;
}
}
