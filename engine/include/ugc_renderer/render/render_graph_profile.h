#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ugc_renderer
{
struct RenderGraphPassProfileTiming
{
    std::string passName;
    double cpuMilliseconds = 0.0;
    double gpuMilliseconds = 0.0;
};

struct RenderGraphProfileSnapshot
{
    std::vector<RenderGraphPassProfileTiming> passTimings;
    double totalCpuMilliseconds = 0.0;
    double totalGpuMilliseconds = 0.0;
    std::uint64_t cpuFrameIndex = 0;
    std::uint64_t gpuFrameIndex = 0;
    bool hasGpuTimings = false;
};
}
