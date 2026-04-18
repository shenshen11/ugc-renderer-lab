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
};
} // namespace ugc_renderer
