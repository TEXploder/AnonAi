#pragma once

#include "AiClient.hpp"

#include <string>
#include <vector>
#include <windows.h>

std::vector<ChatMessage> LoadChatHistory(HWND nppHandle, int limit);
bool AppendChatExchange(HWND nppHandle, int limit, const std::string& userPrompt, const std::string& assistantText);
bool ClearChatHistory(HWND nppHandle);
std::wstring ChatHistoryPathForDisplay(HWND nppHandle);
