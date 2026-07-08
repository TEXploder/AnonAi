#pragma once

#include <windows.h>
#include <cstdint>

struct NppData
{
    HWND _nppHandle = nullptr;
    HWND _scintillaMainHandle = nullptr;
    HWND _scintillaSecondHandle = nullptr;
};

using PFUNCPLUGINCMD = void(__cdecl*)();

struct ShortcutKey
{
    bool _isCtrl = false;
    bool _isAlt = false;
    bool _isShift = false;
    UCHAR _key = 0;
};

constexpr int menuItemSize = 64;

struct FuncItem
{
    wchar_t _itemName[menuItemSize] = {};
    PFUNCPLUGINCMD _pFunc = nullptr;
    int _cmdID = 0;
    bool _init2Check = false;
    ShortcutKey* _pShKey = nullptr;
};

struct SCNotification
{
    NMHDR nmhdr = {};
    uintptr_t position = 0;
    int ch = 0;
    int modifiers = 0;
    int modificationType = 0;
    const char* text = nullptr;
    uintptr_t length = 0;
    uintptr_t linesAdded = 0;
    int message = 0;
    uintptr_t wParam = 0;
    intptr_t lParam = 0;
    uintptr_t line = 0;
    int foldLevelNow = 0;
    int foldLevelPrev = 0;
    int margin = 0;
    int listType = 0;
    int x = 0;
    int y = 0;
    int token = 0;
    uintptr_t annotationLinesAdded = 0;
    int updated = 0;
    int listCompletionMethod = 0;
    uintptr_t characterSource = 0;
};

#ifndef NPPMSG
#define NPPMSG (WM_USER + 1000)
#endif

#define NPPM_GETCURRENTSCINTILLA (NPPMSG + 4)
#define NPPM_SETMENUITEMCHECK (NPPMSG + 40)
#define NPPM_GETPLUGINSCONFIGDIR (NPPMSG + 46)

#define NPPN_FIRST 1000
#define NPPN_READY (NPPN_FIRST + 1)
#define NPPN_SHUTDOWN (NPPN_FIRST + 9)

#define SCN_CHARADDED 2001
