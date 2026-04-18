#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ugc_renderer
{
class JsonValue
{
public:
    using Array = std::vector<JsonValue>;
    using Object = std::unordered_map<std::string, JsonValue>;

    JsonValue() = default;
    JsonValue(std::nullptr_t) noexcept;
    JsonValue(bool value) noexcept;
    JsonValue(double value) noexcept;
    JsonValue(std::string value);
    JsonValue(Array value);
    JsonValue(Object value);

    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type GetType() const noexcept;

    bool IsNull() const noexcept;
    bool IsBool() const noexcept;
    bool IsNumber() const noexcept;
    bool IsString() const noexcept;
    bool IsArray() const noexcept;
    bool IsObject() const noexcept;

    bool AsBool() const;
    double AsNumber() const;
    const std::string& AsString() const;
    const Array& AsArray() const;
    const Object& AsObject() const;

    const JsonValue* FindMember(std::string_view key) const;

private:
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    template<typename TValue>
    const TValue& GetAs(const char* expectedType) const
    {
        if (const auto* value = std::get_if<TValue>(&storage_); value != nullptr)
        {
            return *value;
        }

        throw std::logic_error(expectedType);
    }

    Storage storage_ = nullptr;
};

inline JsonValue::JsonValue(std::nullptr_t) noexcept
    : storage_(nullptr)
{
}

inline JsonValue::JsonValue(const bool value) noexcept
    : storage_(value)
{
}

inline JsonValue::JsonValue(const double value) noexcept
    : storage_(value)
{
}

inline JsonValue::JsonValue(std::string value)
    : storage_(std::move(value))
{
}

inline JsonValue::JsonValue(Array value)
    : storage_(std::move(value))
{
}

inline JsonValue::JsonValue(Object value)
    : storage_(std::move(value))
{
}

inline JsonValue::Type JsonValue::GetType() const noexcept
{
    switch (storage_.index())
    {
    case 0:
        return Type::Null;
    case 1:
        return Type::Bool;
    case 2:
        return Type::Number;
    case 3:
        return Type::String;
    case 4:
        return Type::Array;
    case 5:
        return Type::Object;
    default:
        return Type::Null;
    }
}

inline bool JsonValue::IsNull() const noexcept
{
    return std::holds_alternative<std::nullptr_t>(storage_);
}

inline bool JsonValue::IsBool() const noexcept
{
    return std::holds_alternative<bool>(storage_);
}

inline bool JsonValue::IsNumber() const noexcept
{
    return std::holds_alternative<double>(storage_);
}

inline bool JsonValue::IsString() const noexcept
{
    return std::holds_alternative<std::string>(storage_);
}

inline bool JsonValue::IsArray() const noexcept
{
    return std::holds_alternative<Array>(storage_);
}

inline bool JsonValue::IsObject() const noexcept
{
    return std::holds_alternative<Object>(storage_);
}

inline bool JsonValue::AsBool() const
{
    return GetAs<bool>("JSON value is not a bool.");
}

inline double JsonValue::AsNumber() const
{
    return GetAs<double>("JSON value is not a number.");
}

inline const std::string& JsonValue::AsString() const
{
    return GetAs<std::string>("JSON value is not a string.");
}

inline const JsonValue::Array& JsonValue::AsArray() const
{
    return GetAs<Array>("JSON value is not an array.");
}

inline const JsonValue::Object& JsonValue::AsObject() const
{
    return GetAs<Object>("JSON value is not an object.");
}

inline const JsonValue* JsonValue::FindMember(const std::string_view key) const
{
    if (!IsObject())
    {
        return nullptr;
    }

    const auto& object = AsObject();
    if (const auto iterator = object.find(std::string(key)); iterator != object.end())
    {
        return &iterator->second;
    }

    return nullptr;
}
} // namespace ugc_renderer
