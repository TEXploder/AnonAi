#pragma once

#include "Settings.hpp"

#include <optional>
#include <string>
#include <windows.h>

bool ShowSettingsDialog(HWND owner, Settings& settings);
std::optional<std::string> ShowPromptDialog(HWND owner, size_t contextBytes);
