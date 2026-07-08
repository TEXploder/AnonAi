#include "AiClient.hpp"

#include "JsonUtil.hpp"
#include "Utf.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <winhttp.h>

namespace
{
std::string TrimRightSlashes(std::string value)
{
    while (!value.empty() && value.back() == '/')
    {
        value.pop_back();
    }
    return value;
}

bool EndsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string BuildUrl(const std::string& baseUrl, const std::string& endpoint)
{
    std::string base = TrimRightSlashes(baseUrl);
    if (EndsWith(base, endpoint))
    {
        return base;
    }
    return base + endpoint;
}

void AppendTemperature(std::ostringstream& body, const std::string& temperature)
{
    if (!temperature.empty())
    {
        body << ",\"temperature\":" << temperature;
    }
}

std::string ApiRole(const std::string& role)
{
    if (role == "system")
    {
        return "system";
    }
    return role == "assistant" ? "assistant" : "user";
}

void AppendMessageObject(std::ostringstream& body, const std::string& role, const std::string& content, bool& needsComma)
{
    if (content.empty())
    {
        return;
    }

    if (needsComma)
    {
        body << ",";
    }
    body << "{\"role\":\"" << ApiRole(role) << "\",\"content\":\"" << JsonUtil::Escape(content) << "\"}";
    needsComma = true;
}

std::string BuildOpenAIResponsesBody(const CompletionRequest& request)
{
    std::ostringstream body;
    body << "{\"model\":\"" << JsonUtil::Escape(request.profile.model) << "\""
         << ",\"input\":[";

    bool needsComma = false;
    AppendMessageObject(body, "system", request.settings.systemPrompt, needsComma);
    for (const auto& message : request.history)
    {
        AppendMessageObject(body, message.role, message.content, needsComma);
    }
    AppendMessageObject(body, "user", request.prompt, needsComma);

    body << "]"
         << ",\"max_output_tokens\":" << request.settings.maxTokens;
    AppendTemperature(body, request.settings.temperature);
    body << "}";
    return body.str();
}

std::string BuildChatCompletionsBody(const CompletionRequest& request)
{
    std::ostringstream body;
    body << "{\"model\":\"" << JsonUtil::Escape(request.profile.model) << "\""
         << ",\"messages\":[";

    bool needsComma = false;
    AppendMessageObject(body, "system", request.settings.systemPrompt, needsComma);
    for (const auto& message : request.history)
    {
        AppendMessageObject(body, message.role, message.content, needsComma);
    }
    AppendMessageObject(body, "user", request.prompt, needsComma);

    body << "]"
         << ",\"max_tokens\":" << request.settings.maxTokens;
    AppendTemperature(body, request.settings.temperature);
    body << "}";
    return body.str();
}

std::string BuildClaudeMessagesBody(const CompletionRequest& request)
{
    std::ostringstream body;
    body << "{\"model\":\"" << JsonUtil::Escape(request.profile.model) << "\""
         << ",\"max_tokens\":" << request.settings.maxTokens
         << ",\"system\":\"" << JsonUtil::Escape(request.settings.systemPrompt) << "\""
         << ",\"messages\":[";

    bool needsComma = false;
    bool sawUser = false;
    for (const auto& message : request.history)
    {
        const std::string role = ApiRole(message.role);
        if (!sawUser && role == "assistant")
        {
            continue;
        }
        sawUser = sawUser || role == "user";
        AppendMessageObject(body, role, message.content, needsComma);
    }
    AppendMessageObject(body, "user", request.prompt, needsComma);

    body << "]";
    AppendTemperature(body, request.settings.temperature);
    body << "}";
    return body.str();
}

std::string ExtractOpenAIResponsesText(const std::string& json)
{
    if (auto marker = JsonUtil::FindStringFieldWithValue(json, "type", "output_text"))
    {
        if (auto text = JsonUtil::FindStringField(json, "text", *marker))
        {
            return text->second;
        }
    }

    if (auto text = JsonUtil::FindStringField(json, "output_text"))
    {
        return text->second;
    }

    if (auto text = JsonUtil::FindStringField(json, "text"))
    {
        return text->second;
    }

    return {};
}

std::string ExtractChatCompletionsText(const std::string& json)
{
    if (auto content = JsonUtil::FindStringField(json, "content"))
    {
        return content->second;
    }
    return {};
}

std::string ExtractClaudeMessagesText(const std::string& json)
{
    if (auto marker = JsonUtil::FindStringFieldWithValue(json, "type", "text"))
    {
        if (auto text = JsonUtil::FindStringField(json, "text", *marker))
        {
            return text->second;
        }
    }

    if (auto text = JsonUtil::FindStringField(json, "text"))
    {
        return text->second;
    }

    return {};
}

struct HttpResponse
{
    DWORD statusCode = 0;
    std::string body;
};

class WinHttpHandle
{
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~WinHttpHandle()
    {
        if (handle_ != nullptr)
        {
            WinHttpCloseHandle(handle_);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    operator HINTERNET() const { return handle_; }
    bool valid() const { return handle_ != nullptr; }

private:
    HINTERNET handle_ = nullptr;
};

std::wstring LastWindowsError()
{
    const DWORD error = GetLastError();
    wchar_t* message = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&message),
        0,
        nullptr);

