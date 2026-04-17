#include "ugc_renderer/app/application.h"
#include "ugc_renderer/core/logger.h"

#include <Windows.h>

#include <exception>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        ugc_renderer::Application application;
        return application.Run();
    }
    catch (const std::exception& exception)
    {
        ugc_renderer::Logger::Error(exception.what());
        MessageBoxA(nullptr, exception.what(), "UGC Renderer Lab", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
}
