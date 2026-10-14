// ProcQuit.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "ProcQuit.h"
#include "ProcessExit.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <ole2.h>
#include <tlhelp32.h>
#include <UIAutomation.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Uiautomationcore.lib")

#define MAX_LOADSTRING 100

// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    Settings(HWND, UINT, WPARAM, LPARAM);

struct ShortcutItem
{
    std::wstring name;
    std::wstring shortcut;
    WORD virtualKey;
    bool control;
    bool alt;
    bool shift;
    bool windows;
};

static std::vector<ShortcutItem> g_shortcuts;
static std::vector<HWND> g_shortcutButtons;
static HWND g_statusText = nullptr;
static HWND g_processList = nullptr;
static HWND g_refreshButton = nullptr;
static HWND g_exitButton = nullptr;
static HWND g_taskbarMenuButton = nullptr;
static HWND g_copyLogsButton = nullptr;
static std::thread g_exitWorker;
static bool g_exitPending = false;
static constexpr UINT WM_EXIT_FINISHED = WM_APP + 1;
static constexpr int ID_PROCESS_LIST = 1900;
static constexpr int ID_REFRESH = 1901;
static constexpr int ID_REQUEST_EXIT = 1902;
static constexpr int ID_COPY_LOGS = 1903;
static constexpr int ID_TASKBAR_MENU = 1904;
static constexpr int ID_MOUSE_TRIGGER_HOTKEY = 1905;
static constexpr UINT_PTR ID_MOUSE_TRIGGER_TIMER = 1906;
static ShortcutItem g_mouseTrigger = {};
static bool g_mouseTriggerRegistered = false;
static bool g_mouseTriggerPending = false;
static bool g_settingsOpen = false;
static POINT g_mouseTriggerPoint = {};
static ULONGLONG g_mouseTriggerDeadline = 0;

struct ProcessRow
{
    DWORD pid;
    ULONGLONG creationTime;
    std::wstring name;
    std::wstring title;
    HWND window;
};
static std::vector<ProcessRow> g_processes;

static std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

static bool IsTaskbarApplicationWindow(HWND window)
{
    if (!IsWindowVisible(window))
        return false;

    LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((extendedStyle & WS_EX_TOOLWINDOW) != 0 && (extendedStyle & WS_EX_APPWINDOW) == 0)
        return false;
    if (GetWindow(window, GW_OWNER) != nullptr && (extendedStyle & WS_EX_APPWINDOW) == 0)
        return false;
    if ((extendedStyle & WS_EX_NOACTIVATE) != 0 && (extendedStyle & WS_EX_APPWINDOW) == 0)
        return false;

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0)
        return false;

    return GetWindowTextLengthW(window) > 0;
}

static BOOL CALLBACK CollectTaskbarApplications(HWND window, LPARAM parameter)
{
    if (!IsTaskbarApplicationWindow(window))
        return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0 || processId == GetCurrentProcessId())
        return TRUE;

    auto* windows = reinterpret_cast<std::vector<HWND>*>(parameter);
    windows->push_back(window);
    return TRUE;
}

static std::wstring GetWindowTitle(HWND window)
{
    int length = GetWindowTextLengthW(window);
    if (length <= 0)
        return {};

    std::vector<wchar_t> title(static_cast<size_t>(length) + 1);
    int copied = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
    return copied > 0 ? std::wstring(title.data(), copied) : std::wstring();
}

static int ScoreTaskbarButtonName(const std::wstring& accessibleName, const ProcessRow& target)
{
    std::wstring name = ToLower(accessibleName);
    std::wstring title = ToLower(target.title);

    if (!name.empty() && !title.empty() && name == title)
        return 100;
    return 0;
}

static bool IsTaskbarApplicationButton(const std::wstring& automationId)
{
    std::wstring normalizedId = ToLower(automationId);
    return normalizedId.rfind(L"appid:", 0) == 0;
}

struct TaskbarSearchContext
{
    IUIAutomation* automation;
    const ProcessRow* target;
    RECT bestBounds;
    int bestScore;
    int bestMatchCount;
    int taskbarCount;
    bool searchComplete;
};

