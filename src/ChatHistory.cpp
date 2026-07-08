#include "ChatHistory.hpp"

#include "JsonUtil.hpp"
#include "Settings.hpp"
#include "Utf.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
std::filesystem::path HistoryPath(HWND nppHandle)
{
    return std::filesystem::path(ConfigDirectoryForPlugin(nppHandle)) / L"AnonAI.history.jsonl";
}

std::string NormalizeRole(const std::string& role)
{
    return role == "assistant" ? "assistant" : "user";
}

void TrimToLimit(std::vector<ChatMessage>& messages, int limit)
{
    if (limit <= 0)
    {
        messages.clear();
        return;
    }

    if (messages.size() > static_cast<size_t>(limit))
    {
        messages.erase(messages.begin(), messages.end() - static_cast<std::ptrdiff_t>(limit));
    }
}

bool SaveHistory(HWND nppHandle, int limit, std::vector<ChatMessage> messages)
{
    const auto path = HistoryPath(nppHandle);
    if (limit <= 0)
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return true;
    }

    TrimToLimit(messages, limit);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }

    for (const auto& message : messages)
    {
        if (message.content.empty())
        {
            continue;
        }
        out << "{\"role\":\"" << JsonUtil::Escape(NormalizeRole(message.role))
            << "\",\"content\":\"" << JsonUtil::Escape(message.content) << "\"}\n";
    }

    return true;
}
}

std::vector<ChatMessage> LoadChatHistory(HWND nppHandle, int limit)
{
    std::vector<ChatMessage> messages;
    if (limit <= 0)
    {
        return messages;
    }

    std::ifstream in(HistoryPath(nppHandle), std::ios::binary);
    if (!in)
    {
        return messages;
    }

    std::string line;
    while (std::getline(in, line))
    {
        auto role = JsonUtil::FindStringField(line, "role");
        auto content = JsonUtil::FindStringField(line, "content");
        if (!content.has_value() || content->second.empty())
        {
            continue;
        }

        messages.push_back({ role.has_value() ? NormalizeRole(role->second) : "user", content->second });
    }

    TrimToLimit(messages, limit);
    return messages;
}

bool AppendChatExchange(HWND nppHandle, int limit, const std::string& userPrompt, const std::string& assistantText)
{
    if (limit <= 0)
    {
        return SaveHistory(nppHandle, limit, {});
    }

    auto messages = LoadChatHistory(nppHandle, limit);
    messages.push_back({ "user", userPrompt });
    messages.push_back({ "assistant", assistantText });
    return SaveHistory(nppHandle, limit, std::move(messages));
}

bool ClearChatHistory(HWND nppHandle)
{
    std::error_code ignored;
    std::filesystem::remove(HistoryPath(nppHandle), ignored);
    return true;
}

std::wstring ChatHistoryPathForDisplay(HWND nppHandle)
{
    return HistoryPath(nppHandle).wstring();
}
