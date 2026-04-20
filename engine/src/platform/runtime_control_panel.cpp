#include "ugc_renderer/platform/runtime_control_panel.h"

#include "ugc_renderer/core/throw_if_failed.h"

#include <CommCtrl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

namespace ugc_renderer
{
namespace
{
constexpr int kPanelWidth = 848;
constexpr int kPanelHeight = 812;

constexpr int kDebugViewComboControlId = 100;
constexpr int kReloadShadersButtonControlId = 101;
constexpr int kSavePresetButtonControlId = 102;
constexpr int kLoadPresetButtonControlId = 103;
constexpr int kExportRenderGraphButtonControlId = 104;
constexpr int kScreenshotCaptureButtonControlId = 105;
constexpr int kResetDefaultsButtonControlId = 106;
constexpr int kAutoShaderReloadCheckboxControlId = 107;
constexpr int kExposureTrackbarControlId = 200;
constexpr int kEnvironmentTrackbarControlId = 201;
constexpr int kIblDiffuseTrackbarControlId = 202;
constexpr int kIblSpecularTrackbarControlId = 203;
constexpr int kIblSpecularBlendTrackbarControlId = 204;
constexpr int kBloomThresholdTrackbarControlId = 205;
constexpr int kBloomSoftKneeTrackbarControlId = 206;
constexpr int kBloomIntensityTrackbarControlId = 207;
constexpr int kBloomRadiusTrackbarControlId = 208;
constexpr int kShadowBiasTrackbarControlId = 209;
constexpr int kMaterialComboControlId = 300;
constexpr int kMaterialAlphaModeComboControlId = 301;
constexpr int kMaterialDoubleSidedCheckboxControlId = 302;
constexpr int kMaterialBaseColorRedTrackbarControlId = 400;
constexpr int kMaterialBaseColorGreenTrackbarControlId = 401;
constexpr int kMaterialBaseColorBlueTrackbarControlId = 402;
constexpr int kMaterialBaseColorAlphaTrackbarControlId = 403;
constexpr int kMaterialEmissiveRedTrackbarControlId = 404;
constexpr int kMaterialEmissiveGreenTrackbarControlId = 405;
constexpr int kMaterialEmissiveBlueTrackbarControlId = 406;
constexpr int kMaterialMetallicTrackbarControlId = 407;
constexpr int kMaterialRoughnessTrackbarControlId = 408;
constexpr int kMaterialNormalScaleTrackbarControlId = 409;
constexpr int kMaterialOcclusionTrackbarControlId = 410;
constexpr int kMaterialAlphaCutoffTrackbarControlId = 411;

const wchar_t* ToWideString(const PostProcessDebugView debugView) noexcept
{
    switch (debugView)
    {
    case PostProcessDebugView::Final:
        return L"Final";
    case PostProcessDebugView::HdrScene:
        return L"HDR Scene";
    case PostProcessDebugView::Bloom:
        return L"Bloom";
    case PostProcessDebugView::Luminance:
        return L"Luminance";
    case PostProcessDebugView::ShadowMap:
        return L"Shadow Map";
    case PostProcessDebugView::Normal:
        return L"Normal";
    case PostProcessDebugView::Roughness:
        return L"Roughness";
    case PostProcessDebugView::Metallic:
        return L"Metallic";
    }

    return L"Unknown";
}

const wchar_t* ToWideString(const MaterialAlphaMode alphaMode) noexcept
{
    switch (alphaMode)
    {
    case MaterialAlphaMode::Opaque:
        return L"Opaque";
    case MaterialAlphaMode::Mask:
        return L"Mask";
    case MaterialAlphaMode::Blend:
        return L"Blend";
    }

    return L"Opaque";
}

RuntimeRenderSettings ClampRuntimeRenderSettings(RuntimeRenderSettings settings) noexcept
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

MaterialDesc ClampRuntimeMaterialDesc(MaterialDesc material) noexcept
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

bool HaveMaterialEntriesChanged(
    const std::vector<RuntimeMaterialInfo>& left,
    const std::vector<RuntimeMaterialInfo>& right) noexcept
{
    if (left.size() != right.size())
    {
        return true;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (left[index].runtimeIndex != right[index].runtimeIndex || left[index].name != right[index].name)
        {
            return true;
        }
    }

    return false;
}

std::wstring ToWideString(const std::string_view text)
{
    return std::wstring(text.begin(), text.end());
}

int ToComboIndex(const MaterialAlphaMode alphaMode) noexcept
{
    switch (alphaMode)
    {
    case MaterialAlphaMode::Mask:
        return 1;
    case MaterialAlphaMode::Blend:
        return 2;
    case MaterialAlphaMode::Opaque:
    default:
        return 0;
    }
}

MaterialAlphaMode ToMaterialAlphaMode(const LRESULT comboSelection) noexcept
{
    switch (comboSelection)
    {
    case 1:
        return MaterialAlphaMode::Mask;
    case 2:
        return MaterialAlphaMode::Blend;
    case 0:
    default:
        return MaterialAlphaMode::Opaque;
    }
}

std::wstring ToWidePassName(const std::string& passName)
{
    return std::wstring(passName.begin(), passName.end());
}
} // namespace

RuntimeControlPanel::RuntimeControlPanel(
    const HWND ownerWindow,
    const RuntimeRenderSettings& initialSettings,
    const bool autoShaderReloadEnabled)
    : ownerWindow_(ownerWindow)
    , font_(static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)))
    , lastSyncedSettings_(ClampRuntimeRenderSettings(initialSettings))
    , pendingSettings_(lastSyncedSettings_)
    , lastSyncedAutoShaderReloadEnabled_(autoShaderReloadEnabled)
    , pendingAutoShaderReloadEnabled_(autoShaderReloadEnabled)
{
    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&commonControls);

    RegisterWindowClass();

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kWindowClassName,
        L"UGC Renderer Controls",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kPanelWidth,
        kPanelHeight,
        ownerWindow_,
        nullptr,
        GetModuleHandleW(nullptr),
        this);

    if (hwnd_ == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "CreateWindowExW(runtime control panel)");
    }

    CreateControls();
    ApplySettingsToControls(lastSyncedSettings_);
    SyncAutoShaderReloadEnabled(lastSyncedAutoShaderReloadEnabled_);
    if (ownerWindow_ != nullptr)
    {
        RECT ownerRect = {};
        if (GetWindowRect(ownerWindow_, &ownerRect) != FALSE)
        {
            RECT panelRect = {};
            GetWindowRect(hwnd_, &panelRect);
            const int panelWidth = panelRect.right - panelRect.left;
            const int panelHeight = panelRect.bottom - panelRect.top;
            RECT workRect = {};
            MONITORINFO monitorInfo = {};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetMonitorInfoW(MonitorFromWindow(ownerWindow_, MONITOR_DEFAULTTONEAREST), &monitorInfo) != FALSE)
            {
                workRect = monitorInfo.rcWork;
            }
            else
            {
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &workRect, 0);
            }

            const int workLeft = static_cast<int>(workRect.left);
            const int workTop = static_cast<int>(workRect.top);
            const int workRight = static_cast<int>(workRect.right);
            const int workBottom = static_cast<int>(workRect.bottom);
            int panelX = ownerRect.right + 18;
            if (panelX + panelWidth > workRight)
            {
                panelX = ownerRect.left - panelWidth - 18;
            }
            panelX = std::clamp(panelX, workLeft, std::max(workLeft, workRight - panelWidth));
            const int panelY =
                std::clamp(static_cast<int>(ownerRect.top), workTop, std::max(workTop, workBottom - panelHeight));
            SetWindowPos(
                hwnd_,
                nullptr,
                panelX,
                panelY,
                0,
                0,
                SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

RuntimeControlPanel::~RuntimeControlPanel()
{
    if (hwnd_ != nullptr && IsWindow(hwnd_) != FALSE)
    {
        DestroyWindow(hwnd_);
    }
}

void RuntimeControlPanel::ToggleVisibility()
{
    if (hwnd_ == nullptr)
    {
        return;
    }

    ShowWindow(hwnd_, IsVisible() ? SW_HIDE : SW_SHOW);
}

bool RuntimeControlPanel::IsVisible() const noexcept
{
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

void RuntimeControlPanel::SyncFromRenderer(const RuntimeRenderSettings& settings)
{
    const RuntimeRenderSettings clampedSettings = ClampRuntimeRenderSettings(settings);
    if (clampedSettings == lastSyncedSettings_)
    {
        return;
    }

    lastSyncedSettings_ = clampedSettings;
    ApplySettingsToControls(lastSyncedSettings_);
}

void RuntimeControlPanel::SyncMaterials(const std::vector<RuntimeMaterialInfo>& materials)
{
    if (materialCombo_ == nullptr || materialAlphaModeCombo_ == nullptr)
    {
        return;
    }

    const bool materialEntriesChanged = HaveMaterialEntriesChanged(lastSyncedMaterials_, materials);
    std::uint32_t selectedRuntimeIndex = 0;
    if (!lastSyncedMaterials_.empty() && selectedMaterialListIndex_ < lastSyncedMaterials_.size())
    {
        selectedRuntimeIndex = lastSyncedMaterials_[selectedMaterialListIndex_].runtimeIndex;
    }

    lastSyncedMaterials_ = materials;
    if (lastSyncedMaterials_.empty())
    {
        return;
    }

    if (materialEntriesChanged)
    {
        suppressControlNotifications_ = true;
        SendMessageW(materialCombo_, CB_RESETCONTENT, 0, 0);
        for (const RuntimeMaterialInfo& material : lastSyncedMaterials_)
        {
            const std::wstring wideName = ToWideString(material.name);
            SendMessageW(materialCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideName.c_str()));
        }
        suppressControlNotifications_ = false;
    }

    auto selectedMaterialIterator =
        std::find_if(
            lastSyncedMaterials_.begin(),
            lastSyncedMaterials_.end(),
            [&](const RuntimeMaterialInfo& material)
            {
                return material.runtimeIndex == selectedRuntimeIndex;
            });
    if (selectedMaterialIterator == lastSyncedMaterials_.end())
    {
        selectedMaterialIterator = lastSyncedMaterials_.begin();
    }

    selectedMaterialListIndex_ =
        static_cast<std::uint32_t>(std::distance(lastSyncedMaterials_.begin(), selectedMaterialIterator));
    ApplyMaterialToControls(*selectedMaterialIterator);
}

void RuntimeControlPanel::SyncAutoShaderReloadEnabled(const bool enabled)
{
    if (autoShaderReloadCheckbox_ == nullptr)
    {
        return;
    }

    lastSyncedAutoShaderReloadEnabled_ = enabled;
    pendingAutoShaderReloadEnabled_ = enabled;
    SendMessageW(
        autoShaderReloadCheckbox_,
        BM_SETCHECK,
        enabled ? BST_CHECKED : BST_UNCHECKED,
        0);
}

void RuntimeControlPanel::UpdateProfilingText(const RenderGraphProfileSnapshot& snapshot)
{
    if (profilingLabel_ == nullptr)
    {
        return;
    }

    if (snapshot.passTimings.empty())
    {
        SetWindowTextW(profilingLabel_, L"Profiling: waiting for first rendered frame...");
        return;
    }

    const auto topCpuPass = std::max_element(
        snapshot.passTimings.begin(),
        snapshot.passTimings.end(),
        [](const RenderGraphPassProfileTiming& left, const RenderGraphPassProfileTiming& right)
        {
            return left.cpuMilliseconds < right.cpuMilliseconds;
        });
    const auto topGpuPass = std::max_element(
        snapshot.passTimings.begin(),
        snapshot.passTimings.end(),
        [](const RenderGraphPassProfileTiming& left, const RenderGraphPassProfileTiming& right)
        {
            return left.gpuMilliseconds < right.gpuMilliseconds;
        });

    const std::wstring topCpuName =
        topCpuPass != snapshot.passTimings.end() ? ToWidePassName(topCpuPass->passName) : L"-";
    const std::wstring topGpuName =
        topGpuPass != snapshot.passTimings.end() && snapshot.hasGpuTimings ? ToWidePassName(topGpuPass->passName) : L"-";
    const double topCpuMilliseconds =
        topCpuPass != snapshot.passTimings.end() ? topCpuPass->cpuMilliseconds : 0.0;
    const double topGpuMilliseconds =
        topGpuPass != snapshot.passTimings.end() && snapshot.hasGpuTimings ? topGpuPass->gpuMilliseconds : 0.0;

    wchar_t text[384] = {};
    if (snapshot.hasGpuTimings)
    {
        swprintf_s(
            text,
            L"Profiling\nCPU %.3f ms (frame %llu) | top %ls %.3f\nGPU %.3f ms (sample %llu) | top %ls %.3f",
            snapshot.totalCpuMilliseconds,
            static_cast<unsigned long long>(snapshot.cpuFrameIndex),
            topCpuName.c_str(),
            topCpuMilliseconds,
            snapshot.totalGpuMilliseconds,
            static_cast<unsigned long long>(snapshot.gpuFrameIndex),
            topGpuName.c_str(),
            topGpuMilliseconds);
    }
    else
    {
        swprintf_s(
            text,
            L"Profiling\nCPU %.3f ms (frame %llu) | top %ls %.3f\nGPU sampling pending...",
            snapshot.totalCpuMilliseconds,
            static_cast<unsigned long long>(snapshot.cpuFrameIndex),
            topCpuName.c_str(),
            topCpuMilliseconds);
    }

    SetWindowTextW(profilingLabel_, text);
}

void RuntimeControlPanel::UpdateShaderReloadStatusText(const std::string_view status)
{
    if (shaderReloadStatusLabel_ == nullptr)
    {
        return;
    }

    const std::wstring wideStatus(status.begin(), status.end());
    SetWindowTextW(shaderReloadStatusLabel_, wideStatus.c_str());
}

bool RuntimeControlPanel::ConsumeSettingsChange(RuntimeRenderSettings& settings)
{
    if (!hasPendingSettings_)
    {
        return false;
    }

    hasPendingSettings_ = false;
    settings = pendingSettings_;
    return true;
}

bool RuntimeControlPanel::ConsumeMaterialChange(std::uint32_t& materialIndex, MaterialDesc& materialDesc)
{
    if (!hasPendingMaterialChange_)
    {
        return false;
    }

    hasPendingMaterialChange_ = false;
    materialIndex = pendingMaterialRuntimeIndex_;
    materialDesc = pendingMaterialDesc_;
    return true;
}

bool RuntimeControlPanel::ConsumeAutoShaderReloadChange(bool& enabled) noexcept
{
    if (!hasPendingAutoShaderReloadChange_)
    {
        return false;
    }

    hasPendingAutoShaderReloadChange_ = false;
    enabled = pendingAutoShaderReloadEnabled_;
    return true;
}

bool RuntimeControlPanel::ConsumeShaderReloadRequest() noexcept
{
    if (!reloadShadersRequested_)
    {
        return false;
    }

    reloadShadersRequested_ = false;
    return true;
}

bool RuntimeControlPanel::ConsumeSavePresetRequest() noexcept
{
    if (!savePresetRequested_)
    {
        return false;
    }

    savePresetRequested_ = false;
    return true;
}

bool RuntimeControlPanel::ConsumeLoadPresetRequest() noexcept
{
    if (!loadPresetRequested_)
    {
        return false;
    }

    loadPresetRequested_ = false;
    return true;
}

bool RuntimeControlPanel::ConsumeRenderGraphExportRequest() noexcept
{
    if (!exportRenderGraphRequested_)
    {
        return false;
    }

    exportRenderGraphRequested_ = false;
    return true;
}

bool RuntimeControlPanel::ConsumeScreenshotCaptureRequest() noexcept
{
    if (!screenshotCaptureRequested_)
    {
        return false;
    }

    screenshotCaptureRequested_ = false;
    return true;
}

LRESULT CALLBACK RuntimeControlPanel::WndProc(
    const HWND hwnd,
    const UINT message,
    const WPARAM wparam,
    const LPARAM lparam)
{
    RuntimeControlPanel* controlPanel = nullptr;
    if (message == WM_NCCREATE)
    {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        controlPanel = static_cast<RuntimeControlPanel*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(controlPanel));
    }
    else
    {
        controlPanel = reinterpret_cast<RuntimeControlPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (controlPanel != nullptr)
    {
        if (message == WM_NCCREATE)
        {
            controlPanel->hwnd_ = hwnd;
        }

        return controlPanel->HandleMessage(message, wparam, lparam);
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT RuntimeControlPanel::HandleMessage(const UINT message, const WPARAM wparam, const LPARAM lparam)
{
    switch (message)
    {
    case WM_CLOSE:
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    case WM_HSCROLL:
        if (!suppressControlNotifications_)
        {
            const HWND changedControl = reinterpret_cast<HWND>(lparam);
            if (changedControl == materialBaseColorRedSlider_.trackbarHandle
                || changedControl == materialBaseColorGreenSlider_.trackbarHandle
                || changedControl == materialBaseColorBlueSlider_.trackbarHandle
                || changedControl == materialBaseColorAlphaSlider_.trackbarHandle
                || changedControl == materialEmissiveRedSlider_.trackbarHandle
                || changedControl == materialEmissiveGreenSlider_.trackbarHandle
                || changedControl == materialEmissiveBlueSlider_.trackbarHandle
                || changedControl == materialMetallicSlider_.trackbarHandle
                || changedControl == materialRoughnessSlider_.trackbarHandle
                || changedControl == materialNormalScaleSlider_.trackbarHandle
                || changedControl == materialOcclusionSlider_.trackbarHandle
                || changedControl == materialAlphaCutoffSlider_.trackbarHandle)
            {
                MarkSelectedMaterialDirtyFromControls();
            }
            else
            {
                MarkSettingsDirtyFromControls();
            }
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == kAutoShaderReloadCheckboxControlId && HIWORD(wparam) == BN_CLICKED)
        {
            pendingAutoShaderReloadEnabled_ =
                SendMessageW(autoShaderReloadCheckbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            lastSyncedAutoShaderReloadEnabled_ = pendingAutoShaderReloadEnabled_;
            hasPendingAutoShaderReloadChange_ = true;
            return 0;
        }
        if (LOWORD(wparam) == kReloadShadersButtonControlId && HIWORD(wparam) == BN_CLICKED)
        {
            reloadShadersRequested_ = true;
            return 0;
        }
        if (LOWORD(wparam) == kSavePresetButtonControlId && HIWORD(wparam) == BN_CLICKED)
        {
            savePresetRequested_ = true;
            return 0;
        }
        if (LOWORD(wparam) == kLoadPresetButtonControlId && HIWORD(wparam) == BN_CLICKED)
        {
            loadPresetRequested_ = true;
            return 0;
        }
        if (LOWORD(wparam) == kExportRenderGraphButtonControlId && HIWORD(wparam) == BN_CLICKED)
        {
            exportRenderGraphRequested_ = true;
            return 0;
        }
        if (LOWORD(wparam) == kScreenshotCaptureButtonControlId && HIWORD(wparam) == BN_CLICKED)
        {
            screenshotCaptureRequested_ = true;
            return 0;
        }

        if (suppressControlNotifications_)
        {
            return 0;
        }

        if (LOWORD(wparam) == kResetDefaultsButtonControlId && HIWORD(wparam) == BN_CLICKED)
        {
            pendingSettings_ = RuntimeRenderSettings {};
            hasPendingSettings_ = true;
            lastSyncedSettings_ = pendingSettings_;
            ApplySettingsToControls(pendingSettings_);
            return 0;
        }

        if (LOWORD(wparam) == kDebugViewComboControlId && HIWORD(wparam) == CBN_SELCHANGE)
        {
            MarkSettingsDirtyFromControls();
            return 0;
        }
        if (LOWORD(wparam) == kMaterialComboControlId && HIWORD(wparam) == CBN_SELCHANGE)
        {
            const LRESULT selectedIndex = SendMessageW(materialCombo_, CB_GETCURSEL, 0, 0);
            if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < lastSyncedMaterials_.size())
            {
                selectedMaterialListIndex_ = static_cast<std::uint32_t>(selectedIndex);
                ApplyMaterialToControls(lastSyncedMaterials_[selectedMaterialListIndex_]);
            }
            return 0;
        }
        if (LOWORD(wparam) == kMaterialAlphaModeComboControlId && HIWORD(wparam) == CBN_SELCHANGE)
        {
            MarkSelectedMaterialDirtyFromControls();
            return 0;
        }
        if (LOWORD(wparam) == kMaterialDoubleSidedCheckboxControlId && HIWORD(wparam) == BN_CLICKED)
        {
            MarkSelectedMaterialDirtyFromControls();
            return 0;
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wparam == VK_F1)
        {
            ToggleVisibility();
            return 0;
        }
        if (wparam == VK_F5)
        {
            reloadShadersRequested_ = true;
            return 0;
        }
        if (wparam == VK_F6)
        {
            savePresetRequested_ = true;
            return 0;
        }
        if (wparam == VK_F7)
        {
            loadPresetRequested_ = true;
            return 0;
        }
        if (wparam == VK_F8)
        {
            exportRenderGraphRequested_ = true;
            return 0;
        }
        if (wparam == VK_F9)
        {
            screenshotCaptureRequested_ = true;
            return 0;
        }
        break;
    default:
        break;
    }

    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void RuntimeControlPanel::RegisterWindowClass()
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kWindowClassName;

    const ATOM classAtom = RegisterClassExW(&windowClass);
    if (classAtom == 0)
    {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(error), "RegisterClassExW(runtime control panel)");
        }
    }
}

void RuntimeControlPanel::CreateControls()
{
    auto createGroupBox = [&](const wchar_t* label, const int x, const int y, const int width, const int height)
    {
        const HWND handle = CreateWindowExW(
            0,
            L"BUTTON",
            label,
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            x,
            y,
            width,
            height,
            hwnd_,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        SetControlFont(handle);
    };

    createGroupBox(L"View", 12, 10, 400, 288);
    createGroupBox(L"Lighting", 12, 304, 400, 210);
    createGroupBox(L"Bloom", 12, 520, 400, 180);
    createGroupBox(L"Shadow", 12, 704, 400, 72);
    createGroupBox(L"Material Inspector", 430, 10, 400, 766);

    debugViewLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Debug View",
        WS_CHILD | WS_VISIBLE,
        24,
        38,
        100,
        20,
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(debugViewLabel_);

    debugViewCombo_ = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        138,
        34,
        220,
        160,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDebugViewComboControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(debugViewCombo_);
    const std::array<PostProcessDebugView, 8> debugViews = {
        PostProcessDebugView::Final,
        PostProcessDebugView::HdrScene,
        PostProcessDebugView::Bloom,
        PostProcessDebugView::Luminance,
        PostProcessDebugView::ShadowMap,
        PostProcessDebugView::Normal,
        PostProcessDebugView::Roughness,
        PostProcessDebugView::Metallic,
    };
    for (const PostProcessDebugView debugView : debugViews)
    {
        SendMessageW(debugViewCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ToWideString(debugView)));
    }

    autoShaderReloadCheckbox_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Auto Reload",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        24,
        70,
        108,
        22,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoShaderReloadCheckboxControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(autoShaderReloadCheckbox_);

    reloadShadersButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Reload Shaders",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        138,
        68,
        110,
        26,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReloadShadersButtonControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(reloadShadersButton_);

    savePresetButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Save Preset",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        138,
        100,
        110,
        26,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSavePresetButtonControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(savePresetButton_);

    loadPresetButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Load Preset",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        258,
        100,
        110,
        26,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLoadPresetButtonControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(loadPresetButton_);

    exportRenderGraphButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Export Graph",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        138,
        132,
        230,
        26,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExportRenderGraphButtonControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(exportRenderGraphButton_);

    screenshotCaptureButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Capture Screenshot",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        138,
        164,
        230,
        26,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScreenshotCaptureButtonControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(screenshotCaptureButton_);

    resetDefaultsButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Reset Defaults",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        258,
        68,
        110,
        26,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResetDefaultsButtonControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(resetDefaultsButton_);

    helpLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"F1 panel | F5 reload | F6/F7 preset | F8 graph | F9 shot | 1-8 debug | WASDQE orbit | R reset",
        WS_CHILD | WS_VISIBLE,
        24,
        198,
        372,
        18,
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(helpLabel_);

    shaderReloadStatusLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Shader reload status unavailable.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        24,
        218,
        372,
        18,
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(shaderReloadStatusLabel_);

    profilingLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Profiling: waiting for first rendered frame...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        24,
        240,
        372,
        44,
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(profilingLabel_);

    CreateTrackbar(L"Exposure", 24, 330, exposureSlider_, kExposureTrackbarControlId, 0.25f, 2.5f, 2);
    CreateTrackbar(L"Environment", 24, 368, environmentSlider_, kEnvironmentTrackbarControlId, 0.0f, 2.5f, 2);
    CreateTrackbar(L"IBL Diffuse", 24, 406, iblDiffuseSlider_, kIblDiffuseTrackbarControlId, 0.0f, 2.0f, 2);
    CreateTrackbar(L"IBL Specular", 24, 444, iblSpecularSlider_, kIblSpecularTrackbarControlId, 0.0f, 2.0f, 2);
    CreateTrackbar(
        L"Specular Blend",
        24,
        482,
        iblSpecularBlendSlider_,
        kIblSpecularBlendTrackbarControlId,
        0.0f,
        1.5f,
        2);

    CreateTrackbar(L"Threshold", 24, 546, bloomThresholdSlider_, kBloomThresholdTrackbarControlId, 0.25f, 2.5f, 2);
    CreateTrackbar(L"Soft Knee", 24, 584, bloomSoftKneeSlider_, kBloomSoftKneeTrackbarControlId, 0.05f, 1.0f, 2);
    CreateTrackbar(L"Intensity", 24, 622, bloomIntensitySlider_, kBloomIntensityTrackbarControlId, 0.0f, 1.5f, 2);
    CreateTrackbar(L"Radius", 24, 660, bloomRadiusSlider_, kBloomRadiusTrackbarControlId, 0.5f, 2.5f, 2);

    CreateTrackbar(L"Bias", 24, 730, shadowBiasSlider_, kShadowBiasTrackbarControlId, 0.0005f, 0.02f, 4);

    materialLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Material",
        WS_CHILD | WS_VISIBLE,
        442,
        38,
        100,
        20,
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(materialLabel_);

    materialCombo_ = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        556,
        34,
        220,
        160,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMaterialComboControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(materialCombo_);

    materialAlphaModeLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Alpha Mode",
        WS_CHILD | WS_VISIBLE,
        442,
        70,
        100,
        20,
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(materialAlphaModeLabel_);

    materialAlphaModeCombo_ = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        556,
        66,
        220,
        120,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMaterialAlphaModeComboControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(materialAlphaModeCombo_);
    SendMessageW(materialAlphaModeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ToWideString(MaterialAlphaMode::Opaque)));
    SendMessageW(materialAlphaModeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ToWideString(MaterialAlphaMode::Mask)));
    SendMessageW(materialAlphaModeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ToWideString(MaterialAlphaMode::Blend)));

    materialDoubleSidedCheckbox_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Double Sided",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        442,
        98,
        160,
        22,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMaterialDoubleSidedCheckboxControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(materialDoubleSidedCheckbox_);

    CreateTrackbar(L"Base R", 442, 132, materialBaseColorRedSlider_, kMaterialBaseColorRedTrackbarControlId, 0.0f, 2.0f, 2);
    CreateTrackbar(L"Base G", 442, 170, materialBaseColorGreenSlider_, kMaterialBaseColorGreenTrackbarControlId, 0.0f, 2.0f, 2);
    CreateTrackbar(L"Base B", 442, 208, materialBaseColorBlueSlider_, kMaterialBaseColorBlueTrackbarControlId, 0.0f, 2.0f, 2);
    CreateTrackbar(L"Base A", 442, 246, materialBaseColorAlphaSlider_, kMaterialBaseColorAlphaTrackbarControlId, 0.0f, 1.0f, 2);
    CreateTrackbar(L"Emissive R", 442, 284, materialEmissiveRedSlider_, kMaterialEmissiveRedTrackbarControlId, 0.0f, 4.0f, 2);
    CreateTrackbar(L"Emissive G", 442, 322, materialEmissiveGreenSlider_, kMaterialEmissiveGreenTrackbarControlId, 0.0f, 4.0f, 2);
    CreateTrackbar(L"Emissive B", 442, 360, materialEmissiveBlueSlider_, kMaterialEmissiveBlueTrackbarControlId, 0.0f, 4.0f, 2);
    CreateTrackbar(L"Metallic", 442, 398, materialMetallicSlider_, kMaterialMetallicTrackbarControlId, 0.0f, 1.0f, 2);
    CreateTrackbar(L"Roughness", 442, 436, materialRoughnessSlider_, kMaterialRoughnessTrackbarControlId, 0.0f, 1.0f, 2);
    CreateTrackbar(L"Normal Scale", 442, 474, materialNormalScaleSlider_, kMaterialNormalScaleTrackbarControlId, 0.0f, 4.0f, 2);
    CreateTrackbar(L"Occlusion", 442, 512, materialOcclusionSlider_, kMaterialOcclusionTrackbarControlId, 0.0f, 1.0f, 2);
    CreateTrackbar(L"Alpha Cutoff", 442, 550, materialAlphaCutoffSlider_, kMaterialAlphaCutoffTrackbarControlId, 0.0f, 1.0f, 2);

    materialHelpLabel_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Edits shared runtime material constants; glTF texture bindings stay intact.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        442,
        590,
        348,
        32,
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(materialHelpLabel_);
}

