#include "Dialogs.hpp"

#include "Utf.hpp"

#include <algorithm>
#include <commctrl.h>
#include <string>

namespace
{
constexpr UINT EM_SETCUEBANNER_LOCAL = 0x1501;

enum ControlId
{
    IdProvider = 100,
    IdBaseUrl = 101,
    IdModel = 102,
    IdApiKey = 103,
    IdMaxTokens = 104,
    IdTemperature = 105,
    IdTimeout = 106,
    IdSystemPrompt = 107,
    IdClearKey = 108,
    IdSecretTrigger = 109,
    IdHistoryMessages = 110,
    IdOk = 1,
    IdCancel = 2,
    IdPromptEdit = 200
};

int Scale(HWND hwnd, int value)
{
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));

    UINT dpi = 96;
    if (getDpiForWindow != nullptr)
    {
        dpi = getDpiForWindow(hwnd);
    }
    else
    {
        HDC dc = GetDC(hwnd);
        if (dc != nullptr)
        {
            dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
            ReleaseDC(hwnd, dc);
        }
    }
    return MulDiv(value, static_cast<int>(dpi == 0 ? 96 : dpi), 96);
}

void SetFont(HWND control)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h)
{
    HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, Scale(parent, x), Scale(parent, y), Scale(parent, w), Scale(parent, h), parent, nullptr, nullptr, nullptr);
    SetFont(control);
    return control;
}

HWND CreateEdit(HWND parent, int id, int x, int y, int w, int h, DWORD style = ES_AUTOHSCROLL)
{
    HWND control = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
        Scale(parent, x),
        Scale(parent, y),
        Scale(parent, w),
        Scale(parent, h),
        parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        nullptr,
        nullptr);
    SetFont(control);
    return control;
}

HWND CreateButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h)
{
    HWND control = CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        Scale(parent, x),
        Scale(parent, y),
        Scale(parent, w),
        Scale(parent, h),
        parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        nullptr,
        nullptr);
    SetFont(control);
    return control;
}

std::wstring GetWindowString(HWND hwnd)
{
    const int len = GetWindowTextLengthW(hwnd);
    std::wstring value(static_cast<size_t>(len) + 1, L'\0');
    if (len > 0)
    {
        GetWindowTextW(hwnd, value.data(), len + 1);
    }
    value.resize(static_cast<size_t>(len));
    return value;
}

void SetWindowString(HWND hwnd, const std::string& value)
{
    SetWindowTextW(hwnd, Utf8ToWide(value).c_str());
}

struct SettingsDialogState
{
    Settings working;
    bool done = false;
    bool accepted = false;
    Provider visibleProvider = Provider::OpenAIResponses;

    HWND provider = nullptr;
    HWND baseUrl = nullptr;
    HWND model = nullptr;
    HWND apiKey = nullptr;
    HWND maxTokens = nullptr;
    HWND temperature = nullptr;
    HWND timeout = nullptr;
    HWND historyMessages = nullptr;
    HWND secretTrigger = nullptr;
    HWND systemPrompt = nullptr;
};

void SaveVisibleProfile(SettingsDialogState* state)
{
    if (state == nullptr)
    {
        return;
    }

    auto& profile = state->working.Profile(state->visibleProvider);
    profile.baseUrl = WideToUtf8(GetWindowString(state->baseUrl));
    profile.model = WideToUtf8(GetWindowString(state->model));

    std::string key = WideToUtf8(GetWindowString(state->apiKey));
    if (!key.empty())
    {
        state->working.SetApiKey(state->visibleProvider, key);
        SetWindowTextW(state->apiKey, L"");
    }
}

