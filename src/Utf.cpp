#include "Utf.hpp"

#include <windows.h>

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
    {
        return {};
    }

    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
    {
        const int fallbackRequired = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (fallbackRequired <= 0)
        {
            return {};
        }

        std::wstring fallback(static_cast<size_t>(fallbackRequired), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), fallback.data(), fallbackRequired);
        return fallback;
    }

    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

std::string TrimTrailingNull(std::string value)
{
    if (!value.empty() && value.back() == '\0')
    {
        value.pop_back();
    }
    return value;
}

std::string NextUtf8Codepoint(const std::string& value, size_t& index)
{
    if (index >= value.size())
    {
        return {};
    }

    const unsigned char lead = static_cast<unsigned char>(value[index]);
    size_t length = 1;

    if ((lead & 0x80) == 0)
    {
        length = 1;
    }
    else if ((lead & 0xE0) == 0xC0)
    {
        length = 2;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        length = 3;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        length = 4;
    }

    if (index + length > value.size())
    {
        length = 1;
    }

    std::string result = value.substr(index, length);
    index += length;
    return result;
}
