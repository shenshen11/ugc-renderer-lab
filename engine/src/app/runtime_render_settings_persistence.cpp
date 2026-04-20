#include "ugc_renderer/app/runtime_render_settings_persistence.h"

#include "ugc_renderer/core/logger.h"
#include "ugc_renderer/core/throw_if_failed.h"

#include <Windows.h>

#include <array>
#include <cwchar>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace ugc_renderer
{
namespace
{
[[nodiscard]] std::wstring_view Trim(const std::wstring_view value) noexcept
{
    const std::size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos)
    {
        return {};
    }

    const std::size_t last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] std::optional<float> ParseFloat(const std::wstring_view value) noexcept
{
    if (value.empty())
    {
        return std::nullopt;
    }

    float parsedValue = 0.0f;
    const wchar_t* begin = value.data();
    wchar_t* end = nullptr;
    parsedValue = std::wcstof(begin, &end);
    if (end == begin)
    {
        return std::nullopt;
    }

    const std::wstring_view remaining = Trim(std::wstring_view(end, value.data() + value.size() - end));
    if (!remaining.empty())
    {
        return std::nullopt;
    }

    return parsedValue;
}

[[nodiscard]] std::optional<PostProcessDebugView> ParseDebugView(const std::wstring_view value) noexcept
{
    if (value == L"Final")
    {
        return PostProcessDebugView::Final;
    }
    if (value == L"HDR Scene")
    {
        return PostProcessDebugView::HdrScene;
    }
    if (value == L"Bloom")
    {
        return PostProcessDebugView::Bloom;
    }
    if (value == L"Luminance")
    {
        return PostProcessDebugView::Luminance;
    }
    if (value == L"Shadow Map")
    {
        return PostProcessDebugView::ShadowMap;
    }
    if (value == L"Normal")
    {
        return PostProcessDebugView::Normal;
    }
    if (value == L"Roughness")
    {
        return PostProcessDebugView::Roughness;
    }
    if (value == L"Metallic")
    {
        return PostProcessDebugView::Metallic;
    }

    return std::nullopt;
}

[[nodiscard]] std::filesystem::path GetExecutableDirectory()
{
    std::array<wchar_t, MAX_PATH> modulePath = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length == modulePath.size())
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "GetModuleFileNameW");
    }

    return std::filesystem::path(modulePath.data()).parent_path();
}
} // namespace

std::filesystem::path GetRuntimeRenderSettingsPresetPath()
{
    return GetExecutableDirectory() / L"workflow" / L"runtime_render_settings.cfg";
}

bool SaveRuntimeRenderSettingsPreset(const RuntimeRenderSettings& settings)
{
    const std::filesystem::path presetPath = GetRuntimeRenderSettingsPresetPath();
    std::error_code errorCode;
    std::filesystem::create_directories(presetPath.parent_path(), errorCode);
    if (errorCode)
    {
        Logger::Error("Failed to create runtime preset directory.");
        return false;
    }

    std::ofstream output(presetPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        Logger::Error("Failed to open runtime preset file for writing.");
        return false;
    }

    output << "version=1\n";
    output << "environmentIntensity=" << settings.environmentIntensity << '\n';
    output << "exposure=" << settings.exposure << '\n';
    output << "shadowBias=" << settings.shadowBias << '\n';
    output << "bloomThreshold=" << settings.bloomThreshold << '\n';
    output << "bloomSoftKnee=" << settings.bloomSoftKnee << '\n';
    output << "bloomIntensity=" << settings.bloomIntensity << '\n';
    output << "bloomRadius=" << settings.bloomRadius << '\n';
    output << "iblDiffuseIntensity=" << settings.iblDiffuseIntensity << '\n';
    output << "iblSpecularIntensity=" << settings.iblSpecularIntensity << '\n';
    output << "iblSpecularBlend=" << settings.iblSpecularBlend << '\n';
    output << "debugView=" << ToString(settings.debugView) << '\n';

    if (!output.good())
    {
        Logger::Error("Failed while writing runtime preset file.");
        return false;
    }

    Logger::Info(std::string("Saved runtime render preset to: ") + presetPath.string());
    return true;
}

bool LoadRuntimeRenderSettingsPreset(RuntimeRenderSettings& settings)
{
    const std::filesystem::path presetPath = GetRuntimeRenderSettingsPresetPath();
    std::ifstream input(presetPath, std::ios::binary);
    if (!input.is_open())
    {
        Logger::Error(std::string("Runtime render preset not found: ") + presetPath.string());
        return false;
    }

    RuntimeRenderSettings loadedSettings = {};
    std::string line;
    while (std::getline(input, line))
    {
        const std::wstring wideLine(line.begin(), line.end());
        const std::wstring_view trimmedLine = Trim(wideLine);
        if (trimmedLine.empty() || trimmedLine.starts_with(L'#'))
        {
            continue;
        }

        const std::size_t separator = trimmedLine.find(L'=');
        if (separator == std::wstring_view::npos)
        {
            Logger::Error("Runtime preset parse failed: missing '=' separator.");
            return false;
        }

        const std::wstring_view key = Trim(trimmedLine.substr(0, separator));
        const std::wstring_view value = Trim(trimmedLine.substr(separator + 1));

        if (key == L"version")
        {
            continue;
        }
        if (key == L"debugView")
        {
            const std::optional<PostProcessDebugView> debugView = ParseDebugView(value);
            if (!debugView.has_value())
            {
                Logger::Error("Runtime preset parse failed: invalid debugView.");
                return false;
            }

            loadedSettings.debugView = *debugView;
            continue;
        }

        const std::optional<float> parsedValue = ParseFloat(value);
        if (!parsedValue.has_value())
        {
            Logger::Error("Runtime preset parse failed: invalid numeric value.");
            return false;
        }

        if (key == L"environmentIntensity")
        {
            loadedSettings.environmentIntensity = *parsedValue;
        }
        else if (key == L"exposure")
        {
            loadedSettings.exposure = *parsedValue;
        }
        else if (key == L"shadowBias")
        {
            loadedSettings.shadowBias = *parsedValue;
        }
        else if (key == L"bloomThreshold")
        {
            loadedSettings.bloomThreshold = *parsedValue;
        }
        else if (key == L"bloomSoftKnee")
        {
            loadedSettings.bloomSoftKnee = *parsedValue;
        }
        else if (key == L"bloomIntensity")
        {
            loadedSettings.bloomIntensity = *parsedValue;
        }
        else if (key == L"bloomRadius")
        {
            loadedSettings.bloomRadius = *parsedValue;
        }
        else if (key == L"iblDiffuseIntensity")
        {
            loadedSettings.iblDiffuseIntensity = *parsedValue;
        }
        else if (key == L"iblSpecularIntensity")
        {
            loadedSettings.iblSpecularIntensity = *parsedValue;
        }
        else if (key == L"iblSpecularBlend")
        {
            loadedSettings.iblSpecularBlend = *parsedValue;
        }
    }

    if (!input.good() && !input.eof())
    {
        Logger::Error("Failed while reading runtime preset file.");
        return false;
    }

    settings = loadedSettings;
    Logger::Info(std::string("Loaded runtime render preset from: ") + presetPath.string());
    return true;
}
} // namespace ugc_renderer
