#pragma once

#include "ugc_renderer/render/runtime_render_settings.h"

#include <filesystem>

namespace ugc_renderer
{
[[nodiscard]] std::filesystem::path GetRuntimeRenderSettingsPresetPath();
bool SaveRuntimeRenderSettingsPreset(const RuntimeRenderSettings& settings);
bool LoadRuntimeRenderSettingsPreset(RuntimeRenderSettings& settings);
} // namespace ugc_renderer