void LoadVisibleProfile(SettingsDialogState* state, Provider provider)
{
    if (state == nullptr)
    {
        return;
    }

    state->visibleProvider = provider;
    const auto& profile = state->working.Profile(provider);
    SetWindowString(state->baseUrl, profile.baseUrl);
    SetWindowString(state->model, profile.model);
    SetWindowTextW(state->apiKey, L"");
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<SettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<SettingsDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        CreateLabel(hwnd, L"Provider", 14, 18, 120, 22);
        state->provider = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, Scale(hwnd, 150), Scale(hwnd, 14), Scale(hwnd, 360), Scale(hwnd, 140), hwnd, reinterpret_cast<HMENU>(IdProvider), nullptr, nullptr);
        SetFont(state->provider);
        for (Provider provider : { Provider::OpenAIResponses, Provider::ClaudeMessages, Provider::DeepSeek, Provider::OpenAICompatible })
        {
            SendMessageW(state->provider, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ProviderDisplayName(provider).c_str()));
        }
        SendMessageW(state->provider, CB_SETCURSEL, ProviderIndex(state->working.activeProvider), 0);

        CreateLabel(hwnd, L"Base URL", 14, 54, 120, 22);
        state->baseUrl = CreateEdit(hwnd, IdBaseUrl, 150, 50, 360, 24);

        CreateLabel(hwnd, L"Model", 14, 88, 120, 22);
        state->model = CreateEdit(hwnd, IdModel, 150, 84, 360, 24);

        CreateLabel(hwnd, L"API key", 14, 122, 120, 22);
        state->apiKey = CreateEdit(hwnd, IdApiKey, 150, 118, 260, 24, ES_AUTOHSCROLL | ES_PASSWORD);
        SendMessageW(state->apiKey, EM_SETCUEBANNER_LOCAL, TRUE, reinterpret_cast<LPARAM>(L"Leave blank to keep saved key"));
        CreateButton(hwnd, IdClearKey, L"Clear key", 420, 117, 90, 26);

        CreateLabel(hwnd, L"Max output tokens", 14, 156, 120, 22);
        state->maxTokens = CreateEdit(hwnd, IdMaxTokens, 150, 152, 100, 24, ES_AUTOHSCROLL | ES_NUMBER);
        SetWindowTextW(state->maxTokens, Utf8ToWide(std::to_string(state->working.maxTokens)).c_str());

        CreateLabel(hwnd, L"Temperature", 270, 156, 90, 22);
        state->temperature = CreateEdit(hwnd, IdTemperature, 360, 152, 150, 24);
        SetWindowString(state->temperature, state->working.temperature);

        CreateLabel(hwnd, L"Timeout sec.", 14, 190, 120, 22);
        state->timeout = CreateEdit(hwnd, IdTimeout, 150, 186, 100, 24, ES_AUTOHSCROLL | ES_NUMBER);
        SetWindowTextW(state->timeout, Utf8ToWide(std::to_string(state->working.timeoutSeconds)).c_str());

        CreateLabel(hwnd, L"Background messages", 14, 224, 130, 22);
        state->historyMessages = CreateEdit(hwnd, IdHistoryMessages, 150, 220, 100, 24, ES_AUTOHSCROLL | ES_NUMBER);
        SetWindowTextW(state->historyMessages, Utf8ToWide(std::to_string(state->working.historyMessageLimit)).c_str());

        CreateLabel(hwnd, L"Secret cover char", 270, 224, 110, 22);
        state->secretTrigger = CreateEdit(hwnd, IdSecretTrigger, 390, 220, 36, 24);
        SendMessageW(state->secretTrigger, EM_LIMITTEXT, 1, 0);
        SetWindowString(state->secretTrigger, state->working.secretTrigger);

        CreateLabel(hwnd, L"System prompt", 14, 258, 120, 22);
        state->systemPrompt = CreateEdit(hwnd, IdSystemPrompt, 150, 254, 360, 92, ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
        SetWindowString(state->systemPrompt, state->working.systemPrompt);

        CreateButton(hwnd, IdOk, L"Save", 314, 364, 95, 30);
        CreateButton(hwnd, IdCancel, L"Cancel", 416, 364, 95, 30);

        LoadVisibleProfile(state, state->working.activeProvider);
        return 0;
    }
    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);
        const int notify = HIWORD(wParam);

        if (id == IdProvider && notify == CBN_SELCHANGE)
        {
            SaveVisibleProfile(state);
            const int index = static_cast<int>(SendMessageW(state->provider, CB_GETCURSEL, 0, 0));
            LoadVisibleProfile(state, ProviderFromIndex(index));
            return 0;
        }

        if (id == IdClearKey)
        {
            if (state != nullptr)
            {
                state->working.ClearApiKey(state->visibleProvider);
                SetWindowTextW(state->apiKey, L"");
            }
            return 0;
        }

        if (id == IdOk)
        {
            SaveVisibleProfile(state);
            state->working.activeProvider = ProviderFromIndex(static_cast<int>(SendMessageW(state->provider, CB_GETCURSEL, 0, 0)));
            state->working.maxTokens = std::max(1, _wtoi(GetWindowString(state->maxTokens).c_str()));
            state->working.timeoutSeconds = std::max(5, _wtoi(GetWindowString(state->timeout).c_str()));
            state->working.historyMessageLimit = std::min(100, std::max(0, _wtoi(GetWindowString(state->historyMessages).c_str())));
            state->working.temperature = WideToUtf8(GetWindowString(state->temperature));
            state->working.secretTrigger = WideToUtf8(GetWindowString(state->secretTrigger));
            if (state->working.secretTrigger.empty())
            {
                state->working.secretTrigger = ".";
            }
            state->working.systemPrompt = WideToUtf8(GetWindowString(state->systemPrompt));
            state->accepted = true;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }

        if (id == IdCancel)
        {
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        if (state != nullptr)
        {
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

struct PromptDialogState
{
    bool done = false;
    bool accepted = false;
    size_t contextBytes = 0;
    std::string prompt;
    HWND edit = nullptr;
};

LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<PromptDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<PromptDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        std::wstring label = L"Additional question or instruction";
        if (state->contextBytes > 0)
        {
            label += L" (selected context included)";
        }

        CreateLabel(hwnd, label.c_str(), 14, 16, 500, 22);
        state->edit = CreateEdit(hwnd, IdPromptEdit, 14, 44, 520, 130, ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
        CreateButton(hwnd, IdOk, L"Ask", 330, 190, 95, 30);
        CreateButton(hwnd, IdCancel, L"Cancel", 438, 190, 95, 30);
        SetFocus(state->edit);
        return 0;
    }
    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);
        if (id == IdOk)
        {
            state->prompt = WideToUtf8(GetWindowString(state->edit));
            state->accepted = true;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IdCancel)
        {
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        if (state != nullptr)
        {
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EnsureWindowClass(const wchar_t* name, WNDPROC proc)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = name;
    RegisterClassW(&wc);
}

bool RunModal(HWND owner, HWND hwnd, bool* done)
{
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (!*done && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return true;
}
}

bool ShowSettingsDialog(HWND owner, Settings& settings)
{
    INITCOMMONCONTROLSEX init = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&init);

    EnsureWindowClass(L"AnonAISettingsWindow", SettingsWndProc);

    SettingsDialogState state;
    state.working = settings;
    state.visibleProvider = settings.activeProvider;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"AnonAISettingsWindow",
        L"AnonAI Settings",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        Scale(owner, 550),
        Scale(owner, 450),
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);

    if (hwnd == nullptr)
    {
        return false;
    }

    RunModal(owner, hwnd, &state.done);
    if (state.accepted)
    {
        settings = state.working;
        return true;
    }
    return false;
}

std::optional<std::string> ShowPromptDialog(HWND owner, size_t contextBytes)
{
    INITCOMMONCONTROLSEX init = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&init);

    EnsureWindowClass(L"AnonAIPromptWindow", PromptWndProc);

    PromptDialogState state;
    state.contextBytes = contextBytes;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"AnonAIPromptWindow",
        L"Ask AnonAI",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        Scale(owner, 570),
        Scale(owner, 270),
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);

    if (hwnd == nullptr)
    {
        return std::nullopt;
    }

    RunModal(owner, hwnd, &state.done);
    if (state.accepted)
    {
        return state.prompt;
    }
    return std::nullopt;
}