static BOOL CALLBACK FindTaskbarButton(HWND window, LPARAM parameter)
{
    wchar_t className[64] = {};
    if (GetClassNameW(window, className, static_cast<int>(_countof(className))) == 0 ||
        (wcscmp(className, L"Shell_TrayWnd") != 0 && wcscmp(className, L"Shell_SecondaryTrayWnd") != 0))
        return TRUE;

    auto* context = reinterpret_cast<TaskbarSearchContext*>(parameter);
    ++context->taskbarCount;
    IUIAutomationElement* taskbar = nullptr;
    if (FAILED(context->automation->ElementFromHandle(window, &taskbar)) || taskbar == nullptr)
    {
        context->searchComplete = false;
        return TRUE;
    }

    VARIANT value = {};
    value.vt = VT_I4;
    value.lVal = UIA_ButtonControlTypeId;
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationElementArray* buttons = nullptr;
    IUIAutomationCacheRequest* cacheRequest = nullptr;
    HRESULT conditionResult = context->automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &condition);
    HRESULT cacheResult = context->automation->CreateCacheRequest(&cacheRequest);
    if (SUCCEEDED(conditionResult) && condition != nullptr && SUCCEEDED(cacheResult) && cacheRequest != nullptr)
    {
        HRESULT nameResult = cacheRequest->AddProperty(UIA_NamePropertyId);
        HRESULT idResult = cacheRequest->AddProperty(UIA_AutomationIdPropertyId);
        HRESULT offscreenResult = cacheRequest->AddProperty(UIA_IsOffscreenPropertyId);
        HRESULT boundsResult = cacheRequest->AddProperty(UIA_BoundingRectanglePropertyId);
        if (SUCCEEDED(nameResult) && SUCCEEDED(idResult) && SUCCEEDED(offscreenResult) && SUCCEEDED(boundsResult))
        {
            HRESULT findResult = taskbar->FindAllBuildCache(TreeScope_Descendants, condition, cacheRequest, &buttons);
            if (FAILED(findResult) || buttons == nullptr)
                context->searchComplete = false;
        }
        else
        {
            context->searchComplete = false;
        }
    }
    else
    {
        context->searchComplete = false;
    }

    if (buttons != nullptr)
    {
        int count = 0;
        if (FAILED(buttons->get_Length(&count)))
        {
            context->searchComplete = false;
            count = 0;
        }
        for (int index = 0; index < count; ++index)
        {
            IUIAutomationElement* button = nullptr;
            if (FAILED(buttons->GetElement(index, &button)) || button == nullptr)
            {
                context->searchComplete = false;
                continue;
            }

            BSTR accessibleName = nullptr;
            BSTR automationId = nullptr;
            BOOL offscreen = TRUE;
            RECT bounds = {};
            HRESULT nameResult = button->get_CachedName(&accessibleName);
            HRESULT idResult = button->get_CachedAutomationId(&automationId);
            HRESULT offscreenResult = button->get_CachedIsOffscreen(&offscreen);
            HRESULT boundsResult = button->get_CachedBoundingRectangle(&bounds);
            if (FAILED(nameResult) || FAILED(idResult) || FAILED(offscreenResult) || FAILED(boundsResult))
                context->searchComplete = false;
            int score = accessibleName == nullptr || automationId == nullptr || !IsTaskbarApplicationButton(automationId)
                ? 0 : ScoreTaskbarButtonName(accessibleName, *context->target);
            if (!offscreen && score > 0 && bounds.right > bounds.left && bounds.bottom > bounds.top)
            {
                if (score > context->bestScore)
                {
                    context->bestScore = score;
                    context->bestBounds = bounds;
                    context->bestMatchCount = 1;
                }
                else if (score == context->bestScore)
                {
                    ++context->bestMatchCount;
                }
            }
            if (accessibleName != nullptr)
                SysFreeString(accessibleName);
            if (automationId != nullptr)
                SysFreeString(automationId);
            button->Release();
        }
        buttons->Release();
    }
    if (cacheRequest != nullptr)
        cacheRequest->Release();
    if (condition != nullptr)
        condition->Release();
    taskbar->Release();
    return TRUE;
}

