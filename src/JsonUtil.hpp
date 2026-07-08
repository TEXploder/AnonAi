#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace JsonUtil
{
std::string Escape(const std::string& value);
std::optional<std::pair<size_t, std::string>> FindStringField(const std::string& json, const std::string& key, size_t start = 0);
std::optional<size_t> FindStringFieldWithValue(const std::string& json, const std::string& key, const std::string& value, size_t start = 0);
std::vector<std::pair<size_t, std::string>> FindAllStringFields(const std::string& json, const std::string& key);
std::string ExtractErrorMessage(const std::string& json);
}
