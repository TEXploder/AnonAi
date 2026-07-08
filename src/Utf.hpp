#pragma once

#include <string>

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::string TrimTrailingNull(std::string value);
std::string NextUtf8Codepoint(const std::string& value, size_t& index);