static bool SendRightClick(const RECT& bounds)
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

static bool IsCurrentProcessInstance(const ProcessRow& target)
{
    if (target.creationTime == 0)
        return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
    if (process == nullptr)
        return false;

    FILETIME start = {}, end = {}, kernel = {}, user = {};
    bool matches = false;
    if (GetProcessTimes(process, &start, &end, &kernel, &user))
    {
        ULARGE_INTEGER value = {};
        value.LowPart = start.dwLowDateTime;
        value.HighPart = start.dwHighDateTime;
        matches = value.QuadPart == target.creationTime;
    }
    CloseHandle(process);
    return matches;
}

static bool IsCurrentWindowInstance(const ProcessRow& target)
{
    if (!IsWindow(target.window))
        return false;

    DWORD currentProcessId = 0;
    GetWindowThreadProcessId(target.window, &currentProcessId);
    return currentProcessId == target.pid && GetWindowTitle(target.window) == target.title &&
        IsCurrentProcessInstance(target);
}

static void RequestSelectedTaskbarMenu()
{
    int selected = ListView_GetNextItem(g_processList, -1, LVNI_SELECTED);
    if (selected < 0 || static_cast<size_t>(selected) >= g_processes.size())
        return;

    const ProcessRow& target = g_processes[selected];
    if (!IsCurrentWindowInstance(target))
    {
        SetWindowTextW(g_statusText, L"The selected window is no longer available. Refresh the list.");
        return;
    }

    IUIAutomation* automation = nullptr;
    HRESULT result = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
    if (SUCCEEDED(result) && automation != nullptr)
    {
        TaskbarSearchContext context = { automation, &target, {}, 0, 0, 0, true };
        BOOL enumerationSucceeded = EnumWindows(FindTaskbarButton, reinterpret_cast<LPARAM>(&context));
        automation->Release();
        if (enumerationSucceeded && context.searchComplete && context.taskbarCount > 0 &&
            context.bestScore == 100 && context.bestMatchCount == 1 && IsCurrentWindowInstance(target) &&
            SendRightClick(context.bestBounds))
        {
            AppendDiagnosticLog(L"Taskbar button right-click sent for PID " + std::to_wstring(target.pid));
            SetWindowTextW(g_statusText, L"Taskbar icon right-click sent.");
            return;
        }
    }

    AppendDiagnosticLog(L"Unable to identify a unique taskbar button for PID " + std::to_wstring(target.pid));
    SetWindowTextW(g_statusText, L"Unable to identify a unique taskbar button for the selected window.");
}

static void CopyDiagnosticLogs()
{
    std::wstring logs = GetDiagnosticLog();
    if (logs.empty()) logs = L"No diagnostic log entries.";
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (logs.size() + 1) * sizeof(wchar_t));
    if (memory == nullptr || !OpenClipboard(nullptr)) return;
    wchar_t* target = static_cast<wchar_t*>(GlobalLock(memory));
    memcpy(target, logs.c_str(), (logs.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(memory);
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    SetWindowTextW(g_statusText, L"Diagnostic logs copied to clipboard.");
}

static void UpdateExitButton()
{
    if (g_processList == nullptr)
        return;

    bool hasSelection = ListView_GetNextItem(g_processList, -1, LVNI_SELECTED) >= 0;
    if (g_exitButton != nullptr)
        EnableWindow(g_exitButton, !g_exitPending && hasSelection);
    if (g_taskbarMenuButton != nullptr)
        EnableWindow(g_taskbarMenuButton, !g_exitPending && hasSelection);
}

static void RefreshProcesses()
{
    if (g_processList == nullptr) return;
    ListView_DeleteAllItems(g_processList);
    g_processes.clear();

    std::unordered_map<DWORD, std::wstring> processNames;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                processNames.emplace(entry.th32ProcessID, entry.szExeFile);
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    std::vector<HWND> windows;
    EnumWindows(CollectTaskbarApplications, reinterpret_cast<LPARAM>(&windows));
    for (HWND window : windows)
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);

        ULONGLONG created = 0;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (process != nullptr)
        {
            FILETIME start = {}, end = {}, kernel = {}, user = {};
            if (GetProcessTimes(process, &start, &end, &kernel, &user))
            {
                ULARGE_INTEGER value = {};
                value.LowPart = start.dwLowDateTime;
                value.HighPart = start.dwHighDateTime;
                created = value.QuadPart;
            }
            CloseHandle(process);
        }

        auto processName = processNames.find(processId);
        std::wstring name = processName == processNames.end() ? L"Unknown" : processName->second;
        std::wstring title = GetWindowTitle(window);
        g_processes.push_back({ processId, created, name, title, window });

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(g_processes.size() - 1);
        item.pszText = const_cast<LPWSTR>(g_processes.back().name.c_str());
        int row = ListView_InsertItem(g_processList, &item);
        std::wstring pid = std::to_wstring(processId);
        ListView_SetItemText(g_processList, row, 1, const_cast<LPWSTR>(pid.c_str()));
        ListView_SetItemText(g_processList, row, 2, const_cast<LPWSTR>(g_processes.back().title.c_str()));
        LPWSTR availability = created ? const_cast<LPWSTR>(L"Ready") : const_cast<LPWSTR>(L"Unavailable");
        ListView_SetItemText(g_processList, row, 3, availability);
    }

    UpdateExitButton();
    SetWindowTextW(g_statusText, (L"Showing " + std::to_wstring(g_processes.size()) + L" taskbar window candidates.").c_str());
    AppendDiagnosticLog(L"Taskbar window candidates enumerated: " + std::to_wstring(g_processes.size()));
}

