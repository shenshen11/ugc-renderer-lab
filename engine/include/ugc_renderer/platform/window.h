#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

namespace ugc_renderer
{
class Window
{
public:
    Window(std::wstring title, std::uint32_t width, std::uint32_t height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    bool ProcessMessages();
    bool ConsumeResize(std::uint32_t& width, std::uint32_t& height);

    HWND GetNativeHandle() const noexcept;
    std::uint32_t GetClientWidth() const noexcept;
    std::uint32_t GetClientHeight() const noexcept;
    bool IsMinimized() const noexcept;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void RegisterWindowClass();

    static constexpr const wchar_t* kWindowClassName = L"UGCRendererWindowClass";

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    std::wstring title_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool resized_ = false;
    bool minimized_ = false;
};
} // namespace ugc_renderer
