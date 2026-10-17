#include "framework.h"
#include "MouseTrigger.h"
#include "ShortcutConfig.h"
#include "ProcessExit.h"

static ShortcutItem g_mouseTrigger = {};
static bool g_mouseTriggerRegistered = false;
static bool g_mouseTriggerPending = false;
static POINT g_mouseTriggerPoint = {};
static ULONGLONG g_mouseTriggerDeadline = 0;
static HWND g_statusText = nullptr;

bool SendRightClick(const RECT& bounds)
{
    int x = bounds.left + (bounds.right - bounds.left) / 2;
    int y = bounds.top + (bounds.bottom - bounds.top) / 2;
    int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (virtualWidth <= 1 || virtualHeight <= 1)
        return false;
    if (x < virtualLeft || x >= virtualLeft + virtualWidth ||
        y < virtualTop || y >= virtualTop + virtualHeight)
        return false;

    INPUT move = {};
    move.type = INPUT_MOUSE;
    move.mi.dx = MulDiv(x - virtualLeft, 65535, virtualWidth - 1);
    move.mi.dy = MulDiv(y - virtualTop, 65535, virtualHeight - 1);
    move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    if (SendInput(1, &move, sizeof(INPUT)) != 1)
        return false;

    INPUT buttonDown = {};
    buttonDown.type = INPUT_MOUSE;
    buttonDown.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    if (SendInput(1, &buttonDown, sizeof(INPUT)) != 1)
        return false;

    INPUT buttonUp = {};
    buttonUp.type = INPUT_MOUSE;
    buttonUp.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (SendInput(1, &buttonUp, sizeof(INPUT)) == 1)
            return true;
    }
    return false;
}

static bool LoadMouseTrigger()
{
    std::wstring configPath = GetConfigurationPath(L"settings.ini");
    if (configPath.empty())
        return false;

    WCHAR shortcut[128] = {};
    GetPrivateProfileStringW(L"MouseTrigger", L"Shortcut", L"Ctrl+Shift+1", shortcut,
        static_cast<DWORD>(_countof(shortcut)), configPath.c_str());

    ShortcutItem item = {};
    item.name = L"Mouse Trigger";
    item.shortcut = shortcut;
    if (!ParseShortcut(item.shortcut, item))
    {
        item = {};
        item.name = L"Mouse Trigger";
        item.shortcut = L"Ctrl+Shift+1";
        if (!ParseShortcut(item.shortcut, item))
            return false;
        AppendDiagnosticLog(L"Invalid mouse trigger setting; using the default shortcut");
    }

    g_mouseTrigger = item;
    return true;
}

static UINT GetHotKeyModifiers(const ShortcutItem& item)
{
    UINT modifiers = MOD_NOREPEAT;
    if (item.control) modifiers |= MOD_CONTROL;
    if (item.alt) modifiers |= MOD_ALT;
    if (item.shift) modifiers |= MOD_SHIFT;
    if (item.windows) modifiers |= MOD_WIN;
    return modifiers;
}

bool UpdateMouseTrigger(HWND hWnd, const std::wstring& shortcut, std::wstring& errorMessage)
{
    ShortcutItem candidate = {};
    candidate.name = L"Mouse Trigger";
    candidate.shortcut = shortcut;
    if (!ParseShortcut(candidate.shortcut, candidate))
    {
        errorMessage = L"Enter a valid shortcut, for example Ctrl+Shift+1.";
        return false;
    }
    if (!candidate.control && !candidate.alt && !candidate.shift && !candidate.windows)
    {
        errorMessage = L"Use at least one modifier, for example Ctrl+Shift+1.";
        return false;
    }

    ShortcutItem previous = g_mouseTrigger;
    bool previousRegistered = g_mouseTriggerRegistered;
    if (g_mouseTriggerPending)
    {
        KillTimer(hWnd, ID_MOUSE_TRIGGER_TIMER);
        g_mouseTriggerPending = false;
    }
    if (previousRegistered)
    {
        UnregisterHotKey(hWnd, ID_MOUSE_TRIGGER_HOTKEY);
        g_mouseTriggerRegistered = false;
    }

    if (RegisterHotKey(hWnd, ID_MOUSE_TRIGGER_HOTKEY,
        GetHotKeyModifiers(candidate), candidate.virtualKey) == FALSE)
    {
        DWORD registrationError = GetLastError();
        if (previousRegistered)
            g_mouseTriggerRegistered = RegisterHotKey(hWnd, ID_MOUSE_TRIGGER_HOTKEY,
                GetHotKeyModifiers(previous), previous.virtualKey) != FALSE;
        errorMessage = L"The shortcut could not be registered. It may already be used by another application.";
        AppendDiagnosticLog(L"RegisterHotKey update failed, error=" + std::to_wstring(registrationError));
        return false;
    }

    std::wstring configPath = GetConfigurationPath(L"settings.ini");
    if (configPath.empty() || WritePrivateProfileStringW(L"MouseTrigger", L"Shortcut",
        candidate.shortcut.c_str(), configPath.c_str()) == FALSE)
    {
        DWORD writeError = GetLastError();
        UnregisterHotKey(hWnd, ID_MOUSE_TRIGGER_HOTKEY);
        if (previousRegistered)
            g_mouseTriggerRegistered = RegisterHotKey(hWnd, ID_MOUSE_TRIGGER_HOTKEY,
                GetHotKeyModifiers(previous), previous.virtualKey) != FALSE;
        errorMessage = L"The setting could not be saved. Check write access to settings.ini.";
        AppendDiagnosticLog(L"Unable to save settings.ini, error=" + std::to_wstring(writeError));
        return false;
    }

    g_mouseTrigger = candidate;
    g_mouseTriggerRegistered = true;
    AppendDiagnosticLog(L"Mouse trigger hotkey updated: " + candidate.shortcut);
    return true;
}