static void RequestSelectedExit(HWND hWnd)
{
    int selected = ListView_GetNextItem(g_processList, -1, LVNI_SELECTED);
    if (g_exitPending || selected < 0 || static_cast<size_t>(selected) >= g_processes.size()) return;
    ProcessRow target = g_processes[selected];
    if (!target.creationTime || target.pid == GetCurrentProcessId())
    {
        SetWindowTextW(g_statusText, L"This process cannot be requested to exit.");
        return;
    }
    if (g_exitWorker.joinable()) g_exitWorker.join();
    g_exitPending = true;
    UpdateExitButton();
    SetWindowTextW(g_statusText, L"Requesting graceful exit...");
    g_exitWorker = std::thread([hWnd, target]() {
        GracefulExitResult result = RequestGracefulExit(target.pid, target.creationTime, 5000);
        PostMessageW(hWnd, WM_EXIT_FINISHED, static_cast<WPARAM>(result.status), result.errorCode);
    });
}

static bool ParseShortcut(const std::wstring& text, ShortcutItem& item)
{
    WORD key = 0;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(L'+', start);
        std::wstring token = text.substr(start, end == std::wstring::npos ? end : end - start);
        if (token == L"Ctrl") item.control = true;
        else if (token == L"Alt") item.alt = true;
        else if (token == L"Shift") item.shift = true;
        else if (token == L"Win") item.windows = true;
        else if (token.size() == 1 && ((token[0] >= L'A' && token[0] <= L'Z') || (token[0] >= L'0' && token[0] <= L'9')))
            key = static_cast<WORD>(VkKeyScanW(token[0]) & 0xff);
        else if (token.size() >= 2 && token[0] == L'F' && token[1] >= L'1' && token[1] <= L'9')
        {
            int functionNumber = _wtoi(token.c_str() + 1);
            if (functionNumber >= 1 && functionNumber <= 24) key = static_cast<WORD>(VK_F1 + functionNumber - 1);
        }
        else if (token == L"Space") key = VK_SPACE;
        else if (token == L"Enter") key = VK_RETURN;
        else if (token == L"Tab") key = VK_TAB;
        else if (token == L"Esc") key = VK_ESCAPE;
        else if (token == L"Delete") key = VK_DELETE;
        else if (token == L"Backspace") key = VK_BACK;
        else return false;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    if (key == 0) return false;
    item.virtualKey = key;
    return true;
}

static std::wstring GetConfigurationPath(const wchar_t* fileName)
{
    WCHAR modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
        return {};

    std::wstring configPath(modulePath);
    size_t slash = configPath.find_last_of(L"\\/");
    configPath.resize(slash == std::wstring::npos ? 0 : slash + 1);
    configPath += fileName;
    return configPath;
}

