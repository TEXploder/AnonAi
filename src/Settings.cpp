#include "Settings.hpp"

#include "JsonUtil.hpp"
#include "PluginInterface.hpp"
#include "Utf.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <wincrypt.h>

namespace
{
constexpr const char* kSystemPrompt =
    "You are AnonAI inside Notepad++. Answer directly, keep formatting useful, "
    "and do not mention that you are an editor plugin unless asked.";

Provider ProviderFromId(const std::string& id)
{
    if (id == "claude_messages")
    {
        return Provider::ClaudeMessages;
    }
    if (id == "deepseek")
    {
        return Provider::DeepSeek;
    }
    if (id == "openai_compatible")
    {
        return Provider::OpenAICompatible;
    }
    return Provider::OpenAIResponses;
}

std::string ProfilePrefix(Provider provider)
{
    switch (provider)
    {
    case Provider::OpenAIResponses: return "openai";
    case Provider::ClaudeMessages: return "claude";
    case Provider::DeepSeek: return "deepseek";
    case Provider::OpenAICompatible: return "custom";
    }
    return "openai";
}

std::string FieldOrDefault(const std::string& json, const std::string& key, const std::string& fallback)
{
    if (auto value = JsonUtil::FindStringField(json, key))
    {
        return value->second;
    }
    return fallback;
}

std::string NormalizeSecretTrigger(std::string value)
{
    if (value.empty())
    {
        return ".";
    }

    size_t index = 0;
    std::string first = NextUtf8Codepoint(value, index);
    return first.empty() ? "." : first;
}

int IntOrDefault(const std::string& value, int fallback, int minValue, int maxValue)
{
    try
    {
        const int parsed = std::stoi(value);
        if (parsed < minValue || parsed > maxValue)
        {
            return fallback;
        }
        return parsed;
    }
    catch (...)
    {
        return fallback;
    }
}

std::string Base64Encode(const std::vector<BYTE>& data)
{
    if (data.empty())
    {
        return {};
    }

    DWORD chars = 0;
    if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars))
    {
        return {};
    }

    std::string encoded(chars, '\0');
    if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &chars))
    {
        return {};
    }
    if (!encoded.empty() && encoded.back() == '\0')
    {
        encoded.pop_back();
    }
    return encoded;
}

