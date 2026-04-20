#pragma once

#include "ugc_renderer/render/material.h"
#include "ugc_renderer/render/render_graph_profile.h"
#include "ugc_renderer/render/runtime_render_settings.h"

#include <Windows.h>

#include <string_view>
#include <vector>

namespace ugc_renderer
{
class RuntimeControlPanel
{
public:
    RuntimeControlPanel(HWND ownerWindow, const RuntimeRenderSettings& initialSettings, bool autoShaderReloadEnabled);
    ~RuntimeControlPanel();

    RuntimeControlPanel(const RuntimeControlPanel&) = delete;
    RuntimeControlPanel& operator=(const RuntimeControlPanel&) = delete;
    RuntimeControlPanel(RuntimeControlPanel&&) = delete;
    RuntimeControlPanel& operator=(RuntimeControlPanel&&) = delete;

    void ToggleVisibility();
    [[nodiscard]] bool IsVisible() const noexcept;
    void SyncFromRenderer(const RuntimeRenderSettings& settings);
    void SyncMaterials(const std::vector<RuntimeMaterialInfo>& materials);
    void SyncAutoShaderReloadEnabled(bool enabled);
    void UpdateProfilingText(const RenderGraphProfileSnapshot& snapshot);
    void UpdateShaderReloadStatusText(std::string_view status);
    [[nodiscard]] bool ConsumeSettingsChange(RuntimeRenderSettings& settings);
    [[nodiscard]] bool ConsumeMaterialChange(std::uint32_t& materialIndex, MaterialDesc& materialDesc);
    [[nodiscard]] bool ConsumeAutoShaderReloadChange(bool& enabled) noexcept;
    [[nodiscard]] bool ConsumeShaderReloadRequest() noexcept;
    [[nodiscard]] bool ConsumeSavePresetRequest() noexcept;
    [[nodiscard]] bool ConsumeLoadPresetRequest() noexcept;
    [[nodiscard]] bool ConsumeRenderGraphExportRequest() noexcept;
    [[nodiscard]] bool ConsumeScreenshotCaptureRequest() noexcept;

private:
    struct SliderControl;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    void RegisterWindowClass();
    void CreateControls();
    void CreateTrackbar(
        const wchar_t* label,
        int x,
        int y,
        SliderControl& slider,
        int trackbarControlId,
        float minValue,
        float maxValue,
        int precision);
    void ApplySettingsToControls(const RuntimeRenderSettings& settings);
    void ApplyMaterialToControls(const RuntimeMaterialInfo& material);
    [[nodiscard]] RuntimeRenderSettings BuildSettingsFromControls() const;
    [[nodiscard]] MaterialDesc BuildMaterialFromControls() const;
    void MarkSettingsDirtyFromControls();
    void MarkSelectedMaterialDirtyFromControls();
    void UpdateValueLabel(const SliderControl& slider, float value) const;
    void UpdateMaterialValueLabels(const MaterialDesc& material) const;
    [[nodiscard]] int FloatToTrackbarPosition(const SliderControl& slider, float value) const;
    [[nodiscard]] float TrackbarPositionToFloat(const SliderControl& slider) const;
    void SetControlFont(HWND handle) const;

    static constexpr const wchar_t* kWindowClassName = L"UGCRendererRuntimeControlPanel";
    static constexpr int kTrackbarResolution = 1000;

    HWND ownerWindow_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    RuntimeRenderSettings lastSyncedSettings_ = {};
    RuntimeRenderSettings pendingSettings_ = {};
    bool hasPendingSettings_ = false;
    bool lastSyncedAutoShaderReloadEnabled_ = true;
    bool pendingAutoShaderReloadEnabled_ = true;
    bool hasPendingAutoShaderReloadChange_ = false;
    bool suppressControlNotifications_ = false;

    struct SliderControl
    {
        HWND labelHandle = nullptr;
        HWND trackbarHandle = nullptr;
        HWND valueHandle = nullptr;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        int precision = 2;
    };

    HWND debugViewLabel_ = nullptr;
    HWND debugViewCombo_ = nullptr;
    HWND autoShaderReloadCheckbox_ = nullptr;
    HWND reloadShadersButton_ = nullptr;
    HWND savePresetButton_ = nullptr;
    HWND loadPresetButton_ = nullptr;
    HWND exportRenderGraphButton_ = nullptr;
    HWND screenshotCaptureButton_ = nullptr;
    HWND resetDefaultsButton_ = nullptr;
    HWND helpLabel_ = nullptr;
    HWND shaderReloadStatusLabel_ = nullptr;
    HWND profilingLabel_ = nullptr;
    HWND materialLabel_ = nullptr;
    HWND materialCombo_ = nullptr;
    HWND materialAlphaModeLabel_ = nullptr;
    HWND materialAlphaModeCombo_ = nullptr;
    HWND materialDoubleSidedCheckbox_ = nullptr;
    HWND materialHelpLabel_ = nullptr;
    SliderControl exposureSlider_ = {};
    SliderControl environmentSlider_ = {};
    SliderControl iblDiffuseSlider_ = {};
    SliderControl iblSpecularSlider_ = {};
    SliderControl iblSpecularBlendSlider_ = {};
    SliderControl bloomThresholdSlider_ = {};
    SliderControl bloomSoftKneeSlider_ = {};
    SliderControl bloomIntensitySlider_ = {};
    SliderControl bloomRadiusSlider_ = {};
    SliderControl shadowBiasSlider_ = {};
    SliderControl materialBaseColorRedSlider_ = {};
    SliderControl materialBaseColorGreenSlider_ = {};
    SliderControl materialBaseColorBlueSlider_ = {};
    SliderControl materialBaseColorAlphaSlider_ = {};
    SliderControl materialEmissiveRedSlider_ = {};
    SliderControl materialEmissiveGreenSlider_ = {};
    SliderControl materialEmissiveBlueSlider_ = {};
    SliderControl materialMetallicSlider_ = {};
    SliderControl materialRoughnessSlider_ = {};
    SliderControl materialNormalScaleSlider_ = {};
    SliderControl materialOcclusionSlider_ = {};
    SliderControl materialAlphaCutoffSlider_ = {};
    std::vector<RuntimeMaterialInfo> lastSyncedMaterials_ = {};
    std::uint32_t selectedMaterialListIndex_ = 0;
    std::uint32_t pendingMaterialRuntimeIndex_ = 0;
    MaterialDesc pendingMaterialDesc_ = {};
    bool hasPendingMaterialChange_ = false;
    bool reloadShadersRequested_ = false;
    bool savePresetRequested_ = false;
    bool loadPresetRequested_ = false;
    bool exportRenderGraphRequested_ = false;
    bool screenshotCaptureRequested_ = false;
};
} // namespace ugc_renderer
