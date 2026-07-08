#include "JsonUtil.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace
{
size_t SkipSpaces(const std::string& text, size_t index)
{
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0)
    {
        ++index;
    }
    return index;
}

void AppendUtf8(std::string& out, unsigned int codepoint)
{
    if (codepoint <= 0x7F)
    {
        out.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

bool IsHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

unsigned int HexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return static_cast<unsigned int>(c - '0');
    }
    if (c >= 'a' && c <= 'f')
    {
        return static_cast<unsigned int>(10 + c - 'a');
    }
    return static_cast<unsigned int>(10 + c - 'A');
}

bool ParseJsonStringAt(const std::string& json, size_t quoteIndex, std::string& out, size_t& next)
{
    if (quoteIndex >= json.size() || json[quoteIndex] != '"')
    {
        return false;
    }

    out.clear();
    size_t i = quoteIndex + 1;
    while (i < json.size())
    {
        const char c = json[i++];
        if (c == '"')
        {
            next = i;
            return true;
        }

        if (c != '\\')
        {
            out.push_back(c);
            continue;
        }

        if (i >= json.size())
        {
            return false;
        }

        const char escaped = json[i++];
        switch (escaped)
        {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u':
        {
            if (i + 4 > json.size())
            {
                return false;
            }
            unsigned int codepoint = 0;
            for (int n = 0; n < 4; ++n)
            {
                if (!IsHex(json[i + static_cast<size_t>(n)]))
                {
                    return false;
                }
                codepoint = (codepoint << 4) | HexValue(json[i + static_cast<size_t>(n)]);
            }
            i += 4;
            AppendUtf8(out, codepoint);
            break;
        }
        default:
            out.push_back(escaped);
            break;
        }
    }

    return false;
}
}

namespace JsonUtil
{
std::string Escape(const std::string& value)
{
    std::ostringstream out;
    for (const unsigned char c : value)
    {
        switch (c)
        {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20)
            {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
                    << std::dec << std::setw(0);
            }
            else
            {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

std::optional<std::pair<size_t, std::string>> FindStringField(const std::string& json, const std::string& key, size_t start)
{
    size_t i = start;
    while (i < json.size())
    {
        if (json[i] != '"')
        {
            ++i;
            continue;
        }

        std::string parsedKey;
        size_t afterKey = 0;
        if (!ParseJsonStringAt(json, i, parsedKey, afterKey))
        {
            ++i;
            continue;
        }

        size_t colon = SkipSpaces(json, afterKey);
        if (colon < json.size() && json[colon] == ':' && parsedKey == key)
        {
            size_t valueStart = SkipSpaces(json, colon + 1);
            if (valueStart < json.size() && json[valueStart] == '"')
            {
                std::string value;
                size_t afterValue = 0;
                if (ParseJsonStringAt(json, valueStart, value, afterValue))
                {
                    return std::make_pair(afterValue, value);
                }
            }
        }

        i = afterKey;
    }

    return std::nullopt;
}

std::optional<size_t> FindStringFieldWithValue(const std::string& json, const std::string& key, const std::string& value, size_t start)
{
    size_t i = start;
    while (true)
    {
        auto field = FindStringField(json, key, i);
        if (!field.has_value())
        {
            return std::nullopt;
        }

        if (field->second == value)
        {
            return field->first;
        }

        i = field->first;
    }
}

std::vector<std::pair<size_t, std::string>> FindAllStringFields(const std::string& json, const std::string& key)
{
    std::vector<std::pair<size_t, std::string>> result;
    size_t i = 0;
    while (true)
    {
        auto field = FindStringField(json, key, i);
        if (!field.has_value())
        {
            break;
        }
        result.push_back(*field);
        i = field->first;
    }
    return result;
}

std::string ExtractErrorMessage(const std::string& json)
{
    if (auto message = FindStringField(json, "message"))
    {
        return message->second;
    }
    if (auto error = FindStringField(json, "error"))
    {
        return error->second;
    }
    if (auto type = FindStringField(json, "type"))
    {
        return type->second;
    }
    return {};
}
}
