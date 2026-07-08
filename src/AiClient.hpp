#pragma once

#include "Settings.hpp"

#include <string>
#include <vector>

struct ChatMessage
{
    std::string role;
    std::string content;
};

struct CompletionRequest
{
    Settings settings;
    ProviderProfile profile;
    std::string apiKey;
    std::vector<ChatMessage> history;
    std::string prompt;
};

struct CompletionResult
{
    bool ok = false;
    std::string text;
    std::string error;
};

CompletionResult CompleteText(const CompletionRequest& request);
