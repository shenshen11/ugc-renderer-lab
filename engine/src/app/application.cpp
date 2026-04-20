#include "ugc_renderer/app/application.h"

#include "ugc_renderer/app/runtime_render_settings_persistence.h"
#include "ugc_renderer/core/logger.h"
#include "ugc_renderer/core/throw_if_failed.h"
#include "ugc_renderer/platform/runtime_control_panel.h"
#include "ugc_renderer/platform/window.h"
#include "ugc_renderer/render/d3d12_renderer.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>

namespace ugc_renderer
{
ComInitializer::ComInitializer()
{
    const HRESULT result = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
    if (result == RPC_E_CHANGED_MODE)
    {
        Logger::Info("COM apartment already initialized with a different model; continuing with existing COM state.");
        return;
    }

    ThrowIfFailed(result, "CoInitializeEx");
    shouldUninitialize_ = true;
}

ComInitializer::~ComInitializer()
{
    if (shouldUninitialize_)
    {
        CoUninitialize();
    }
}

Application::Application()
{
    window_ = std::make_unique<Window>(L"UGC Renderer Lab", 1600, 900);
    renderer_ = std::make_unique<D3D12Renderer>(*window_);
    runtimeControlPanel_ =
        std::make_unique<RuntimeControlPanel>(
            window_->GetNativeHandle(),
            renderer_->GetRuntimeRenderSettings(),
            renderer_->IsAutoShaderReloadEnabled());
    runtimeControlPanel_->SyncMaterials(renderer_->GetRuntimeMaterials());
    runtimeControlPanel_->UpdateShaderReloadStatusText(renderer_->GetShaderReloadStatus());
    Logger::Info("Application initialized.");
}

Application::~Application()
{
    if (renderer_ != nullptr)
    {
        renderer_->WaitForIdle();
    }

    runtimeControlPanel_.reset();
    renderer_.reset();
    window_.reset();
}

int Application::Run()
{
    Logger::Info("Entering main loop.");
    auto previousFrameTime = std::chrono::steady_clock::now();

    while (window_->ProcessMessages())
    {
        if (window_->IsMinimized() || window_->IsInSizeMove())
        {
            Sleep(10);
            continue;
        }

        const auto currentFrameTime = std::chrono::steady_clock::now();
        const float deltaTimeSeconds =
            std::min(std::chrono::duration<float>(currentFrameTime - previousFrameTime).count(), 0.1f);
        previousFrameTime = currentFrameTime;

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (window_->ConsumeResize(width, height))
        {
            renderer_->Resize(width, height);
        }

        renderer_->Update(deltaTimeSeconds);

        const bool panelTogglePressed = window_->IsKeyDown(VK_F1);
        if (panelTogglePressed && !panelTogglePressedLastFrame_ && runtimeControlPanel_ != nullptr)
        {
            runtimeControlPanel_->ToggleVisibility();
        }
        panelTogglePressedLastFrame_ = panelTogglePressed;

        const bool shaderReloadPressed = window_->IsKeyDown(VK_F5);
        if (shaderReloadPressed && !shaderReloadPressedLastFrame_)
        {
            renderer_->ReloadShaders();
        }
        shaderReloadPressedLastFrame_ = shaderReloadPressed;

        const bool savePresetPressed = window_->IsKeyDown(VK_F6);
        if (savePresetPressed && !savePresetPressedLastFrame_)
        {
            SaveRuntimeRenderSettingsPreset(renderer_->GetRuntimeRenderSettings());
        }
        savePresetPressedLastFrame_ = savePresetPressed;

        const bool loadPresetPressed = window_->IsKeyDown(VK_F7);
        if (loadPresetPressed && !loadPresetPressedLastFrame_)
        {
            RuntimeRenderSettings loadedSettings = {};
            if (LoadRuntimeRenderSettingsPreset(loadedSettings))
            {
                renderer_->SetRuntimeRenderSettings(loadedSettings);
            }
        }
        loadPresetPressedLastFrame_ = loadPresetPressed;

        const bool renderGraphExportPressed = window_->IsKeyDown(VK_F8);
        if (renderGraphExportPressed && !renderGraphExportPressedLastFrame_)
        {
            renderer_->RequestRenderGraphSnapshotExport();
        }
        renderGraphExportPressedLastFrame_ = renderGraphExportPressed;

        const bool screenshotCapturePressed = window_->IsKeyDown(VK_F9);
        if (screenshotCapturePressed && !screenshotCapturePressedLastFrame_)
        {
            renderer_->RequestScreenshotCapture();
        }
        screenshotCapturePressedLastFrame_ = screenshotCapturePressed;

        if (runtimeControlPanel_ != nullptr)
        {
            RuntimeRenderSettings updatedSettings = {};
            if (runtimeControlPanel_->ConsumeSettingsChange(updatedSettings))
            {
                renderer_->SetRuntimeRenderSettings(updatedSettings);
            }

            std::uint32_t updatedMaterialIndex = 0;
            MaterialDesc updatedMaterial = {};
            if (runtimeControlPanel_->ConsumeMaterialChange(updatedMaterialIndex, updatedMaterial))
            {
                renderer_->UpdateRuntimeMaterial(updatedMaterialIndex, updatedMaterial);
            }

            bool autoShaderReloadEnabled = false;
            if (runtimeControlPanel_->ConsumeAutoShaderReloadChange(autoShaderReloadEnabled))
            {
                renderer_->SetAutoShaderReloadEnabled(autoShaderReloadEnabled);
            }

            if (runtimeControlPanel_->ConsumeShaderReloadRequest())
            {
                renderer_->ReloadShaders();
            }

            if (runtimeControlPanel_->ConsumeSavePresetRequest())
            {
                SaveRuntimeRenderSettingsPreset(renderer_->GetRuntimeRenderSettings());
            }

            if (runtimeControlPanel_->ConsumeLoadPresetRequest())
            {
                RuntimeRenderSettings loadedSettings = {};
                if (LoadRuntimeRenderSettingsPreset(loadedSettings))
                {
                    renderer_->SetRuntimeRenderSettings(loadedSettings);
                }
            }

            if (runtimeControlPanel_->ConsumeRenderGraphExportRequest())
            {
                renderer_->RequestRenderGraphSnapshotExport();
            }

            if (runtimeControlPanel_->ConsumeScreenshotCaptureRequest())
            {
                renderer_->RequestScreenshotCapture();
            }

            runtimeControlPanel_->SyncFromRenderer(renderer_->GetRuntimeRenderSettings());
            runtimeControlPanel_->SyncMaterials(renderer_->GetRuntimeMaterials());
            runtimeControlPanel_->SyncAutoShaderReloadEnabled(renderer_->IsAutoShaderReloadEnabled());
        }

        renderer_->Render();
        if (runtimeControlPanel_ != nullptr)
        {
            runtimeControlPanel_->UpdateProfilingText(renderer_->GetRenderGraphProfileSnapshot());
            runtimeControlPanel_->UpdateShaderReloadStatusText(renderer_->GetShaderReloadStatus());
        }
    }

    renderer_->WaitForIdle();
    Logger::Info("Application shutdown complete.");
    return 0;
}
} // namespace ugc_renderer
