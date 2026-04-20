#pragma once

#include <cstdint>

struct ID3D12GraphicsCommandList;

namespace ugc_renderer
{
class GpuDebugEventScope
{
public:
    GpuDebugEventScope(ID3D12GraphicsCommandList* commandList, const char* label, std::uint64_t color) noexcept;
    ~GpuDebugEventScope() noexcept;

    GpuDebugEventScope(const GpuDebugEventScope&) = delete;
    GpuDebugEventScope& operator=(const GpuDebugEventScope&) = delete;
    GpuDebugEventScope(GpuDebugEventScope&&) = delete;
    GpuDebugEventScope& operator=(GpuDebugEventScope&&) = delete;

private:
    ID3D12GraphicsCommandList* commandList_ = nullptr;
    bool active_ = false;
};

void SetGpuDebugMarker(ID3D12GraphicsCommandList* commandList, const char* label, std::uint64_t color) noexcept;
[[nodiscard]] bool AreGpuDebugMarkersAvailable() noexcept;
}
