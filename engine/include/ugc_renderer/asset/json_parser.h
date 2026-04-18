#pragma once

#include "ugc_renderer/asset/json_value.h"

#include <string_view>

namespace ugc_renderer
{
class JsonParser
{
public:
    static JsonValue Parse(std::string_view text);
};
} // namespace ugc_renderer