std::vector<BYTE> Base64Decode(const std::string& encoded)
{
    if (encoded.empty())
    {
        return {};
    }

    DWORD bytes = 0;
    if (!CryptStringToBinaryA(encoded.c_str(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64, nullptr, &bytes, nullptr, nullptr))
    {
        return {};
    }

    std::vector<BYTE> decoded(bytes);
    if (!CryptStringToBinaryA(encoded.c_str(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64, decoded.data(), &bytes, nullptr, nullptr))
    {
        return {};
    }
    decoded.resize(bytes);
    return decoded;
}

std::string ProtectString(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    DATA_BLOB input = {};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(value.data()));
    input.cbData = static_cast<DWORD>(value.size());

    DATA_BLOB output = {};
    if (!CryptProtectData(&input, L"AnonAI API key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        return {};
    }

    std::vector<BYTE> bytes(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return Base64Encode(bytes);
}

std::string UnprotectString(const std::string& protectedValue)
{
    auto bytes = Base64Decode(protectedValue);
    if (bytes.empty())
    {
        return {};
    }

    DATA_BLOB input = {};
    input.pbData = bytes.data();
    input.cbData = static_cast<DWORD>(bytes.size());

    DATA_BLOB output = {};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        return {};
    }

    std::string value(reinterpret_cast<char*>(output.pbData), reinterpret_cast<char*>(output.pbData) + output.cbData);
    LocalFree(output.pbData);
    return value;
}

std::wstring ConfigDirectory(HWND nppHandle)
{
    wchar_t buffer[MAX_PATH * 2] = {};
    if (nppHandle != nullptr)
    {
        LRESULT ok = SendMessageW(nppHandle, NPPM_GETPLUGINSCONFIGDIR, static_cast<WPARAM>(std::size(buffer)), reinterpret_cast<LPARAM>(buffer));
        if (ok != 0 && buffer[0] != L'\0')
        {
            std::filesystem::create_directories(buffer);
            return buffer;
        }
    }

    wchar_t appData[MAX_PATH * 2] = {};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, static_cast<DWORD>(std::size(appData)));
    std::wstring dir = (len > 0 && len < std::size(appData))
        ? std::wstring(appData) + L"\\Notepad++\\plugins\\Config"
        : L".";
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path ConfigPath(HWND nppHandle)
{
    return std::filesystem::path(ConfigDirectory(nppHandle)) / L"AnonAI.json";
}

void WriteJsonField(std::ostream& out, const std::string& key, const std::string& value, bool comma = true)
{
    out << "  \"" << key << "\": \"" << JsonUtil::Escape(value) << "\"";
    if (comma)
    {
        out << ",";
    }
    out << "\n";
}
}

std::string ProviderId(Provider provider)
{
    switch (provider)
    {
    case Provider::OpenAIResponses: return "openai_responses";
    case Provider::ClaudeMessages: return "claude_messages";
    case Provider::DeepSeek: return "deepseek";
    case Provider::OpenAICompatible: return "openai_compatible";
    }
    return "openai_responses";
}

std::wstring ProviderDisplayName(Provider provider)
{
    switch (provider)
    {
    case Provider::OpenAIResponses: return L"OpenAI (Responses API)";
    case Provider::ClaudeMessages: return L"Claude (Anthropic Messages)";
    case Provider::DeepSeek: return L"DeepSeek (OpenAI-compatible)";
    case Provider::OpenAICompatible: return L"Custom OpenAI-compatible";
    }
    return L"OpenAI (Responses API)";
}

Provider ProviderFromIndex(int index)
{
    switch (index)
    {
    case 1: return Provider::ClaudeMessages;
    case 2: return Provider::DeepSeek;
    case 3: return Provider::OpenAICompatible;
    default: return Provider::OpenAIResponses;
    }
}

int ProviderIndex(Provider provider)
{
    return static_cast<int>(provider);
}

Settings Settings::WithDefaults()
{
    Settings settings;
    settings.activeProvider = Provider::OpenAIResponses;
    settings.maxTokens = 2048;
    settings.timeoutSeconds = 120;
    settings.historyMessageLimit = 8;
    settings.temperature.clear();
    settings.secretTrigger = ".";
    settings.systemPrompt = kSystemPrompt;

    settings.profiles[0] = { Provider::OpenAIResponses, "https://api.openai.com/v1", "gpt-5", "" };
    settings.profiles[1] = { Provider::ClaudeMessages, "https://api.anthropic.com", "claude-sonnet-4-6", "" };
    settings.profiles[2] = { Provider::DeepSeek, "https://api.deepseek.com", "deepseek-v4-flash", "" };
    settings.profiles[3] = { Provider::OpenAICompatible, "https://api.example.com/v1", "model-name", "" };

    return settings;
}

Settings Settings::Load(HWND nppHandle)
{
    Settings settings = WithDefaults();
    const auto path = ConfigPath(nppHandle);

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return settings;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();

    settings.activeProvider = ProviderFromId(FieldOrDefault(json, "activeProvider", ProviderId(settings.activeProvider)));
    settings.maxTokens = IntOrDefault(FieldOrDefault(json, "maxTokens", std::to_string(settings.maxTokens)), settings.maxTokens, 1, 2000000);
    settings.timeoutSeconds = IntOrDefault(FieldOrDefault(json, "timeoutSeconds", std::to_string(settings.timeoutSeconds)), settings.timeoutSeconds, 5, 600);
    settings.historyMessageLimit = IntOrDefault(FieldOrDefault(json, "historyMessageLimit", std::to_string(settings.historyMessageLimit)), settings.historyMessageLimit, 0, 100);
    settings.temperature = FieldOrDefault(json, "temperature", settings.temperature);
    settings.secretTrigger = NormalizeSecretTrigger(FieldOrDefault(json, "secretTrigger", settings.secretTrigger));
    settings.systemPrompt = FieldOrDefault(json, "systemPrompt", settings.systemPrompt);

    for (Provider provider : { Provider::OpenAIResponses, Provider::ClaudeMessages, Provider::DeepSeek, Provider::OpenAICompatible })
    {
        const std::string prefix = ProfilePrefix(provider);
        auto& profile = settings.Profile(provider);
        profile.baseUrl = FieldOrDefault(json, prefix + "BaseUrl", profile.baseUrl);
        profile.model = FieldOrDefault(json, prefix + "Model", profile.model);
        profile.apiKeyProtected = FieldOrDefault(json, prefix + "ApiKeyProtected", profile.apiKeyProtected);
    }

    return settings;
}

bool Settings::Save(HWND nppHandle) const
{
    const auto path = ConfigPath(nppHandle);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }

    out << "{\n";
    WriteJsonField(out, "activeProvider", ProviderId(activeProvider));
    WriteJsonField(out, "maxTokens", std::to_string(maxTokens));
    WriteJsonField(out, "timeoutSeconds", std::to_string(timeoutSeconds));
    WriteJsonField(out, "historyMessageLimit", std::to_string(historyMessageLimit));
    WriteJsonField(out, "temperature", temperature);
    WriteJsonField(out, "secretTrigger", NormalizeSecretTrigger(secretTrigger));
    WriteJsonField(out, "systemPrompt", systemPrompt);

    for (Provider provider : { Provider::OpenAIResponses, Provider::ClaudeMessages, Provider::DeepSeek, Provider::OpenAICompatible })
    {
        const std::string prefix = ProfilePrefix(provider);
        const auto& profile = Profile(provider);
        WriteJsonField(out, prefix + "BaseUrl", profile.baseUrl);
        WriteJsonField(out, prefix + "Model", profile.model);
        WriteJsonField(out, prefix + "ApiKeyProtected", profile.apiKeyProtected, provider != Provider::OpenAICompatible);
    }
    out << "}\n";
    return true;
}

ProviderProfile& Settings::Profile(Provider provider)
{
    return profiles[static_cast<size_t>(provider)];
}

const ProviderProfile& Settings::Profile(Provider provider) const
{
    return profiles[static_cast<size_t>(provider)];
}

ProviderProfile& Settings::ActiveProfile()
{
    return Profile(activeProvider);
}

const ProviderProfile& Settings::ActiveProfile() const
{
    return Profile(activeProvider);
}

std::string Settings::GetApiKey(Provider provider) const
{
    return UnprotectString(Profile(provider).apiKeyProtected);
}

void Settings::SetApiKey(Provider provider, const std::string& apiKey)
{
    Profile(provider).apiKeyProtected = ProtectString(apiKey);
}

void Settings::ClearApiKey(Provider provider)
{
    Profile(provider).apiKeyProtected.clear();
}

std::wstring ConfigPathForDisplay(HWND nppHandle)
{
    return ConfigPath(nppHandle).wstring();
}

std::wstring ConfigDirectoryForPlugin(HWND nppHandle)
{
    return ConfigDirectory(nppHandle);
}
