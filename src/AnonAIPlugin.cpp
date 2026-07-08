#include "AiClient.hpp"
#include "ChatHistory.hpp"
#include "Dialogs.hpp"
#include "PluginInterface.hpp"
#include "ScintillaMini.hpp"
#include "Utf.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace
{
constexpr int kCommandCount = 7;
constexpr UINT WM_ANONAI_RESULT = WM_APP + 391;

enum CommandIndex
{
    CmdSettings = 0,
    CmdInstant = 1,
    CmdPrompt = 2,
    CmdSecret = 3,
    CmdStop = 4,
    CmdClearHistory = 5,
    CmdAbout = 6
};

enum class RequestKind
{
    Insert,
    Secret
};

struct AsyncResult
{
    int generation = 0;
    RequestKind kind = RequestKind::Insert;
    HWND scintilla = nullptr;
    Settings settings;
    std::string prompt;
    CompletionResult completion;
};

NppData g_npp = {};
FuncItem g_funcItems[kCommandCount] = {};
ShortcutKey g_shortcutSettings { true, true, true, 'K' };
ShortcutKey g_shortcutInstant { true, true, true, 'A' };
ShortcutKey g_shortcutPrompt { true, true, true, 'P' };
ShortcutKey g_shortcutSecret { true, true, true, 'H' };
ShortcutKey g_shortcutStop { true, true, true, 'X' };

Settings g_settings = Settings::WithDefaults();
HWND g_messageWindow = nullptr;
std::atomic_bool g_busy = false;
std::atomic_int g_generation = 0;

std::mutex g_secretMutex;
std::string g_secretText;
size_t g_secretIndex = 0;
bool g_secretActive = false;
bool g_secretPending = false;
bool g_internalEdit = false;

void CommandSettings();
void CommandInstant();
void CommandPrompt();
void CommandSecret();
void CommandStop();
void CommandClearHistory();
void CommandAbout();

void SetCommand(int index, const wchar_t* name, PFUNCPLUGINCMD function, ShortcutKey* shortcut)
{
    wcsncpy_s(g_funcItems[index]._itemName, name, _TRUNCATE);
    g_funcItems[index]._pFunc = function;
    g_funcItems[index]._pShKey = shortcut;
}

void EnsureCommands()
{
    static bool initialized = false;
    if (initialized)
    {
        return;
    }

    SetCommand(CmdSettings, L"Settings...", CommandSettings, &g_shortcutSettings);
    SetCommand(CmdInstant, L"Instant answer from selection", CommandInstant, &g_shortcutInstant);
    SetCommand(CmdPrompt, L"Ask with extra prompt...", CommandPrompt, &g_shortcutPrompt);
    SetCommand(CmdSecret, L"Secret typeout from selection", CommandSecret, &g_shortcutSecret);
    SetCommand(CmdStop, L"Stop request/typeout", CommandStop, &g_shortcutStop);
    SetCommand(CmdClearHistory, L"Clear background chat history", CommandClearHistory, nullptr);
    SetCommand(CmdAbout, L"About AnonAI", CommandAbout, nullptr);
    initialized = true;
}

void ShowError(const std::string& message)
{
    MessageBoxW(g_npp._nppHandle, Utf8ToWide(message).c_str(), L"AnonAI", MB_ICONERROR | MB_OK);
}

HWND CurrentScintilla()
{
    int current = 0;
    SendMessageW(g_npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&current));
    return current == 0 ? g_npp._scintillaMainHandle : g_npp._scintillaSecondHandle;
}

std::string GetSelectedText(HWND scintilla)
{
    const LRESULT length = SendMessageW(scintilla, SCI_GETSELTEXT, 0, 0);
    if (length <= 1)
    {
        return {};
    }

    std::string text(static_cast<size_t>(length), '\0');
    SendMessageW(scintilla, SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(text.data()));
    return TrimTrailingNull(text);
}

std::string GetDocumentText(HWND scintilla)
{
    const LRESULT length = SendMessageW(scintilla, SCI_GETLENGTH, 0, 0);
    if (length <= 0)
    {
        return {};
    }

    std::string text(static_cast<size_t>(length) + 1, '\0');
    SendMessageW(scintilla, SCI_GETTEXT, static_cast<WPARAM>(text.size()), reinterpret_cast<LPARAM>(text.data()));
    return TrimTrailingNull(text);
}

