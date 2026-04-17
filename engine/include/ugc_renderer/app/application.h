#pragma once

#include <memory>

namespace ugc_renderer
{
class D3D12Renderer;
class Window;

class Application
{
public:
    Application();
    ~Application();

    int Run();

private:
    std::unique_ptr<Window> window_;
    std::unique_ptr<D3D12Renderer> renderer_;
};
} // namespace ugc_renderer
