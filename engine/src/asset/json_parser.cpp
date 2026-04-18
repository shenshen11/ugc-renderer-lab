#include "ugc_renderer/asset/json_parser.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace ugc_renderer
{
namespace
{
class JsonTextParser
{
public:
    explicit JsonTextParser(const std::string_view text)
        : text_(text)
    {
        if (text_.size() >= 3 &&
            static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB &&
            static_cast<unsigned char>(text_[2]) == 0xBF)
        {
            position_ = 3;
        }
    }

    JsonValue Parse()
    {
        SkipWhitespace();
        JsonValue root = ParseValue();
        SkipWhitespace();
        if (!IsAtEnd())
        {
            throw std::runtime_error("Unexpected trailing characters after JSON value.");
        }

        return root;
    }

private:
    JsonValue ParseValue()
    {
        if (IsAtEnd())
        {
            throw std::runtime_error("Unexpected end of JSON input.");
        }

        const char current = Peek();
        if (current == '{')
        {
            return ParseObject();
        }
        if (current == '[')
        {
            return ParseArray();
        }
        if (current == '"')
        {
            return JsonValue(ParseString());
        }
        if (current == 't')
        {
            ConsumeLiteral("true");
            return JsonValue(true);
        }
        if (current == 'f')
        {
            ConsumeLiteral("false");
            return JsonValue(false);
        }
        if (current == 'n')
        {
            ConsumeLiteral("null");
            return JsonValue(nullptr);
        }
        if (current == '-' || std::isdigit(static_cast<unsigned char>(current)) != 0)
        {
            return JsonValue(ParseNumber());
        }

        throw std::runtime_error("Unexpected token in JSON input.");
    }

    JsonValue ParseObject()
    {
        Consume('{');
        JsonValue::Object object;
        SkipWhitespace();

        if (TryConsume('}'))
        {
            return JsonValue(std::move(object));
        }

        while (true)
        {
            SkipWhitespace();
            if (Peek() != '"')
            {
                throw std::runtime_error("Expected JSON object key string.");
            }

            std::string key = ParseString();
            SkipWhitespace();
            Consume(':');
            SkipWhitespace();
            object.emplace(std::move(key), ParseValue());
            SkipWhitespace();

            if (TryConsume('}'))
            {
                break;
            }

            Consume(',');
            SkipWhitespace();
        }

        return JsonValue(std::move(object));
    }

    JsonValue ParseArray()
    {
        Consume('[');
        JsonValue::Array array;
        SkipWhitespace();

        if (TryConsume(']'))
        {
            return JsonValue(std::move(array));
        }

        while (true)
        {
            SkipWhitespace();
            array.push_back(ParseValue());
            SkipWhitespace();

            if (TryConsume(']'))
            {
                break;
            }

            Consume(',');
            SkipWhitespace();
        }

        return JsonValue(std::move(array));
    }

    std::string ParseString()
    {
        Consume('"');
        std::string result;

        while (!IsAtEnd())
        {
            const char current = Advance();
            if (current == '"')
            {
                return result;
            }

            if (current == '\\')
            {
                if (IsAtEnd())
                {
                    throw std::runtime_error("Unexpected end of JSON escape sequence.");
                }

                const char escapeCharacter = Advance();
                switch (escapeCharacter)
                {
                case '"':
                case '\\':
                case '/':
                    result.push_back(escapeCharacter);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u':
                    AppendUtf8(result, ParseUnicodeEscape());
                    break;
                default:
                    throw std::runtime_error("Unsupported JSON escape sequence.");
                }

                continue;
            }

            result.push_back(current);
        }

        throw std::runtime_error("Unterminated JSON string.");
    }

    double ParseNumber()
    {
        const std::size_t start = position_;

        if (Peek() == '-')
        {
            ++position_;
        }

        ConsumeDigits();
        if (!IsAtEnd() && Peek() == '.')
        {
            ++position_;
            ConsumeDigits();
        }

        if (!IsAtEnd() && (Peek() == 'e' || Peek() == 'E'))
        {
            ++position_;
            if (!IsAtEnd() && (Peek() == '+' || Peek() == '-'))
            {
                ++position_;
            }
            ConsumeDigits();
        }

        const std::string token(text_.substr(start, position_ - start));
        char* parseEnd = nullptr;
        const double value = std::strtod(token.c_str(), &parseEnd);
        if (parseEnd == nullptr || *parseEnd != '\0')
        {
            throw std::runtime_error("Invalid JSON number token.");
        }

        return value;
    }

    void ConsumeDigits()
    {
        if (IsAtEnd() || std::isdigit(static_cast<unsigned char>(Peek())) == 0)
        {
            throw std::runtime_error("Expected digit in JSON number.");
        }

        while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek())) != 0)
        {
            ++position_;
        }
    }

    void ConsumeLiteral(const std::string_view literal)
    {
        for (const char literalCharacter : literal)
        {
            if (IsAtEnd() || Advance() != literalCharacter)
            {
                throw std::runtime_error("Invalid JSON literal.");
            }
        }
    }

    std::uint32_t ParseUnicodeEscape()
    {
        std::uint32_t codePoint = 0;
        for (int digitIndex = 0; digitIndex < 4; ++digitIndex)
        {
            if (IsAtEnd())
            {
                throw std::runtime_error("Unexpected end of JSON unicode escape.");
            }

            codePoint <<= 4;
            codePoint |= ParseHexDigit(Advance());
        }
        return codePoint;
    }

    static std::uint32_t ParseHexDigit(const char value)
    {
        if (value >= '0' && value <= '9')
        {
            return static_cast<std::uint32_t>(value - '0');
        }
        if (value >= 'a' && value <= 'f')
        {
            return static_cast<std::uint32_t>(10 + (value - 'a'));
        }
        if (value >= 'A' && value <= 'F')
        {
            return static_cast<std::uint32_t>(10 + (value - 'A'));
        }

        throw std::runtime_error("Invalid JSON unicode escape digit.");
    }

    static void AppendUtf8(std::string& destination, const std::uint32_t codePoint)
    {
        if (codePoint <= 0x7F)
        {
            destination.push_back(static_cast<char>(codePoint));
            return;
        }
        if (codePoint <= 0x7FF)
        {
            destination.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
            destination.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            return;
        }
        if (codePoint <= 0xFFFF)
        {
            destination.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
            destination.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            destination.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            return;
        }

        destination.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
        destination.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        destination.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        destination.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }

    void SkipWhitespace()
    {
        while (!IsAtEnd() && std::isspace(static_cast<unsigned char>(Peek())) != 0)
        {
            ++position_;
        }
    }

    void Consume(const char expectedCharacter)
    {
        if (IsAtEnd() || Advance() != expectedCharacter)
        {
            throw std::runtime_error("Unexpected JSON token.");
        }
    }

    bool TryConsume(const char expectedCharacter)
    {
        if (!IsAtEnd() && Peek() == expectedCharacter)
        {
            ++position_;
            return true;
        }

        return false;
    }

    char Peek() const
    {
        return text_[position_];
    }

    char Advance()
    {
        return text_[position_++];
    }

    bool IsAtEnd() const noexcept
    {
        return position_ >= text_.size();
    }

    std::string_view text_;
    std::size_t position_ = 0;
};
} // namespace

JsonValue JsonParser::Parse(const std::string_view text)
{
    JsonTextParser parser(text);
    return parser.Parse();
}
} // namespace ugc_renderer