void MoveCaretToSelectionEnd(HWND scintilla)
{
    const LRESULT end = SendMessageW(scintilla, SCI_GETSELECTIONEND, 0, 0);
    SendMessageW(scintilla, SCI_SETSEL, static_cast<WPARAM>(end), static_cast<LPARAM>(end));
}

void InsertAnswer(HWND scintilla, const std::string& text)
{
    if (scintilla == nullptr)
    {
        scintilla = CurrentScintilla();
    }

    const LRESULT end = SendMessageW(scintilla, SCI_GETSELECTIONEND, 0, 0);
    SendMessageW(scintilla, SCI_SETSEL, static_cast<WPARAM>(end), static_cast<LPARAM>(end));

    std::string insertion = "\r\n\r\n" + text;
    SendMessageW(scintilla, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(insertion.c_str()));
}

void SetSecretChecked(bool checked)
{
    if (g_funcItems[CmdSecret]._cmdID != 0)
    {
        SendMessageW(g_npp._nppHandle, NPPM_SETMENUITEMCHECK, static_cast<WPARAM>(g_funcItems[CmdSecret]._cmdID), static_cast<LPARAM>(checked ? TRUE : FALSE));
    }
}

void ClearSecret()
{
    std::lock_guard<std::mutex> lock(g_secretMutex);
    g_secretText.clear();
    g_secretIndex = 0;
    g_secretActive = false;
    g_secretPending = false;
    SetSecretChecked(false);
}

std::string BuildPromptWithContext(const std::string& context, const std::string& instruction)
{
    if (context.empty())
    {
        return instruction;
    }
    if (instruction.empty())
    {
        return context;
    }

    std::ostringstream prompt;
    prompt << "Context:\n" << context << "\n\nInstruction:\n" << instruction;
    return prompt.str();
}

LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_ANONAI_RESULT)
    {
        auto* result = reinterpret_cast<AsyncResult*>(lParam);
        if (result == nullptr)
        {
            return 0;
        }

        const bool current = result->generation == g_generation.load();
        if (current)
        {
            g_busy = false;
            if (!result->completion.ok)
            {
                if (result->kind == RequestKind::Secret)
                {
                    ClearSecret();
                }
                ShowError(result->completion.error);
            }
            else if (result->kind == RequestKind::Insert)
            {
                AppendChatExchange(g_npp._nppHandle, result->settings.historyMessageLimit, result->prompt, result->completion.text);
                InsertAnswer(result->scintilla, result->completion.text);
            }
            else
            {
                AppendChatExchange(g_npp._nppHandle, result->settings.historyMessageLimit, result->prompt, result->completion.text);
                {
                    std::lock_guard<std::mutex> lock(g_secretMutex);
                    g_secretText = result->completion.text;
                    g_secretIndex = 0;
                    g_secretActive = true;
                    g_secretPending = false;
                }
                SetSecretChecked(true);
            }
        }

        delete result;
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EnsureMessageWindow()
{
    if (g_messageWindow != nullptr)
    {
        return;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MessageWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AnonAIMessageWindow";
    RegisterClassW(&wc);

    g_messageWindow = CreateWindowExW(
        0,
        L"AnonAIMessageWindow",
        L"AnonAIMessageWindow",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
}

bool ValidateSettings(const Settings& settings, const ProviderProfile& profile, const std::string& apiKey)
{
    if (profile.baseUrl.empty())
    {
        ShowError("Base URL is missing. Open AnonAI settings first.");
        return false;
    }
    if (profile.model.empty())
    {
        ShowError("Model is missing. Open AnonAI settings first.");
        return false;
    }
    if (apiKey.empty())
    {
        MessageBoxW(
            g_npp._nppHandle,
            (L"No API key is saved for " + ProviderDisplayName(settings.activeProvider) + L". Open settings with the AnonAI Settings command.").c_str(),
            L"AnonAI",
            MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

std::string ApplySecretAutoWrap(HWND scintilla, LRESULT insertPos, const std::string& replacement);

bool ReplaceLastTypedChar(SCNotification* notification, const std::string& replacement)
{
    if (replacement.empty())
    {
        return false;
    }

    HWND scintilla = notification->nmhdr.hwndFrom != nullptr ? notification->nmhdr.hwndFrom : CurrentScintilla();
    const LRESULT currentPos = SendMessageW(scintilla, SCI_GETCURRENTPOS, 0, 0);
    const LRESULT before = SendMessageW(scintilla, SCI_POSITIONBEFORE, static_cast<WPARAM>(currentPos), 0);
    if (before < 0 || before >= currentPos)
    {
        return false;
    }

    const std::string finalReplacement = ApplySecretAutoWrap(scintilla, before, replacement);

    g_internalEdit = true;
    SendMessageW(scintilla, SCI_SETSEL, static_cast<WPARAM>(before), static_cast<LPARAM>(currentPos));
    SendMessageW(scintilla, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(finalReplacement.c_str()));
    g_internalEdit = false;
    return true;
}

bool StartsWithLineBreak(const std::string& value)
{
    return !value.empty() && (value[0] == '\r' || value[0] == '\n');
}

std::string ApplySecretAutoWrap(HWND scintilla, LRESULT insertPos, const std::string& replacement)
{
    if (replacement.empty() || StartsWithLineBreak(replacement))
    {
        return replacement;
    }

    const LRESULT line = SendMessageW(scintilla, SCI_LINEFROMPOSITION, static_cast<WPARAM>(insertPos), 0);
    LRESULT column = SendMessageW(scintilla, SCI_GETCOLUMN, static_cast<WPARAM>(insertPos), 0);
    if (line < 0)
    {
        return replacement;
    }

    if (column <= 0)
    {
        const LRESULT lineStart = SendMessageW(scintilla, SCI_POSITIONFROMLINE, static_cast<WPARAM>(line), 0);
        if (lineStart >= 0 && insertPos >= lineStart)
        {
            column = insertPos - lineStart;
        }
    }
    if (column <= 0)
    {
        return replacement;
    }

    RECT rect = {};
    if (!GetClientRect(scintilla, &rect))
    {
        return replacement;
    }

    const int visibleWidth = rect.right - rect.left;
    constexpr int rightPadding = 18;
    if (visibleWidth <= rightPadding + 80)
    {
        return replacement;
    }

    const LRESULT x = SendMessageW(scintilla, SCI_POINTXFROMPOSITION, 0, static_cast<LPARAM>(insertPos));
    const LRESULT replacementWidth = SendMessageW(scintilla, SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>(replacement.c_str()));
    if (x > 0 && replacementWidth > 0 && x + replacementWidth >= visibleWidth - rightPadding)
    {
        return "\r\n" + replacement;
    }

    const LRESULT spaceWidth = SendMessageW(scintilla, SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>(" "));
    const int roughColumns = std::max(20, (visibleWidth - rightPadding) / 8);
    int wrapColumn = roughColumns;
    if (spaceWidth > 0)
    {
        const int measuredColumns = std::max(20, (visibleWidth - rightPadding) / static_cast<int>(spaceWidth));
        wrapColumn = std::min(wrapColumn, measuredColumns);
    }
    if (column >= wrapColumn)
    {
        return "\r\n" + replacement;
    }

    return replacement;
}

void StartRequest(RequestKind kind, HWND scintilla, const std::string& prompt)
{
    if (prompt.empty())
    {
        ShowError("There is no prompt text. Select text, write a prompt, or use the prompt window.");
        return;
    }

    if (g_busy.exchange(true))
    {
        ShowError("AnonAI is already waiting for a provider response. Use the AnonAI Stop command to ignore it.");
        return;
    }

    g_settings = Settings::Load(g_npp._nppHandle);
    ProviderProfile profile = g_settings.ActiveProfile();
    const std::string apiKey = g_settings.GetApiKey(g_settings.activeProvider);
    if (!ValidateSettings(g_settings, profile, apiKey))
    {
        g_busy = false;
        return;
    }

    EnsureMessageWindow();

    const int generation = ++g_generation;
    if (kind == RequestKind::Secret)
    {
        std::lock_guard<std::mutex> lock(g_secretMutex);
        g_secretText.clear();
        g_secretIndex = 0;
        g_secretActive = false;
        g_secretPending = true;
        SetSecretChecked(true);
    }

    CompletionRequest request;
    request.settings = g_settings;
    request.profile = profile;
    request.apiKey = apiKey;
    request.history = LoadChatHistory(g_npp._nppHandle, g_settings.historyMessageLimit);
    request.prompt = prompt;

    std::thread([generation, kind, scintilla, request]() {
        auto* async = new AsyncResult;
        async->generation = generation;
        async->kind = kind;
        async->scintilla = scintilla;
        async->settings = request.settings;
        async->prompt = request.prompt;
        async->completion = CompleteText(request);

        HWND target = g_messageWindow;
        if (target != nullptr)
        {
            PostMessageW(target, WM_ANONAI_RESULT, 0, reinterpret_cast<LPARAM>(async));
        }
        else
        {
            delete async;
        }
    }).detach();
}

void CommandSettings()
{
    g_settings = Settings::Load(g_npp._nppHandle);
    if (ShowSettingsDialog(g_npp._nppHandle, g_settings))
    {
        if (!g_settings.Save(g_npp._nppHandle))
        {
            ShowError("Could not save settings to " + WideToUtf8(ConfigPathForDisplay(g_npp._nppHandle)));
        }
        else if (g_settings.historyMessageLimit <= 0)
        {
            ClearChatHistory(g_npp._nppHandle);
        }
    }
}

void CommandInstant()
{
    HWND scintilla = CurrentScintilla();
    std::string context = GetSelectedText(scintilla);
    if (context.empty())
    {
        context = GetDocumentText(scintilla);
    }
    StartRequest(RequestKind::Insert, scintilla, context);
}

void CommandPrompt()
{
    HWND scintilla = CurrentScintilla();
    const std::string context = GetSelectedText(scintilla);
    auto instruction = ShowPromptDialog(g_npp._nppHandle, context.size());
    if (!instruction.has_value())
    {
        return;
    }

    StartRequest(RequestKind::Insert, scintilla, BuildPromptWithContext(context, *instruction));
}

void CommandSecret()
{
    HWND scintilla = CurrentScintilla();

    bool shouldClear = false;
    {
        std::lock_guard<std::mutex> lock(g_secretMutex);
        shouldClear = g_secretActive || g_secretPending;
    }
    if (shouldClear)
    {
        ClearSecret();
        return;
    }

    std::string context = GetSelectedText(scintilla);
    if (context.empty())
    {
        context = GetDocumentText(scintilla);
    }

    MoveCaretToSelectionEnd(scintilla);
    StartRequest(RequestKind::Secret, scintilla, context);
}

void CommandStop()
{
    ++g_generation;
    g_busy = false;
    ClearSecret();
}

void CommandClearHistory()
{
    ClearChatHistory(g_npp._nppHandle);
    MessageBoxW(
        g_npp._nppHandle,
        L"Background chat history was cleared.",
        L"AnonAI",
        MB_OK | MB_ICONINFORMATION);
}

void CommandAbout()
{
    MessageBoxW(
        g_npp._nppHandle,
        L"AnonAI for Notepad++\n\nCreated by texploder / asashin.com\n\nYou may modify and redistribute this program, but keep the original credits and license notices.",
        L"About AnonAI",
        MB_OK | MB_ICONINFORMATION);
}

void OnCharAdded(SCNotification* notification)
{
    if (notification == nullptr || g_internalEdit)
    {
        return;
    }

    if (notification->ch < 32)
    {
        return;
    }

    std::string replacement;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(g_secretMutex);
        if (g_secretPending && !g_secretActive)
        {
            replacement = g_settings.secretTrigger.empty() ? "." : g_settings.secretTrigger;
        }
        else if (g_secretActive && g_secretIndex < g_secretText.size())
        {
            replacement = NextUtf8Codepoint(g_secretText, g_secretIndex);
            if (g_secretIndex >= g_secretText.size())
            {
                g_secretActive = false;
                finished = true;
            }
        }
        else
        {
            return;
        }
    }

    if (!ReplaceLastTypedChar(notification, replacement))
    {
        ClearSecret();
        return;
    }

    if (finished)
    {
        SetSecretChecked(false);
    }
}
}

extern "C" __declspec(dllexport) void setInfo(NppData notepadPlusData)
{
    g_npp = notepadPlusData;
    EnsureCommands();
    EnsureMessageWindow();
    g_settings = Settings::Load(g_npp._nppHandle);
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
    return L"AnonAI";
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count)
{
    EnsureCommands();
    *count = kCommandCount;
    return g_funcItems;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notification)
{
    if (notification == nullptr)
    {
        return;
    }

    if (notification->nmhdr.code == SCN_CHARADDED)
    {
        OnCharAdded(notification);
        return;
    }

    if (notification->nmhdr.code == NPPN_SHUTDOWN)
    {
        ClearSecret();
        if (g_messageWindow != nullptr)
        {
            DestroyWindow(g_messageWindow);
            g_messageWindow = nullptr;
        }
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{
    return TRUE;
}