void RuntimeControlPanel::CreateTrackbar(
    const wchar_t* label,
    const int x,
    const int y,
    SliderControl& slider,
    const int trackbarControlId,
    const float minValue,
    const float maxValue,
    const int precision)
{
    slider.minValue = minValue;
    slider.maxValue = maxValue;
    slider.precision = precision;

    slider.labelHandle = CreateWindowExW(
        0,
        L"STATIC",
        label,
        WS_CHILD | WS_VISIBLE,
        x,
        y,
        110,
        20,
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(slider.labelHandle);

    slider.trackbarHandle = CreateWindowExW(
        0,
        TRACKBAR_CLASSW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
        x + 112,
        y - 4,
        180,
        30,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(trackbarControlId)),
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(slider.trackbarHandle, TBM_SETRANGEMIN, FALSE, 0);
    SendMessageW(slider.trackbarHandle, TBM_SETRANGEMAX, FALSE, kTrackbarResolution);
    SendMessageW(slider.trackbarHandle, TBM_SETPAGESIZE, 0, 100);
    SendMessageW(slider.trackbarHandle, TBM_SETTICFREQ, 100, 0);

    slider.valueHandle = CreateWindowExW(
        0,
        L"STATIC",
        L"0.00",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        x + 300,
        y,
        70,
        20,
        hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    SetControlFont(slider.valueHandle);
}

void RuntimeControlPanel::ApplySettingsToControls(const RuntimeRenderSettings& settings)
{
    if (hwnd_ == nullptr)
    {
        return;
    }

    suppressControlNotifications_ = true;

    SendMessageW(
        debugViewCombo_,
        CB_SETCURSEL,
        static_cast<WPARAM>(settings.debugView),
        0);

    const auto applySlider = [&](const SliderControl& slider, const float value)
    {
        SendMessageW(slider.trackbarHandle, TBM_SETPOS, TRUE, FloatToTrackbarPosition(slider, value));
        UpdateValueLabel(slider, value);
    };

    applySlider(exposureSlider_, settings.exposure);
    applySlider(environmentSlider_, settings.environmentIntensity);
    applySlider(iblDiffuseSlider_, settings.iblDiffuseIntensity);
    applySlider(iblSpecularSlider_, settings.iblSpecularIntensity);
    applySlider(iblSpecularBlendSlider_, settings.iblSpecularBlend);
    applySlider(bloomThresholdSlider_, settings.bloomThreshold);
    applySlider(bloomSoftKneeSlider_, settings.bloomSoftKnee);
    applySlider(bloomIntensitySlider_, settings.bloomIntensity);
    applySlider(bloomRadiusSlider_, settings.bloomRadius);
    applySlider(shadowBiasSlider_, settings.shadowBias);

    suppressControlNotifications_ = false;
}

void RuntimeControlPanel::ApplyMaterialToControls(const RuntimeMaterialInfo& material)
{
    if (hwnd_ == nullptr)
    {
        return;
    }

    suppressControlNotifications_ = true;

    SendMessageW(materialCombo_, CB_SETCURSEL, selectedMaterialListIndex_, 0);
    SendMessageW(materialAlphaModeCombo_, CB_SETCURSEL, ToComboIndex(material.desc.alphaMode), 0);
    SendMessageW(
        materialDoubleSidedCheckbox_,
        BM_SETCHECK,
        material.desc.doubleSided ? BST_CHECKED : BST_UNCHECKED,
        0);

    const auto applySlider = [&](const SliderControl& slider, const float value)
    {
        SendMessageW(slider.trackbarHandle, TBM_SETPOS, TRUE, FloatToTrackbarPosition(slider, value));
        UpdateValueLabel(slider, value);
    };

    applySlider(materialBaseColorRedSlider_, material.desc.constants.baseColorFactor.x);
    applySlider(materialBaseColorGreenSlider_, material.desc.constants.baseColorFactor.y);
    applySlider(materialBaseColorBlueSlider_, material.desc.constants.baseColorFactor.z);
    applySlider(materialBaseColorAlphaSlider_, material.desc.constants.baseColorFactor.w);
    applySlider(materialEmissiveRedSlider_, material.desc.constants.emissiveFactorAndMetallic.x);
    applySlider(materialEmissiveGreenSlider_, material.desc.constants.emissiveFactorAndMetallic.y);
    applySlider(materialEmissiveBlueSlider_, material.desc.constants.emissiveFactorAndMetallic.z);
    applySlider(materialMetallicSlider_, material.desc.constants.emissiveFactorAndMetallic.w);
    applySlider(materialRoughnessSlider_, material.desc.constants.roughnessUvScaleAlphaCutoff.x);
    applySlider(materialNormalScaleSlider_, material.desc.constants.textureControls.x);
    applySlider(materialOcclusionSlider_, material.desc.constants.textureControls.y);
    applySlider(materialAlphaCutoffSlider_, material.desc.constants.roughnessUvScaleAlphaCutoff.w);

    suppressControlNotifications_ = false;
}

RuntimeRenderSettings RuntimeControlPanel::BuildSettingsFromControls() const
{
    RuntimeRenderSettings settings = {};
    settings.debugView = static_cast<PostProcessDebugView>(SendMessageW(debugViewCombo_, CB_GETCURSEL, 0, 0));
    settings.exposure = TrackbarPositionToFloat(exposureSlider_);
    settings.environmentIntensity = TrackbarPositionToFloat(environmentSlider_);
    settings.iblDiffuseIntensity = TrackbarPositionToFloat(iblDiffuseSlider_);
    settings.iblSpecularIntensity = TrackbarPositionToFloat(iblSpecularSlider_);
    settings.iblSpecularBlend = TrackbarPositionToFloat(iblSpecularBlendSlider_);
    settings.bloomThreshold = TrackbarPositionToFloat(bloomThresholdSlider_);
    settings.bloomSoftKnee = TrackbarPositionToFloat(bloomSoftKneeSlider_);
    settings.bloomIntensity = TrackbarPositionToFloat(bloomIntensitySlider_);
    settings.bloomRadius = TrackbarPositionToFloat(bloomRadiusSlider_);
    settings.shadowBias = TrackbarPositionToFloat(shadowBiasSlider_);
    return ClampRuntimeRenderSettings(settings);
}

MaterialDesc RuntimeControlPanel::BuildMaterialFromControls() const
{
    if (lastSyncedMaterials_.empty() || selectedMaterialListIndex_ >= lastSyncedMaterials_.size())
    {
        return {};
    }

    MaterialDesc material = lastSyncedMaterials_[selectedMaterialListIndex_].desc;
    material.alphaMode = ToMaterialAlphaMode(SendMessageW(materialAlphaModeCombo_, CB_GETCURSEL, 0, 0));
    material.doubleSided =
        SendMessageW(materialDoubleSidedCheckbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    material.constants.baseColorFactor = {
        TrackbarPositionToFloat(materialBaseColorRedSlider_),
        TrackbarPositionToFloat(materialBaseColorGreenSlider_),
        TrackbarPositionToFloat(materialBaseColorBlueSlider_),
        TrackbarPositionToFloat(materialBaseColorAlphaSlider_)};
    material.constants.emissiveFactorAndMetallic = {
        TrackbarPositionToFloat(materialEmissiveRedSlider_),
        TrackbarPositionToFloat(materialEmissiveGreenSlider_),
        TrackbarPositionToFloat(materialEmissiveBlueSlider_),
        TrackbarPositionToFloat(materialMetallicSlider_)};
    material.constants.roughnessUvScaleAlphaCutoff.x = TrackbarPositionToFloat(materialRoughnessSlider_);
    material.constants.roughnessUvScaleAlphaCutoff.w = TrackbarPositionToFloat(materialAlphaCutoffSlider_);
    material.constants.textureControls.x = TrackbarPositionToFloat(materialNormalScaleSlider_);
    material.constants.textureControls.y = TrackbarPositionToFloat(materialOcclusionSlider_);
    return ClampRuntimeMaterialDesc(material);
}

void RuntimeControlPanel::MarkSettingsDirtyFromControls()
{
    pendingSettings_ = BuildSettingsFromControls();
    hasPendingSettings_ = true;
    lastSyncedSettings_ = pendingSettings_;

    UpdateValueLabel(exposureSlider_, pendingSettings_.exposure);
    UpdateValueLabel(environmentSlider_, pendingSettings_.environmentIntensity);
    UpdateValueLabel(iblDiffuseSlider_, pendingSettings_.iblDiffuseIntensity);
    UpdateValueLabel(iblSpecularSlider_, pendingSettings_.iblSpecularIntensity);
    UpdateValueLabel(iblSpecularBlendSlider_, pendingSettings_.iblSpecularBlend);
    UpdateValueLabel(bloomThresholdSlider_, pendingSettings_.bloomThreshold);
    UpdateValueLabel(bloomSoftKneeSlider_, pendingSettings_.bloomSoftKnee);
    UpdateValueLabel(bloomIntensitySlider_, pendingSettings_.bloomIntensity);
    UpdateValueLabel(bloomRadiusSlider_, pendingSettings_.bloomRadius);
    UpdateValueLabel(shadowBiasSlider_, pendingSettings_.shadowBias);
}

void RuntimeControlPanel::MarkSelectedMaterialDirtyFromControls()
{
    if (lastSyncedMaterials_.empty() || selectedMaterialListIndex_ >= lastSyncedMaterials_.size())
    {
        return;
    }

    pendingMaterialRuntimeIndex_ = lastSyncedMaterials_[selectedMaterialListIndex_].runtimeIndex;
    pendingMaterialDesc_ = BuildMaterialFromControls();
    lastSyncedMaterials_[selectedMaterialListIndex_].desc = pendingMaterialDesc_;
    hasPendingMaterialChange_ = true;
    UpdateMaterialValueLabels(pendingMaterialDesc_);
}

void RuntimeControlPanel::UpdateValueLabel(const SliderControl& slider, const float value) const
{
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%.*f", slider.precision, value);
    SetWindowTextW(slider.valueHandle, buffer);
}

void RuntimeControlPanel::UpdateMaterialValueLabels(const MaterialDesc& material) const
{
    UpdateValueLabel(materialBaseColorRedSlider_, material.constants.baseColorFactor.x);
    UpdateValueLabel(materialBaseColorGreenSlider_, material.constants.baseColorFactor.y);
    UpdateValueLabel(materialBaseColorBlueSlider_, material.constants.baseColorFactor.z);
    UpdateValueLabel(materialBaseColorAlphaSlider_, material.constants.baseColorFactor.w);
    UpdateValueLabel(materialEmissiveRedSlider_, material.constants.emissiveFactorAndMetallic.x);
    UpdateValueLabel(materialEmissiveGreenSlider_, material.constants.emissiveFactorAndMetallic.y);
    UpdateValueLabel(materialEmissiveBlueSlider_, material.constants.emissiveFactorAndMetallic.z);
    UpdateValueLabel(materialMetallicSlider_, material.constants.emissiveFactorAndMetallic.w);
    UpdateValueLabel(materialRoughnessSlider_, material.constants.roughnessUvScaleAlphaCutoff.x);
    UpdateValueLabel(materialNormalScaleSlider_, material.constants.textureControls.x);
    UpdateValueLabel(materialOcclusionSlider_, material.constants.textureControls.y);
    UpdateValueLabel(materialAlphaCutoffSlider_, material.constants.roughnessUvScaleAlphaCutoff.w);
}

int RuntimeControlPanel::FloatToTrackbarPosition(const SliderControl& slider, const float value) const
{
    const float normalizedValue =
        (std::clamp(value, slider.minValue, slider.maxValue) - slider.minValue)
        / std::max(slider.maxValue - slider.minValue, 1e-4f);
    return static_cast<int>(std::round(normalizedValue * static_cast<float>(kTrackbarResolution)));
}

float RuntimeControlPanel::TrackbarPositionToFloat(const SliderControl& slider) const
{
    const int position = static_cast<int>(SendMessageW(slider.trackbarHandle, TBM_GETPOS, 0, 0));
    const float normalizedValue = static_cast<float>(position) / static_cast<float>(kTrackbarResolution);
    return slider.minValue + normalizedValue * (slider.maxValue - slider.minValue);
}

void RuntimeControlPanel::SetControlFont(const HWND handle) const
{
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
}
} // namespace ugc_renderer
