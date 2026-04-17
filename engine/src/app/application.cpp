#include "ugc_renderer/app/application.h"

#include "ugc_renderer/core/logger.h"
#include "ugc_renderer/platform/window.h"
#include "ugc_renderer/render/d3d12_renderer.h"

#include <Windows.h>

namespace ugc_renderer
{
Application::Application()
    : window_(std::make_unique<Window>(L"UGC Renderer Lab", 1600, 900))
    , renderer_(std::make_unique<D3D12Renderer>(*window_))
{
    Logger::Info("Application initialized.");
}

Application::~Application()
{
    if (renderer_ != nullptr)
    {
        renderer_->WaitForIdle();
    }
}

int Application::Run()
{
    Logger::Info("Entering main loop.");

    while (window_->ProcessMessages())
    {
        if (window_->IsMinimized())
        {
            Sleep(10);
            continue;
        }

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (window_->ConsumeResize(width, height))
        {
            renderer_->Resize(width, height);
        }

        renderer_->Render();
    }

    renderer_->WaitForIdle();
    Logger::Info("Application shutdown complete.");
    return 0;
}
} // namespace ugc_renderer
