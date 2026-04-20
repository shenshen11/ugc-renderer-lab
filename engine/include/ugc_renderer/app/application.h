#pragma once

#include <memory>

namespace ugc_renderer
{
class ComInitializer
{
public:
    ComInitializer();
    ~ComInitializer();

    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;
    ComInitializer(ComInitializer&&) = delete;
    ComInitializer& operator=(ComInitializer&&) = delete;

private:
    bool shouldUninitialize_ = false;
};

class D3D12Renderer;
class RuntimeControlPanel;
class Window;

class Application
{
public:
    Application();
    ~Application();

    int Run();

private:
    ComInitializer comInitializer_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<D3D12Renderer> renderer_;
    std::unique_ptr<RuntimeControlPanel> runtimeControlPanel_;
    bool panelTogglePressedLastFrame_ = false;
    bool shaderReloadPressedLastFrame_ = false;
    bool savePresetPressedLastFrame_ = false;
    bool loadPresetPressedLastFrame_ = false;
    bool renderGraphExportPressedLastFrame_ = false;
    bool screenshotCapturePressedLastFrame_ = false;
};
} // namespace ugc_renderer
