#pragma once

#include <array>
#include <string>
#include <windows.h>

enum class Provider
{
    OpenAIResponses = 0,
    ClaudeMessages = 1,
    DeepSeek = 2,
    OpenAICompatible = 3
};

struct ProviderProfile
{
    Provider provider = Provider::OpenAIResponses;
    std::string baseUrl;
    std::string model;
    std::string apiKeyProtected;
};

struct Settings
{
    Provider activeProvider = Provider::OpenAIResponses;
    std::array<ProviderProfile, 4> profiles = {};
    int maxTokens = 2048;
    int timeoutSeconds = 120;
    int historyMessageLimit = 8;
    std::string temperature;
    std::string secretTrigger;
    std::string systemPrompt;

    static Settings WithDefaults();
    static Settings Load(HWND nppHandle);
    bool Save(HWND nppHandle) const;

    ProviderProfile& Profile(Provider provider);
    const ProviderProfile& Profile(Provider provider) const;
    ProviderProfile& ActiveProfile();
    const ProviderProfile& ActiveProfile() const;

    std::string GetApiKey(Provider provider) const;
    void SetApiKey(Provider provider, const std::string& apiKey);
    void ClearApiKey(Provider provider);
};

std::string ProviderId(Provider provider);
std::wstring ProviderDisplayName(Provider provider);
Provider ProviderFromIndex(int index);
int ProviderIndex(Provider provider);
std::wstring ConfigDirectoryForPlugin(HWND nppHandle);
std::wstring ConfigPathForDisplay(HWND nppHandle);
