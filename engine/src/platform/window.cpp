#include "ugc_renderer/platform/window.h"

#include "ugc_renderer/core/throw_if_failed.h"

#include <stdexcept>

namespace ugc_renderer
{
Window::Window(std::wstring title, const std::uint32_t width, const std::uint32_t height)
    : instance_(GetModuleHandleW(nullptr))
    , title_(std::move(title))
    , width_(width)
    , height_(height)
{
    RegisterWindowClass();

    constexpr DWORD windowStyle = WS_OVERLAPPEDWINDOW;

    RECT windowRect = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRectEx(&windowRect, windowStyle, FALSE, 0);

    hwnd_ = CreateWindowExW(
        0,
        kWindowClassName,
        title_.c_str(),
        windowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance_,
        this);

    if (hwnd_ == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "CreateWindowExW");
    }

    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);
}

Window::~Window()
{
    if (hwnd_ != nullptr && IsWindow(hwnd_) != FALSE)
    {
        DestroyWindow(hwnd_);
    }

    UnregisterClassW(kWindowClassName, instance_);
}

bool Window::ProcessMessages()
{
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
    {
        if (message.message == WM_QUIT)
        {
            return false;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return true;
}

bool Window::ConsumeResize(std::uint32_t& width, std::uint32_t& height)
{
    if (!resized_)
    {
        return false;
    }

    resized_ = false;
    width = width_;
    height = height_;
    return true;
}

HWND Window::GetNativeHandle() const noexcept
{
    return hwnd_;
}

std::uint32_t Window::GetClientWidth() const noexcept
{
    return width_;
}

std::uint32_t Window::GetClientHeight() const noexcept
{
    return height_;
}

bool Window::IsMinimized() const noexcept
{
    return minimized_;
}

bool Window::IsInSizeMove() const noexcept
{
    return inSizeMove_;
}

LRESULT CALLBACK Window::WndProc(const HWND hwnd, const UINT message, const WPARAM wparam, const LPARAM lparam)
{
    Window* window = nullptr;

    if (message == WM_NCCREATE)
    {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = static_cast<Window*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        window->hwnd_ = hwnd;
    }
    else
    {
        window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (window != nullptr)
    {
        return window->HandleMessage(message, wparam, lparam);
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT Window::HandleMessage(const UINT message, const WPARAM wparam, const LPARAM lparam)
{
    switch (message)
    {
    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        width_ = static_cast<std::uint32_t>(LOWORD(lparam));
        height_ = static_cast<std::uint32_t>(HIWORD(lparam));
        minimized_ = (wparam == SIZE_MINIMIZED);

        if (!minimized_ && !inSizeMove_ && width_ > 0 && height_ > 0)
        {
            resized_ = true;
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        inSizeMove_ = true;
        return 0;
    case WM_EXITSIZEMOVE:
        inSizeMove_ = false;
        if (!minimized_ && width_ > 0 && height_ > 0)
        {
            resized_ = true;
        }
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wparam, lparam);
    }
}

void Window::RegisterWindowClass()
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;

    const ATOM classAtom = RegisterClassExW(&windowClass);
    if (classAtom == 0)
    {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(error), "RegisterClassExW");
        }
    }
}
} // namespace ugc_renderer