    std::wstring result = message != nullptr ? message : L"Unknown Windows error";
    if (message != nullptr)
    {
        LocalFree(message);
    }
    return result;
}

HttpResponse PostJson(const std::string& url, const std::wstring& headers, const std::string& body, int timeoutSeconds)
{
    std::wstring wideUrl = Utf8ToWide(url);

    URL_COMPONENTSW components = {};
    components.dwStructSize = sizeof(components);

    wchar_t host[512] = {};
    wchar_t path[4096] = {};
    wchar_t extra[2048] = {};

    components.lpszHostName = host;
    components.dwHostNameLength = static_cast<DWORD>(std::size(host));
    components.lpszUrlPath = path;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    components.lpszExtraInfo = extra;
    components.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));

    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &components))
    {
        throw std::runtime_error("Invalid URL");
    }

    std::wstring objectName(path, components.dwUrlPathLength);
    objectName.append(extra, components.dwExtraInfoLength);
    if (objectName.empty())
    {
        objectName = L"/";
    }

    WinHttpHandle session(WinHttpOpen(
        L"AnonAI Notepad++ Plugin/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session.valid())
    {
        throw std::runtime_error("WinHTTP session failed");
    }

    const int timeoutMs = std::max(5, timeoutSeconds) * 1000;
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    WinHttpHandle connection(WinHttpConnect(session, std::wstring(host, components.dwHostNameLength).c_str(), components.nPort, 0));
    if (!connection.valid())
    {
        throw std::runtime_error("WinHTTP connection failed");
    }

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request(WinHttpOpenRequest(
        connection,
        L"POST",
        objectName.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags));
    if (!request.valid())
    {
        throw std::runtime_error("WinHTTP request failed");
    }

    BOOL sent = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);
    if (!sent)
    {
        throw std::runtime_error("Sending request failed: " + WideToUtf8(LastWindowsError()));
    }

    if (!WinHttpReceiveResponse(request, nullptr))
    {
        throw std::runtime_error("Receiving response failed: " + WideToUtf8(LastWindowsError()));
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    std::string responseBody;
    while (true)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
        {
            break;
        }

        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0)
        {
            break;
        }
        chunk.resize(read);
        responseBody += chunk;
    }

    return { statusCode, responseBody };
}
}

CompletionResult CompleteText(const CompletionRequest& request)
{
    try
    {
        std::string endpoint;
        std::string body;
        std::wstring headers;

        switch (request.profile.provider)
        {
        case Provider::OpenAIResponses:
            endpoint = "/responses";
            body = BuildOpenAIResponsesBody(request);
            headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + Utf8ToWide(request.apiKey) + L"\r\n";
            break;
        case Provider::ClaudeMessages:
            endpoint = "/v1/messages";
            body = BuildClaudeMessagesBody(request);
            headers = L"Content-Type: application/json\r\nx-api-key: " + Utf8ToWide(request.apiKey) + L"\r\nanthropic-version: 2023-06-01\r\n";
            break;
        case Provider::DeepSeek:
        case Provider::OpenAICompatible:
            endpoint = "/chat/completions";
            body = BuildChatCompletionsBody(request);
            headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + Utf8ToWide(request.apiKey) + L"\r\n";
            break;
        }

        const std::string url = BuildUrl(request.profile.baseUrl, endpoint);
        HttpResponse response = PostJson(url, headers, body, request.settings.timeoutSeconds);
        if (response.statusCode < 200 || response.statusCode >= 300)
        {
            std::string message = JsonUtil::ExtractErrorMessage(response.body);
            if (message.empty())
            {
                message = response.body.substr(0, 500);
            }
            return { false, {}, "HTTP " + std::to_string(response.statusCode) + ": " + message };
        }

        std::string text;
        switch (request.profile.provider)
        {
        case Provider::OpenAIResponses:
            text = ExtractOpenAIResponsesText(response.body);
            break;
        case Provider::ClaudeMessages:
            text = ExtractClaudeMessagesText(response.body);
            break;
        case Provider::DeepSeek:
        case Provider::OpenAICompatible:
            text = ExtractChatCompletionsText(response.body);
            break;
        }

        if (text.empty())
        {
            return { false, {}, "The provider response did not contain assistant text." };
        }

        return { true, text, {} };
    }
    catch (const std::exception& ex)
    {
        return { false, {}, ex.what() };
    }
}