static bool IsMouseTriggerReleased()
{
    const WORD keys[] = { g_mouseTrigger.virtualKey, VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN };
    const bool enabled[] = { true, g_mouseTrigger.control, g_mouseTrigger.alt, g_mouseTrigger.shift,
        g_mouseTrigger.windows, g_mouseTrigger.windows };
    for (int index = 0; index < static_cast<int>(_countof(keys)); ++index)
    {
        if (enabled[index] && (GetAsyncKeyState(keys[index]) & 0x8000) != 0)
            return false;
    }
    return true;
}

void HandleMouseTriggerHotkey(HWND hWnd)
{
    if (!g_mouseTriggerRegistered) return;
    if (!GetCursorPos(&g_mouseTriggerPoint))
    {
        SetWindowTextW(g_statusText, L"Unable to read the current mouse position.");
        AppendDiagnosticLog(L"GetCursorPos failed, error=" + std::to_wstring(GetLastError()));
        return;
    }

    g_mouseTriggerPending = true;
    g_mouseTriggerDeadline = GetTickCount64() + 5000;
    if (SetTimer(hWnd, ID_MOUSE_TRIGGER_TIMER, 10, nullptr) == 0)
    {
        g_mouseTriggerPending = false;
        SetWindowTextW(g_statusText, L"Unable to schedule the mouse trigger.");
        AppendDiagnosticLog(L"SetTimer failed, error=" + std::to_wstring(GetLastError()));
    }
}

void HandleMouseTriggerTimer(HWND hWnd)
{
    if (!g_mouseTriggerPending)
        return;
    if (!IsMouseTriggerReleased())
    {
        if (GetTickCount64() < g_mouseTriggerDeadline)
            return;
        KillTimer(hWnd, ID_MOUSE_TRIGGER_TIMER);
        g_mouseTriggerPending = false;
        SetWindowTextW(g_statusText, L"Mouse trigger canceled because the hotkey was not released.");
        AppendDiagnosticLog(L"Mouse trigger canceled after waiting for hotkey release");
        return;
    }

    KillTimer(hWnd, ID_MOUSE_TRIGGER_TIMER);
    g_mouseTriggerPending = false;
    RECT target = { g_mouseTriggerPoint.x, g_mouseTriggerPoint.y,
        g_mouseTriggerPoint.x + 1, g_mouseTriggerPoint.y + 1 };
    if (SendRightClick(target))
    {
        SetWindowTextW(g_statusText, L"Right-click sent at the captured mouse position.");
        AppendDiagnosticLog(L"Mouse trigger right-click sent at x=" + std::to_wstring(g_mouseTriggerPoint.x) +
            L", y=" + std::to_wstring(g_mouseTriggerPoint.y));
    }
    else
    {
        SetWindowTextW(g_statusText, L"Unable to send the right-click.");
        AppendDiagnosticLog(L"Mouse trigger right-click failed");
    }
}

void InitializeMouseTrigger(HWND owner, HWND statusWindow)
{
    g_statusText = statusWindow;
    if (LoadMouseTrigger())
    {
        g_mouseTriggerRegistered = RegisterHotKey(owner, ID_MOUSE_TRIGGER_HOTKEY,
            GetHotKeyModifiers(g_mouseTrigger), g_mouseTrigger.virtualKey) != FALSE;
        if (g_mouseTriggerRegistered)
            AppendDiagnosticLog(L"Mouse trigger hotkey registered: " + g_mouseTrigger.shortcut);
        else
            AppendDiagnosticLog(L"RegisterHotKey failed, error=" + std::to_wstring(GetLastError()));
    }
}

void ShutdownMouseTrigger(HWND owner)
{
    if (g_mouseTriggerPending)
    {
        KillTimer(owner, ID_MOUSE_TRIGGER_TIMER);
        g_mouseTriggerPending = false;
    }
    if (g_mouseTriggerRegistered)
    {
        UnregisterHotKey(owner, ID_MOUSE_TRIGGER_HOTKEY);
        g_mouseTriggerRegistered = false;
    }
    g_statusText = nullptr;
}

const std::wstring& GetMouseTriggerShortcut()
{
    return g_mouseTrigger.shortcut;
}
