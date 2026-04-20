#pragma once

#include <cstdint>

namespace ugc_renderer
{
enum class PostProcessDebugView : std::uint32_t
{
    Final = 0,
    HdrScene = 1,
    Bloom = 2,
    Luminance = 3,
    ShadowMap = 4,
    Normal = 5,
    Roughness = 6,
    Metallic = 7,
};

struct RuntimeRenderSettings
{
    float environmentIntensity = 1.05f;
    float exposure = 1.0f;
    float shadowBias = 0.0035f;
    float bloomThreshold = 1.15f;
    float bloomSoftKnee = 0.35f;
    float bloomIntensity = 0.18f;
    float bloomRadius = 1.0f;
    float iblDiffuseIntensity = 1.0f;
    float iblSpecularIntensity = 1.0f;
    float iblSpecularBlend = 0.85f;
    PostProcessDebugView debugView = PostProcessDebugView::Final;

    [[nodiscard]] bool operator==(const RuntimeRenderSettings& other) const noexcept
    {
        return environmentIntensity == other.environmentIntensity
            && exposure == other.exposure
            && shadowBias == other.shadowBias
            && bloomThreshold == other.bloomThreshold
            && bloomSoftKnee == other.bloomSoftKnee
            && bloomIntensity == other.bloomIntensity
            && bloomRadius == other.bloomRadius
            && iblDiffuseIntensity == other.iblDiffuseIntensity
            && iblSpecularIntensity == other.iblSpecularIntensity
            && iblSpecularBlend == other.iblSpecularBlend
            && debugView == other.debugView;
    }
};

[[nodiscard]] constexpr const char* ToString(const PostProcessDebugView debugView) noexcept
{
    switch (debugView)
    {
    case PostProcessDebugView::Final:
        return "Final";
    case PostProcessDebugView::HdrScene:
        return "HDR Scene";
    case PostProcessDebugView::Bloom:
        return "Bloom";
    case PostProcessDebugView::Luminance:
        return "Luminance";
    case PostProcessDebugView::ShadowMap:
        return "Shadow Map";
    case PostProcessDebugView::Normal:
        return "Normal";
    case PostProcessDebugView::Roughness:
        return "Roughness";
    case PostProcessDebugView::Metallic:
        return "Metallic";
    }

    return "Unknown";
}
} // namespace ugc_renderer