static void LoadShortcuts()
{
    WCHAR modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) return;
    std::wstring configPath(modulePath);
    size_t slash = configPath.find_last_of(L"\\/");
    configPath.resize(slash == std::wstring::npos ? 0 : slash + 1);
    configPath += L"shortcuts.ini";
    WCHAR entries[32768] = {};
    DWORD length = GetPrivateProfileSectionW(L"Shortcuts", entries, 32768, configPath.c_str());
    for (DWORD offset = 0; offset < length;)
    {
        std::wstring entry(entries + offset);
        offset += static_cast<DWORD>(entry.size()) + 1;
        size_t separator = entry.find(L'=');
        if (separator == std::wstring::npos) continue;
        ShortcutItem item = {};
        item.name = entry.substr(0, separator);
        item.shortcut = entry.substr(separator + 1);
        if (ParseShortcut(item.shortcut, item)) g_shortcuts.push_back(item);
    }
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

static UINT GetHotKeyModifiers(const ShortcutItem& item);

static bool UpdateMouseTrigger(HWND hWnd, const std::wstring& shortcut, std::wstring& errorMessage)
{
    ShortcutItem candidate = {};
    candidate.name = L"Mouse Trigger";
    candidate.shortcut = shortcut;
    if (!ParseShortcut(candidate.shortcut, candidate))
    {
        errorMessage = L"Enter a valid shortcut, for example Ctrl+Shift+1.";
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

static UINT GetHotKeyModifiers(const ShortcutItem& item)
{
    UINT modifiers = MOD_NOREPEAT;
    if (item.control) modifiers |= MOD_CONTROL;
    if (item.alt) modifiers |= MOD_ALT;
    if (item.shift) modifiers |= MOD_SHIFT;
    if (item.windows) modifiers |= MOD_WIN;
    return modifiers;
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

static void RequestMouseTrigger(HWND hWnd)
{
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

static void CompleteMouseTrigger(HWND hWnd)
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

static void LayoutShortcuts(HWND hWnd)
{
    RECT client = {};
    GetClientRect(hWnd, &client);
    const int margin = 12;
    const int gap = 8;
    const int buttonHeight = 36;
    int buttonWidth = (client.right - margin * 2 - gap * 3) / 4;
    if (buttonWidth < 1) buttonWidth = 1;
    for (size_t i = 0; i < g_shortcutButtons.size(); ++i)
    {
        int column = static_cast<int>(i % 4);
        int row = static_cast<int>(i / 4);
        MoveWindow(g_shortcutButtons[i], margin + column * (buttonWidth + gap), margin + row * (buttonHeight + gap), buttonWidth, buttonHeight, TRUE);
    }
    int top = margin + static_cast<int>((g_shortcutButtons.size() + 3) / 4) * (buttonHeight + gap);
    if (g_processList != nullptr)
        MoveWindow(g_processList, margin, top + buttonHeight, client.right - margin * 2, (client.bottom - top - buttonHeight - 82 > 40 ? client.bottom - top - buttonHeight - 82 : 40), TRUE);
    if (g_refreshButton != nullptr) MoveWindow(g_refreshButton, margin, top, 90, 28, TRUE);
    if (g_exitButton != nullptr) MoveWindow(g_exitButton, margin + 100, top, 130, 28, TRUE);
    if (g_taskbarMenuButton != nullptr) MoveWindow(g_taskbarMenuButton, margin + 240, top, 160, 28, TRUE);
    if (g_copyLogsButton != nullptr) MoveWindow(g_copyLogsButton, margin + 410, top, 120, 28, TRUE);
    if (g_statusText != nullptr)
        MoveWindow(g_statusText, margin, client.bottom - 30, client.right - margin * 2, 20, TRUE);
}

static void SendShortcut(const ShortcutItem& item)
{
    INPUT inputs[10] = {};
    UINT count = 0;
    const WORD modifiers[] = { VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN };
    const bool enabled[] = { item.control, item.alt, item.shift, item.windows };
    for (int i = 0; i < 4; ++i) if (enabled[i]) { inputs[count].type = INPUT_KEYBOARD; inputs[count++].ki.wVk = modifiers[i]; }
    inputs[count].type = INPUT_KEYBOARD; inputs[count++].ki.wVk = item.virtualKey;
    inputs[count].type = INPUT_KEYBOARD; inputs[count].ki.wVk = item.virtualKey; inputs[count++].ki.dwFlags = KEYEVENTF_KEYUP;
    for (int i = 3; i >= 0; --i) if (enabled[i]) { inputs[count].type = INPUT_KEYBOARD; inputs[count].ki.wVk = modifiers[i]; inputs[count++].ki.dwFlags = KEYEVENTF_KEYUP; }
    UINT sent = SendInput(count, inputs, sizeof(INPUT));
    if (g_statusText != nullptr)
        SetWindowTextW(g_statusText, sent == count ? L"Shortcut sent" : L"Unable to send shortcut. Try running with the same privilege level as the target application.");
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    using SetProcessDpiAwarenessContextFunction = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto setProcessDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContextFunction>(
        user32 == nullptr ? nullptr : GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setProcessDpiAwarenessContext != nullptr)
        setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls = { sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&controls);

    // TODO: 在此处放置代码。

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PROCQUIT, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // 执行应用程序初始化:
    if (!InitInstance (hInstance, nCmdShow))
    {
        if (SUCCEEDED(comResult)) CoUninitialize();
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_PROCQUIT));

    MSG msg;

    // 主消息循环:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (SUCCEEDED(comResult)) CoUninitialize();
    return (int) msg.wParam;
}



//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PROCQUIT));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_PROCQUIT);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 将实例句柄存储在全局变量中

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
//  WM_COMMAND  - 处理应用程序菜单
//  WM_PAINT    - 绘制主窗口
//  WM_DESTROY  - 发送退出消息并返回
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        AppendDiagnosticLog(L"Application window created");
        g_refreshButton = CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_REFRESH)), hInst, nullptr);
        g_exitButton = CreateWindowW(L"BUTTON", L"Request Exit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_REQUEST_EXIT)), hInst, nullptr);
        g_taskbarMenuButton = CreateWindowW(L"BUTTON", L"Right-click Taskbar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TASKBAR_MENU)), hInst, nullptr);
        g_copyLogsButton = CreateWindowW(L"BUTTON", L"Copy Logs", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COPY_LOGS)), hInst, nullptr);
        g_processList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROCESS_LIST)), hInst, nullptr);
        ListView_SetExtendedListViewStyle(g_processList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW column = { LVCF_TEXT | LVCF_WIDTH, 0, 230, const_cast<LPWSTR>(L"Name") };
        ListView_InsertColumn(g_processList, 0, &column);
        column.cx = 90; column.pszText = const_cast<LPWSTR>(L"PID");
        ListView_InsertColumn(g_processList, 1, &column);
        column.cx = 280; column.pszText = const_cast<LPWSTR>(L"Window title");
        ListView_InsertColumn(g_processList, 2, &column);
        column.cx = 120; column.pszText = const_cast<LPWSTR>(L"Exit availability");
        ListView_InsertColumn(g_processList, 3, &column);
        LoadShortcuts();
        if (LoadMouseTrigger())
        {
            g_mouseTriggerRegistered = RegisterHotKey(hWnd, ID_MOUSE_TRIGGER_HOTKEY,
                GetHotKeyModifiers(g_mouseTrigger), g_mouseTrigger.virtualKey) != FALSE;
            if (g_mouseTriggerRegistered)
                AppendDiagnosticLog(L"Mouse trigger hotkey registered: " + g_mouseTrigger.shortcut);
            else
                AppendDiagnosticLog(L"RegisterHotKey failed, error=" + std::to_wstring(GetLastError()));
        }
        for (size_t i = 0; i < g_shortcuts.size(); ++i)
        {
            std::wstring label = g_shortcuts[i].name + L"\n" + g_shortcuts[i].shortcut;
            HWND button = CreateWindowW(L"BUTTON", label.c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE,
                0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(2000 + i)), hInst, nullptr);
            g_shortcutButtons.push_back(button);
        }
        g_statusText = CreateWindowW(L"STATIC", g_shortcuts.empty() ? L"No valid shortcuts found in shortcuts.ini" : L"Click a shortcut to send it to the active application.",
            WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, hWnd, nullptr, hInst, nullptr);
        LayoutShortcuts(hWnd);
        RefreshProcesses();
        return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.x = 360;
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.y = 340;
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SIZE:
        LayoutShortcuts(hWnd);
        return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lParam)->idFrom == ID_PROCESS_LIST &&
            reinterpret_cast<NMHDR*>(lParam)->code == LVN_ITEMCHANGED)
            UpdateExitButton();
        return 0;
    case WM_EXIT_FINISHED:
        if (g_exitWorker.joinable()) g_exitWorker.join();
        g_exitPending = false;
        switch (static_cast<GracefulExitStatus>(wParam))
        {
        case GracefulExitStatus::Exited: SetWindowTextW(g_statusText, L"Process exited."); break;
        case GracefulExitStatus::StillRunning: SetWindowTextW(g_statusText, L"Process is still running."); break;
        case GracefulExitStatus::Unsupported: SetWindowTextW(g_statusText, L"No supported exit window found."); break;
        default: SetWindowTextW(g_statusText, L"Unable to request graceful exit."); break;
        }
        UpdateExitButton();
        return 0;
    case WM_HOTKEY:
        if (wParam == ID_MOUSE_TRIGGER_HOTKEY && g_mouseTriggerRegistered && !g_settingsOpen)
        {
            RequestMouseTrigger(hWnd);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == ID_MOUSE_TRIGGER_TIMER)
        {
            CompleteMouseTrigger(hWnd);
            return 0;
        }
        break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            if (wmId == ID_REFRESH) { RefreshProcesses(); return 0; }
            if (wmId == ID_REQUEST_EXIT) { RequestSelectedExit(hWnd); return 0; }
            if (wmId == ID_TASKBAR_MENU) { RequestSelectedTaskbarMenu(); return 0; }
            if (wmId == ID_COPY_LOGS) { CopyDiagnosticLogs(); return 0; }
            if (wmId == IDM_SETTINGS)
            {
                g_settingsOpen = true;
                DialogBox(hInst, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, Settings);
                g_settingsOpen = false;
                return 0;
            }
            if (wmId >= 2000 && static_cast<size_t>(wmId - 2000) < g_shortcutButtons.size())
            {
                SendShortcut(g_shortcuts[wmId - 2000]);
                return 0;
            }
            // 分析菜单选择:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: 在此处添加使用 hdc 的任何绘图代码...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        if (g_mouseTriggerPending)
            KillTimer(hWnd, ID_MOUSE_TRIGGER_TIMER);
        if (g_mouseTriggerRegistered)
        {
            UnregisterHotKey(hWnd, ID_MOUSE_TRIGGER_HOTKEY);
            g_mouseTriggerRegistered = false;
        }
        if (g_exitWorker.joinable()) g_exitWorker.join();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// “关于”框的消息处理程序。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK Settings(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        SetDlgItemTextW(hDlg, IDC_MOUSE_TRIGGER_SHORTCUT, g_mouseTrigger.shortcut.c_str());
        return static_cast<INT_PTR>(TRUE);
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            WCHAR shortcut[128] = {};
            GetDlgItemTextW(hDlg, IDC_MOUSE_TRIGGER_SHORTCUT, shortcut,
                static_cast<int>(_countof(shortcut)));
            std::wstring errorMessage;
            HWND owner = GetParent(hDlg);
            if (!UpdateMouseTrigger(owner, shortcut, errorMessage))
            {
                MessageBoxW(hDlg, errorMessage.c_str(), L"Settings", MB_OK | MB_ICONERROR);
                return static_cast<INT_PTR>(TRUE);
            }
            SetWindowTextW(g_statusText, L"Mouse trigger shortcut updated.");
            EndDialog(hDlg, IDOK);
            return static_cast<INT_PTR>(TRUE);
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return static_cast<INT_PTR>(TRUE);
        }
        break;
    }
    return static_cast<INT_PTR>(FALSE);
}
