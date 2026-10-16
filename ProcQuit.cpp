// ProcQuit.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "ProcQuit.h"
#include "ProcessExit.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <ole2.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <UIAutomation.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Uiautomationcore.lib")
#pragma comment(lib, "Shell32.lib")

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
    std::wstring category;
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
static HWND g_terminateButton = nullptr;
static HWND g_taskbarOnlyCheck = nullptr;
static HWND g_processSearch = nullptr;
static bool g_refreshingProcesses = false;
static int g_sortColumn = 0;
static bool g_sortDescending = false;
static HWND g_taskbarMenuButton = nullptr;
static HWND g_copyLogsButton = nullptr;
static HWND g_shortcutPanelButton = nullptr;
static HWND g_settingsButton = nullptr;
static HWND g_programExitButton = nullptr;
static HWND g_mainMinimizeButton = nullptr;
static HWND g_mainCloseButton = nullptr;
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
static constexpr int ID_SHORTCUT_ADD = 1907;
static constexpr int ID_SHORTCUT_PANEL_BUTTON = 1908;
static constexpr int ID_SHORTCUT_REORDER = 1909;
static constexpr int ID_SHORTCUT_CLOSE = 1910;
static constexpr int ID_SHORTCUT_MINIMIZE = 1914;
static constexpr int ID_MAIN_MINIMIZE = 1911;
static constexpr int ID_MAIN_CLOSE = 1912;
static constexpr int ID_MAIN_SETTINGS = 1913;
static constexpr int ID_SHORTCUT_CLEAR = 1915;
static constexpr int ID_SHORTCUT_RESET = 1916;
static constexpr int ID_MAIN_EXIT_PROGRAM = 1917;
static constexpr int ID_TERMINATE_PROCESS = 1918;
static constexpr int ID_TASKBAR_ONLY = 1919;
static constexpr int ID_PROCESS_SEARCH = 1920;
static constexpr int ID_COPY_NAME = 1921;
static constexpr int ID_COPY_PATH = 1922;
static constexpr int ID_COPY_COMMAND = 1923;
static constexpr int ID_OPEN_PATH = 1924;
static constexpr int MAIN_TITLE_BAR_HEIGHT = 32;
static constexpr int ID_SHORTCUT_SEND_BASE = 2000;
static constexpr int ID_SHORTCUT_DELETE_BASE = 3000;
static constexpr size_t MAX_SHORTCUTS = 500;
static constexpr wchar_t SHORTCUT_PANEL_CLASS[] = L"ProcQuitShortcutPanel";
static constexpr wchar_t SHORTCUT_CUSTOM_CATEGORY[] = L"自定义";
static HWND g_shortcutWindow = nullptr;
static std::vector<char> g_shortcutButtonHover;
static HWND g_shortcutAddButton = nullptr;
static HWND g_shortcutClearButton = nullptr;
static HWND g_shortcutResetButton = nullptr;
static HWND g_shortcutCloseButton = nullptr;
static HWND g_shortcutMinimizeButton = nullptr;
static HWND g_shortcutHintText = nullptr;
static bool g_shortcutPanelDragging = false;
static POINT g_shortcutPanelDragCursor = {};
static RECT g_shortcutPanelDragBounds = {};
static int g_shortcutScrollOffset = 0;
// 拖动按钮调整顺序时的状态；g_shortcutDragIndex 小于 0 表示没有拖动。
static int g_shortcutDragIndex = -1;
static int g_shortcutDropIndex = -1;
static POINT g_shortcutDragOrigin = {};
static bool g_shortcutDragging = false;
static ShortcutItem g_mouseTrigger = {};
static bool g_mouseTriggerRegistered = false;
static bool g_mouseTriggerPending = false;
static bool g_settingsOpen = false;
static POINT g_mouseTriggerPoint = {};
static ULONGLONG g_mouseTriggerDeadline = 0;

// 托盘图标：普通关闭只隐藏主窗口，程序常驻后台；托盘菜单提供“显示/退出”。
static NOTIFYICONDATAW g_trayIcon = {};
static constexpr UINT WM_TRAYICON = WM_APP + 2;
static constexpr UINT TRAY_CALLBACK_ID = 1;
static constexpr UINT TRAY_UID = 1;
static constexpr int ID_TRAY_SHOW = 1913;
static constexpr int ID_TRAY_EXIT = 1914;

struct ProcessRow
{
    DWORD pid;
    ULONGLONG creationTime;
    std::wstring name;
    std::wstring title;
    HWND window;
    std::wstring directory;
    std::wstring commandLine;
};
static std::vector<ProcessRow> g_processes;
static std::vector<ProcessRow> g_allProcesses;

static void CopyTextToClipboard(const std::wstring& text)
{
    if (text.empty() || text == L"Unavailable" || !OpenClipboard(nullptr)) return;
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) { CloseClipboard(); return; }
    auto* target = static_cast<wchar_t*>(GlobalLock(memory));
    if (target == nullptr) { GlobalFree(memory); CloseClipboard(); return; }
    memcpy(target, text.c_str(), bytes);
    GlobalUnlock(memory);
    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) GlobalFree(memory);
    CloseClipboard();
}

static void ShowProcessContextMenu(HWND hWnd, int index, POINT point)
{
    if (index < 0 || index >= static_cast<int>(g_processes.size())) return;
    const ProcessRow target = g_processes[index];
    ListView_SetItemState(g_processList, index, LVIS_SELECTED, LVIS_SELECTED);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_COPY_NAME, L"Copy name");
    AppendMenuW(menu, MF_STRING, ID_COPY_PATH, L"Copy path");
    AppendMenuW(menu, MF_STRING, ID_COPY_COMMAND, L"Copy command line");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_OPEN_PATH, L"Open process location");
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, 0, hWnd, nullptr);
    DestroyMenu(menu);
    if (command == ID_COPY_NAME) CopyTextToClipboard(target.name);
    else if (command == ID_COPY_PATH) CopyTextToClipboard(target.directory);
    else if (command == ID_COPY_COMMAND) CopyTextToClipboard(target.commandLine);
    else if (command == ID_OPEN_PATH && target.directory != L"Unavailable")
        ShellExecuteW(hWnd, L"open", target.directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static std::vector<ProcessRow> GetSelectedProcesses()
{
    std::vector<ProcessRow> targets;
    for (int index = 0; index < static_cast<int>(g_processes.size()); ++index)
        if (ListView_GetCheckState(g_processList, index) ||
            (ListView_GetItemState(g_processList, index, LVIS_SELECTED) & LVIS_SELECTED) != 0)
            targets.push_back(g_processes[index]);
    return targets;
}

static std::wstring ReadProcessCommandLine(HANDLE process)
{
    using QueryProcess = LONG (NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto query = reinterpret_cast<QueryProcess>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (query == nullptr) return L"Unavailable";
    struct CommandLineString { USHORT length; USHORT maximumLength; PWSTR buffer; };
    ULONG required = 0;
    query(process, 60, nullptr, 0, &required);
    if (required < sizeof(CommandLineString) || required > 1024 * 1024) return L"Unavailable";
    std::vector<BYTE> data(required);
    if (query(process, 60, data.data(), required, &required) < 0) return L"Unavailable";
    const auto* text = reinterpret_cast<const CommandLineString*>(data.data());
    ULONG_PTR begin = reinterpret_cast<ULONG_PTR>(data.data());
    ULONG_PTR pointer = reinterpret_cast<ULONG_PTR>(text->buffer);
    if (text->length % sizeof(wchar_t) != 0 || pointer < begin ||
        pointer - begin > data.size() || text->length > data.size() - (pointer - begin))
        return L"Unavailable";
    return std::wstring(text->buffer, text->length / sizeof(wchar_t));
}

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

static bool RequestTaskbarMenu(const ProcessRow& target)
{
    if (!IsCurrentWindowInstance(target))
    {
        SetWindowTextW(g_statusText, L"The selected window is no longer available. Refresh the list.");
        return false;
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
            return true;
        }
    }

    AppendDiagnosticLog(L"Unable to identify a unique taskbar button for PID " + std::to_wstring(target.pid));
    SetWindowTextW(g_statusText, L"Unable to identify a unique taskbar button for the selected window.");
    return false;
}

static void RequestSelectedTaskbarMenu()
{
    size_t sent = 0;
    const auto targets = GetSelectedProcesses();
    for (const auto& target : targets)
        if (RequestTaskbarMenu(target)) ++sent;
    SetWindowTextW(g_statusText, (L"Taskbar right-click sent: " + std::to_wstring(sent) +
        L" / " + std::to_wstring(targets.size()) + L". Only the last menu can remain open.").c_str());
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

    bool hasSelection = false;
    for (int index = 0; index < ListView_GetItemCount(g_processList) && !hasSelection; ++index)
        hasSelection = ListView_GetCheckState(g_processList, index) != FALSE ||
            (ListView_GetItemState(g_processList, index, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    if (g_exitButton != nullptr)
        EnableWindow(g_exitButton, !g_exitPending && hasSelection);
    if (g_taskbarMenuButton != nullptr)
        EnableWindow(g_taskbarMenuButton, !g_exitPending && hasSelection);
    if (g_terminateButton != nullptr)
        EnableWindow(g_terminateButton, !g_exitPending && hasSelection);
}

static void DisplayProcesses(bool preserveSelection = true)
{
    if (g_processList == nullptr) return;
    const auto selected = preserveSelection ? GetSelectedProcesses() : std::vector<ProcessRow>();
    const int length = GetWindowTextLengthW(g_processSearch);
    std::vector<wchar_t> search(static_cast<size_t>(length) + 1);
    GetWindowTextW(g_processSearch, search.data(), static_cast<int>(search.size()));
    const std::wstring query = ToLower(search.data());
    g_refreshingProcesses = true;
    SendMessageW(g_processList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_processList);
    g_processes.clear();
    for (const auto& target : g_allProcesses)
    {
        if (query.empty() || ToLower(target.name).find(query) != std::wstring::npos ||
            std::to_wstring(target.pid).find(query) != std::wstring::npos ||
            ToLower(target.directory).find(query) != std::wstring::npos ||
            ToLower(target.commandLine).find(query) != std::wstring::npos ||
            ToLower(target.title).find(query) != std::wstring::npos)
            g_processes.push_back(target);
    }
    std::stable_sort(g_processes.begin(), g_processes.end(), [](const ProcessRow& left, const ProcessRow& right) {
        int comparison = g_sortColumn == 1 ? (left.pid < right.pid ? -1 : left.pid > right.pid ? 1 : 0)
            : _wcsicmp(left.name.c_str(), right.name.c_str());
        if (comparison == 0) comparison = left.pid < right.pid ? -1 : left.pid > right.pid ? 1 : 0;
        return g_sortDescending ? comparison > 0 : comparison < 0;
    });
    for (const auto& target : g_processes)
    {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = ListView_GetItemCount(g_processList);
        item.pszText = const_cast<LPWSTR>(target.name.c_str());
        int row = ListView_InsertItem(g_processList, &item);
        std::wstring pid = std::to_wstring(target.pid);
        ListView_SetItemText(g_processList, row, 1, const_cast<LPWSTR>(pid.c_str()));
        ListView_SetItemText(g_processList, row, 2, const_cast<LPWSTR>(target.title.c_str()));
        LPWSTR availability = target.creationTime && target.pid != GetCurrentProcessId()
            ? const_cast<LPWSTR>(L"Ready") : const_cast<LPWSTR>(L"Unavailable");
        ListView_SetItemText(g_processList, row, 3, availability);
        ListView_SetItemText(g_processList, row, 4, const_cast<LPWSTR>(target.directory.c_str()));
        ListView_SetItemText(g_processList, row, 5, const_cast<LPWSTR>(target.commandLine.c_str()));
        for (const auto& previous : selected)
            if (previous.pid == target.pid && previous.creationTime == target.creationTime)
            {
                ListView_SetCheckState(g_processList, row, TRUE);
                ListView_SetItemState(g_processList, row, LVIS_SELECTED, LVIS_SELECTED);
                break;
            }
    }
    HWND header = ListView_GetHeader(g_processList);
    for (int column = 0; column < 2; ++column)
    {
        HDITEMW item = {};
        item.mask = HDI_FORMAT;
        Header_GetItem(header, column, &item);
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (column == g_sortColumn) item.fmt |= g_sortDescending ? HDF_SORTDOWN : HDF_SORTUP;
        Header_SetItem(header, column, &item);
    }
    SendMessageW(g_processList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_processList, nullptr, TRUE);
    g_refreshingProcesses = false;
    UpdateExitButton();
    SetWindowTextW(g_statusText, (L"Showing " + std::to_wstring(g_processes.size()) +
        L" / " + std::to_wstring(g_allProcesses.size()) + L" processes.").c_str());
}

static void RefreshProcesses()
{
    if (g_processList == nullptr) return;
    g_allProcesses.clear();

    std::unordered_map<DWORD, HWND> taskbarWindows;
    std::vector<HWND> windows;
    EnumWindows(CollectTaskbarApplications, reinterpret_cast<LPARAM>(&windows));
    for (HWND window : windows)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        taskbarWindows.emplace(pid, window);
    }
    const bool taskbarOnly = SendMessageW(g_taskbarOnlyCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                auto window = taskbarWindows.find(entry.th32ProcessID);
                if (taskbarOnly && window == taskbarWindows.end()) continue;
                ProcessRow target = {};
                target.pid = entry.th32ProcessID;
                target.name = entry.szExeFile;
                target.window = window == taskbarWindows.end() ? nullptr : window->second;
                target.title = GetWindowTitle(target.window);
                target.directory = L"Unavailable";
                target.commandLine = L"Unavailable";
                HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
                if (process != nullptr)
                {
                    FILETIME start = {}, end = {}, kernel = {}, user = {};
                    if (GetProcessTimes(process, &start, &end, &kernel, &user))
                    {
                        ULARGE_INTEGER value = {};
                        value.LowPart = start.dwLowDateTime;
                        value.HighPart = start.dwHighDateTime;
                        target.creationTime = value.QuadPart;
                    }
                    std::vector<wchar_t> path(32768);
                    DWORD length = static_cast<DWORD>(path.size());
                    if (QueryFullProcessImageNameW(process, 0, path.data(), &length))
                    {
                        target.directory.assign(path.data(), length);
                        size_t slash = target.directory.find_last_of(L"\\/");
                        if (slash != std::wstring::npos) target.directory.resize(slash + 1);
                    }
                    target.commandLine = ReadProcessCommandLine(process);
                    CloseHandle(process);
                }
                g_allProcesses.push_back(std::move(target));
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    DisplayProcesses(false);
    AppendDiagnosticLog(L"Processes enumerated: " + std::to_wstring(g_allProcesses.size()));
}

static void RequestSelectedExit(HWND hWnd, bool force = false)
{
    if (g_exitPending) return;
    auto targets = GetSelectedProcesses();
    if (targets.empty()) return;
    if (force && MessageBoxW(hWnd, L"Force terminate all selected processes? Unsaved data may be lost.",
        L"Confirm termination", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    if (g_exitWorker.joinable()) g_exitWorker.join();
    g_exitPending = true;
    UpdateExitButton();
    EnableWindow(g_refreshButton, FALSE);
    EnableWindow(g_taskbarOnlyCheck, FALSE);
    SetWindowTextW(g_statusText, force ? L"Terminating selected processes..." : L"Requesting graceful exit...");
    g_exitWorker = std::thread([hWnd, targets = std::move(targets), force]() {
        size_t completed = 0;
        for (const auto& target : targets)
        {
            if (!target.creationTime || target.pid == GetCurrentProcessId() || target.pid <= 4)
            {
                AppendDiagnosticLog(L"Skipped unavailable or protected PID " + std::to_wstring(target.pid));
                continue;
            }
            if (force)
            {
                HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                    FALSE, target.pid);
                if (process != nullptr)
                {
                    FILETIME start = {}, end = {}, kernel = {}, user = {};
                    ULARGE_INTEGER created = {};
                    bool valid = GetProcessTimes(process, &start, &end, &kernel, &user) != FALSE;
                    created.LowPart = start.dwLowDateTime;
                    created.HighPart = start.dwHighDateTime;
                    if (valid && created.QuadPart == target.creationTime && TerminateProcess(process, 1))
                    {
                        if (WaitForSingleObject(process, 5000) == WAIT_OBJECT_0) ++completed;
                    }
                    else
                        AppendDiagnosticLog(L"Termination failed or instance changed for PID " +
                            std::to_wstring(target.pid) + L", error=" + std::to_wstring(GetLastError()));
                    CloseHandle(process);
                }
                else
                    AppendDiagnosticLog(L"OpenProcess for termination failed for PID " +
                        std::to_wstring(target.pid) + L", error=" + std::to_wstring(GetLastError()));
            }
            else if (RequestGracefulExit(target.pid, target.creationTime, 5000).status == GracefulExitStatus::Exited)
                ++completed;
        }
        PostMessageW(hWnd, WM_EXIT_FINISHED, static_cast<WPARAM>(completed), static_cast<LPARAM>(targets.size()));
    });
}

static std::wstring Trim(const std::wstring& value)
{
    size_t start = 0;
    size_t end = value.size();
    while (start < end && iswspace(value[start]) != 0) ++start;
    while (end > start && iswspace(value[end - 1]) != 0) --end;
    return value.substr(start, end - start);
}

static bool ParseShortcut(const std::wstring& text, ShortcutItem& item)
{
    item.virtualKey = 0;
    item.control = false;
    item.alt = false;
    item.shift = false;
    item.windows = false;

    WORD key = 0;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(L'+', start);
        std::wstring token = Trim(text.substr(start, end == std::wstring::npos ? end : end - start));
        if (token.empty()) return false;
        if (token == L"Ctrl")
        {
            if (item.control) return false;
            item.control = true;
        }
        else if (token == L"Alt")
        {
            if (item.alt) return false;
            item.alt = true;
        }
        else if (token == L"Shift")
        {
            if (item.shift) return false;
            item.shift = true;
        }
        else if (token == L"Win")
        {
            if (item.windows) return false;
            item.windows = true;
        }
        else if (key != 0) return false;
        else if (token.size() == 1 && ((token[0] >= L'A' && token[0] <= L'Z') || (token[0] >= L'a' && token[0] <= L'z') ||
                 (token[0] >= L'0' && token[0] <= L'9')))
        {
            SHORT scan = VkKeyScanW(static_cast<WCHAR>(towupper(token[0])));
            if (scan == -1) return false;
            key = static_cast<WORD>(scan & 0xff);
        }
        else if (token.size() >= 2 && token[0] == L'F')
        {
            for (size_t index = 1; index < token.size(); ++index)
                if (token[index] < L'0' || token[index] > L'9') return false;
            int functionNumber = _wtoi(token.c_str() + 1);
            if (functionNumber < 1 || functionNumber > 24) return false;
            key = static_cast<WORD>(VK_F1 + functionNumber - 1);
        }
        else if (token == L"Space") key = VK_SPACE;
        else if (token == L"Enter") key = VK_RETURN;
        else if (token == L"Tab") key = VK_TAB;
        else if (token == L"Esc") key = VK_ESCAPE;
        else if (token == L"Delete") key = VK_DELETE;
        else if (token == L"Backspace") key = VK_BACK;
        else if (token == L"Insert") key = VK_INSERT;
        else if (token == L"Left") key = VK_LEFT;
        else if (token == L"Right") key = VK_RIGHT;
        else if (token == L"Up") key = VK_UP;
        else if (token == L"Down") key = VK_DOWN;
        else if (token == L"Home") key = VK_HOME;
        else if (token == L"End") key = VK_END;
        else if (token == L"PageUp") key = VK_PRIOR;
        else if (token == L"PageDown") key = VK_NEXT;
        else if (token == L"PrintScreen") key = VK_SNAPSHOT;
        else if (token == L"Pause") key = VK_PAUSE;
        else if (token == L"NumLock") key = VK_NUMLOCK;
        else if (token == L"Plus") key = VK_OEM_PLUS;
        else if (token == L"Minus") key = VK_OEM_MINUS;
        else if (token == L"Period") key = VK_OEM_PERIOD;
        else if (token == L"Comma") key = VK_OEM_COMMA;
        else if (token == L"Semicolon") key = VK_OEM_1;
        else if (token == L"Slash") key = VK_OEM_2;
        else return false;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    if (key == 0)
    {
        // 只有修饰键的写法表示单独按下该修饰键，例如 Win 打开开始菜单。
        WORD fallback = 0;
        int modifierCount = 0;
        if (item.control) { fallback = VK_LCONTROL; ++modifierCount; }
        if (item.alt) { fallback = VK_LMENU; ++modifierCount; }
        if (item.shift) { fallback = VK_LSHIFT; ++modifierCount; }
        if (item.windows) { fallback = VK_LWIN; ++modifierCount; }
        if (modifierCount != 1) return false;
        item.control = false;
        item.alt = false;
        item.shift = false;
        item.windows = false;
        key = fallback;
    }
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

// 按 BOM 判断编码后整份读入，兼容手工建立的 ANSI/UTF-8 配置。
static bool ReadConfigurationText(const std::wstring& path, std::wstring& text)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file, &size) == FALSE || size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024)
    {
        CloseHandle(file);
        return false;
    }

    std::vector<char> bytes(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (ok == FALSE || read == 0)
        return false;
    bytes.resize(read);

    const unsigned char* data = reinterpret_cast<const unsigned char*>(bytes.data());
    if (bytes.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE)
    {
        text.resize((bytes.size() - 2) / sizeof(wchar_t));
        if (!text.empty()) memcpy(&text[0], data + 2, text.size() * sizeof(wchar_t));
        return true;
    }

    UINT codePage = CP_ACP;
    size_t offset = 0;
    if (bytes.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
    {
        codePage = CP_UTF8;
        offset = 3;
    }

    int chars = MultiByteToWideChar(codePage, 0, reinterpret_cast<const char*>(data + offset),
        static_cast<int>(bytes.size() - offset), nullptr, 0);
    if (chars <= 0)
        return false;
    text.resize(static_cast<size_t>(chars));
    MultiByteToWideChar(codePage, 0, reinterpret_cast<const char*>(data + offset),
        static_cast<int>(bytes.size() - offset), &text[0], chars);
    return true;
}

// 直接按行解析：拖动调整顺序后同一个分类可能在文件里出现多次，
// Profile API 只会读到第一段，所以这里自己读。
static bool ParseShortcutsText(const std::wstring& text, std::vector<ShortcutItem>& shortcuts, bool rejectInvalid = false)
{
    std::wstring category;
    size_t position = 0;
    while (position < text.size() && shortcuts.size() < MAX_SHORTCUTS)
    {
        size_t lineEnd = text.find(L'\n', position);
        if (lineEnd == std::wstring::npos) lineEnd = text.size();
        std::wstring line = Trim(text.substr(position, lineEnd - position));
        position = lineEnd + 1;

        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line.size() >= 2 && line.front() == L'[' && line.back() == L']')
        {
            category = line.substr(1, line.size() - 2);
            continue;
        }

        size_t separator = line.find(L'=');
        if (separator == std::wstring::npos)
        {
            if (rejectInvalid) return false;
            continue;
        }
        ShortcutItem item = {};
        item.category = category;
        item.name = Trim(line.substr(0, separator));
        item.shortcut = Trim(line.substr(separator + 1));
        if (item.name.empty() || !ParseShortcut(item.shortcut, item))
        {
            if (rejectInvalid) return false;
            continue;
        }
        shortcuts.push_back(item);
    }
    return !rejectInvalid || position >= text.size();
}

static void LoadShortcuts()
{
    std::wstring configPath = GetConfigurationPath(L"shortcuts.ini");
    if (configPath.empty()) return;

    std::wstring text;
    if (!ReadConfigurationText(configPath, text)) return;

    std::vector<ShortcutItem> shortcuts;
    ParseShortcutsText(text, shortcuts);
    g_shortcuts = std::move(shortcuts);
}

static bool LoadDefaultShortcuts(std::vector<ShortcutItem>& shortcuts)
{
    HRSRC resource = FindResourceW(hInst, MAKEINTRESOURCEW(IDR_DEFAULT_SHORTCUTS), RT_RCDATA);
    if (resource == nullptr)
        return false;
    HGLOBAL loaded = LoadResource(hInst, resource);
    const void* data = loaded != nullptr ? LockResource(loaded) : nullptr;
    DWORD size = SizeofResource(hInst, resource);
    if (data == nullptr || size < 2 || (size - 2) % sizeof(wchar_t) != 0)
        return false;

    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    if (bytes[0] != 0xFF || bytes[1] != 0xFE)
        return false;

    std::wstring text;
    text.resize((size - 2) / sizeof(wchar_t));
    if (!text.empty()) memcpy(&text[0], bytes + 2, text.size() * sizeof(wchar_t));
    return ParseShortcutsText(text, shortcuts, true) && !shortcuts.empty();
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

static void LayoutMainWindow(HWND hWnd)
{
    RECT client = {};
    GetClientRect(hWnd, &client);
    const int margin = 12;
    const int buttonHeight = 28;
    int top = MAIN_TITLE_BAR_HEIGHT + 8;
    int listTop = top + (buttonHeight + 8) * 2;
    int listHeight = client.bottom - listTop - 40;
    if (listHeight < 40) listHeight = 40;

    HDWP defer = BeginDeferWindowPos(14);
    auto place = [&defer](HWND window, int x, int y, int width, int height) {
        if (window == nullptr || defer == nullptr) return;
        defer = DeferWindowPos(defer, window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    place(g_mainCloseButton, client.right - 36 - margin, (MAIN_TITLE_BAR_HEIGHT - 24) / 2, 36, 24);
    place(g_mainMinimizeButton, client.right - 36 - margin - 40, (MAIN_TITLE_BAR_HEIGHT - 24) / 2, 36, 24);
    place(g_refreshButton, margin, top, 90, buttonHeight);
    place(g_exitButton, margin + 100, top, 130, buttonHeight);
    place(g_taskbarMenuButton, margin + 240, top, 160, buttonHeight);
    place(g_copyLogsButton, margin + 410, top, 120, buttonHeight);
    place(g_shortcutPanelButton, margin + 540, top, 110, buttonHeight);
    place(g_settingsButton, margin + 660, top, 100, buttonHeight);
    place(g_terminateButton, margin, top + buttonHeight + 8, 150, buttonHeight);
    place(g_taskbarOnlyCheck, margin + 166, top + buttonHeight + 8, 220, buttonHeight);
    place(g_processSearch, margin + 400, top + buttonHeight + 8, client.right - margin * 2 - 400, buttonHeight);
    place(g_processList, margin, listTop, client.right - margin * 2, listHeight);
    place(g_statusText, margin, client.bottom - 30, client.right - margin * 2 - 114, 20);
    place(g_programExitButton, client.right - margin - 104, client.bottom - 34, 104, 26);
    if (defer != nullptr) EndDeferWindowPos(defer);
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

static constexpr int SHORTCUT_PANEL_MARGIN = 8;
static constexpr int SHORTCUT_PANEL_GAP = 4;
static constexpr int SHORTCUT_CELL_WIDTH = 88;
static constexpr int SHORTCUT_CELL_HEIGHT = 36;
static constexpr int SHORTCUT_MIN_CELL_WIDTH = 64;
static constexpr int SHORTCUT_DELETE_SIZE = 12;
static constexpr int SHORTCUT_HOVER_NONE = 0;
static constexpr int SHORTCUT_HOVER_CELL = 1;
static constexpr int SHORTCUT_HOVER_DELETE = 2;
static constexpr int SHORTCUT_HOVER_PRESSED = 3;
static constexpr int SHORTCUT_HOVER_DROP = 4;
static constexpr int SHORTCUT_PANEL_FOOTER = 28;
static constexpr int SHORTCUT_PANEL_TITLE_BAR_HEIGHT = 32;
static constexpr int SHORTCUT_MIN_COLUMNS = 3;
static constexpr int SHORTCUT_MIN_ROWS = 3;
static constexpr int SHORTCUT_INITIAL_COLUMNS = 5;
// WS_CLIPCHILDREN 让父窗口不再重画按钮占用的区域，重建和排版时不会闪白。
static constexpr DWORD SHORTCUT_PANEL_STYLE = WS_POPUP | WS_THICKFRAME | WS_VSCROLL | WS_CLIPCHILDREN;
static constexpr DWORD SHORTCUT_PANEL_EX_STYLE = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
static bool g_shortcutScrollVisible = false;

static int GetShortcutPanelRowStep()
{
    return SHORTCUT_CELL_HEIGHT + SHORTCUT_PANEL_GAP;
}

static int GetShortcutPanelInnerWidth(HWND hWnd)
{
    RECT client = {};
    GetClientRect(hWnd, &client);
    int width = client.right;
    if (g_shortcutScrollVisible) width += GetSystemMetrics(SM_CXVSCROLL);
    return width;
}

static int GetShortcutPanelAvailableWidth(HWND hWnd)
{
    int width = GetShortcutPanelInnerWidth(hWnd) - SHORTCUT_PANEL_MARGIN * 2;
    return width < 0 ? 0 : width;
}

static int GetShortcutPanelColumns(HWND hWnd)
{
    int available = GetShortcutPanelAvailableWidth(hWnd);
    int columns = (available + SHORTCUT_PANEL_GAP) / (SHORTCUT_CELL_WIDTH + SHORTCUT_PANEL_GAP);
    return columns < SHORTCUT_MIN_COLUMNS ? SHORTCUT_MIN_COLUMNS : columns;
}

static int GetShortcutPanelCellWidth(HWND hWnd, int columns)
{
    int available = GetShortcutPanelAvailableWidth(hWnd);
    int cellWidth = columns > 0 ? (available - (columns - 1) * SHORTCUT_PANEL_GAP) / columns : SHORTCUT_CELL_WIDTH;
    return cellWidth < SHORTCUT_MIN_CELL_WIDTH ? SHORTCUT_MIN_CELL_WIDTH : cellWidth;
}

static int GetShortcutPanelMinClientWidth()
{
    return SHORTCUT_PANEL_MARGIN * 2 + GetSystemMetrics(SM_CXVSCROLL) +
        SHORTCUT_MIN_COLUMNS * SHORTCUT_CELL_WIDTH + (SHORTCUT_MIN_COLUMNS - 1) * SHORTCUT_PANEL_GAP;
}

static int GetShortcutPanelMinClientHeight()
{
    return SHORTCUT_PANEL_MARGIN + SHORTCUT_MIN_ROWS * GetShortcutPanelRowStep() - SHORTCUT_PANEL_GAP +
        SHORTCUT_PANEL_FOOTER + SHORTCUT_PANEL_TITLE_BAR_HEIGHT;
}

static int GetShortcutPanelInitialClientWidth()
{
    return SHORTCUT_PANEL_MARGIN * 2 + GetSystemMetrics(SM_CXVSCROLL) +
        SHORTCUT_INITIAL_COLUMNS * SHORTCUT_CELL_WIDTH + (SHORTCUT_INITIAL_COLUMNS - 1) * SHORTCUT_PANEL_GAP;
}

static void AdjustShortcutPanelWindowRect(int clientWidth, int clientHeight, int& width, int& height)
{
    RECT rect = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRectEx(&rect, SHORTCUT_PANEL_STYLE, FALSE, SHORTCUT_PANEL_EX_STYLE);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
}

// 批量改动子控件前先关闭重绘，改完一次性刷新，避免逐个控件刷新造成的闪烁。
static void BeginShortcutPanelUpdate(HWND hWnd)
{
    SendMessageW(hWnd, WM_SETREDRAW, FALSE, 0);
}

static void EndShortcutPanelUpdate(HWND hWnd)
{
    SendMessageW(hWnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// 按钮上只显示按键组合；过长时在加号处折行，保证单行不会超出按钮宽度。
static std::wstring GetShortcutButtonLabel(const ShortcutItem& item)
{
    std::wstring keys = item.shortcut;
    if (keys.size() > 16)
    {
        size_t split = keys.rfind(L'+', 16);
        if (split != std::wstring::npos && split + 1 < keys.size())
            keys = keys.substr(0, split + 1) + L"\n" + keys.substr(split + 1);
    }
    return keys;
}

// 按顺序走一遍网格：apply 为真时同时移动子控件，返回内容底部坐标。
static int WalkShortcutPanel(HWND hWnd, int yOffset, bool apply)
{
    int columns = GetShortcutPanelColumns(hWnd);
    int cellWidth = GetShortcutPanelCellWidth(hWnd, columns);
    int rowStep = GetShortcutPanelRowStep();
    RECT client = {};
    GetClientRect(hWnd, &client);
    int viewportBottom = client.bottom - SHORTCUT_PANEL_FOOTER;
    int column = 0;
    int x = SHORTCUT_PANEL_MARGIN;
    int y = SHORTCUT_PANEL_MARGIN + SHORTCUT_PANEL_TITLE_BAR_HEIGHT;
    int bottom = SHORTCUT_PANEL_MARGIN + SHORTCUT_PANEL_TITLE_BAR_HEIGHT;

    // 所有按钮的位置一次提交，避免逐个移动时画出中间状态。
    HDWP defer = apply ? BeginDeferWindowPos(static_cast<int>(g_shortcuts.size())) : nullptr;

    for (size_t index = 0; index < g_shortcuts.size(); ++index)
    {
        if (apply && index < g_shortcutButtons.size() && g_shortcutButtons[index] != nullptr)
        {
            HWND button = g_shortcutButtons[index];
            bool visible = y + yOffset >= 0 && y + yOffset + SHORTCUT_CELL_HEIGHT <= viewportBottom;
            // 状态没变就不要调用，否则每次排版都会多一次无谓的重画。
            if ((IsWindowVisible(button) != FALSE) != visible)
                ShowWindow(button, visible ? SW_SHOWNA : SW_HIDE);
            if (defer != nullptr)
                defer = DeferWindowPos(defer, button, nullptr, x, y + yOffset, cellWidth, SHORTCUT_CELL_HEIGHT,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            else
                MoveWindow(button, x, y + yOffset, cellWidth, SHORTCUT_CELL_HEIGHT, TRUE);
        }
        bottom = y + SHORTCUT_CELL_HEIGHT;

        ++column;
        if (column >= columns)
        {
            column = 0;
            x = SHORTCUT_PANEL_MARGIN;
            y += rowStep;
        }
        else
        {
            x += cellWidth + SHORTCUT_PANEL_GAP;
        }
    }

    if (defer != nullptr)
        EndDeferWindowPos(defer);

    return bottom;
}

static void UpdateShortcutPanelScroll(HWND hWnd)
{
    RECT client = {};
    GetClientRect(hWnd, &client);
    int viewportBottom = client.bottom - SHORTCUT_PANEL_FOOTER;
    if (viewportBottom < 1) viewportBottom = 1;

    int step = GetShortcutPanelRowStep();
    int overflow = WalkShortcutPanel(hWnd, 0, false) + SHORTCUT_PANEL_MARGIN - viewportBottom;
    int maxScroll = overflow > 0 ? ((overflow + step - 1) / step) * step : 0;

    if (g_shortcutScrollOffset > maxScroll) g_shortcutScrollOffset = maxScroll;
    if (g_shortcutScrollOffset < 0) g_shortcutScrollOffset = 0;

    bool visible = maxScroll > 0;
    if (visible != g_shortcutScrollVisible)
    {
        g_shortcutScrollVisible = visible;
        ShowScrollBar(hWnd, SB_VERT, visible);
    }

    SCROLLINFO info = {};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = maxScroll + viewportBottom - 1;
    info.nPage = static_cast<UINT>(viewportBottom);
    info.nPos = g_shortcutScrollOffset;
    SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
}

static void LayoutShortcutPanel(HWND hWnd)
{
    RECT client = {};
    GetClientRect(hWnd, &client);

    WalkShortcutPanel(hWnd, -g_shortcutScrollOffset, true);

    if (g_shortcutMinimizeButton != nullptr)
        MoveWindow(g_shortcutMinimizeButton, client.right - 36 - SHORTCUT_PANEL_MARGIN - 40,
            (SHORTCUT_PANEL_TITLE_BAR_HEIGHT - 24) / 2, 36, 24, TRUE);
    if (g_shortcutCloseButton != nullptr)
        MoveWindow(g_shortcutCloseButton, client.right - 36 - SHORTCUT_PANEL_MARGIN,
            (SHORTCUT_PANEL_TITLE_BAR_HEIGHT - 24) / 2, 36, 24, TRUE);

    int footerTop = client.bottom - SHORTCUT_PANEL_FOOTER + 4;
    if (g_shortcutAddButton != nullptr)
        MoveWindow(g_shortcutAddButton, SHORTCUT_PANEL_MARGIN, footerTop, 86, 22, TRUE);
    if (g_shortcutClearButton != nullptr)
        MoveWindow(g_shortcutClearButton, SHORTCUT_PANEL_MARGIN + 92, footerTop, 68, 22, TRUE);
    if (g_shortcutResetButton != nullptr)
        MoveWindow(g_shortcutResetButton, SHORTCUT_PANEL_MARGIN + 166, footerTop, 58, 22, TRUE);
    if (g_shortcutHintText != nullptr)
    {
        int hintWidth = client.right - SHORTCUT_PANEL_MARGIN * 2 - 232;
        ShowWindow(g_shortcutHintText, hintWidth >= 120 ? SW_SHOWNA : SW_HIDE);
        MoveWindow(g_shortcutHintText, SHORTCUT_PANEL_MARGIN + 232, footerTop + 3,
            hintWidth > 0 ? hintWidth : 0, 16, TRUE);
    }
}

static void HideShortcutTooltip();

static void RefreshShortcutPanel(HWND hWnd)
{
    // 重新排版后提示指向的位置就不对了，先收起来。
    HideShortcutTooltip();
    BeginShortcutPanelUpdate(hWnd);
    UpdateShortcutPanelScroll(hWnd);
    LayoutShortcutPanel(hWnd);
    EndShortcutPanelUpdate(hWnd);
}

// 面板上的按钮全部自绘：白底、细边框，鼠标悬停时边框和底色变化。
// 关闭区画在同一个按钮内，避免两个重叠子窗口抢点击。
static void DrawShortcutCell(const DRAWITEMSTRUCT* item, int hoverState)
{
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    bool pressed = (item->itemState & ODS_SELECTED) != 0 || hoverState == SHORTCUT_HOVER_PRESSED;
    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool overClose = hoverState == SHORTCUT_HOVER_DELETE;
    bool dropTarget = hoverState == SHORTCUT_HOVER_DROP;

    RECT closeRect = rect;
    closeRect.left = rect.right - SHORTCUT_DELETE_SIZE - 2;

    COLORREF fill = RGB(255, 255, 255);
    if (dropTarget) fill = RGB(255, 243, 224);
    else if (pressed) fill = RGB(214, 232, 250);
    else if (hoverState != SHORTCUT_HOVER_NONE) fill = RGB(240, 247, 254);

    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);

    if (overClose)
    {
        HBRUSH closeBrush = CreateSolidBrush(RGB(209, 52, 56));
        FillRect(dc, &closeRect, closeBrush);
        DeleteObject(closeBrush);
    }

    COLORREF border = RGB(200, 200, 200);
    int borderWidth = 1;
    if (disabled) border = RGB(228, 228, 228);
    else if (dropTarget) { border = RGB(240, 150, 0); borderWidth = 2; }
    else if (pressed || hoverState != SHORTCUT_HOVER_NONE) border = RGB(0, 120, 212);

    HPEN pen = CreatePen(PS_SOLID, borderWidth, border);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);

    WCHAR text[128] = {};
    GetWindowTextW(item->hwndItem, text, static_cast<int>(_countof(text)));
    if (text[0] != L'\0')
    {
        SetTextColor(dc, disabled ? GetSysColor(COLOR_GRAYTEXT) : RGB(32, 32, 32));
        RECT textRect = rect;
        textRect.right = closeRect.left - 2;

        // DrawText 的 DT_VCENTER 只对单行有效，多行时先量高度再手动垂直居中。
        RECT measure = textRect;
        DrawTextW(dc, text, -1, &measure, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        int offset = ((textRect.bottom - textRect.top) - (measure.bottom - measure.top)) / 2;
        if (offset > 0) textRect.top += offset;
        DrawTextW(dc, text, -1, &textRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    }

    SetTextColor(dc, overClose ? RGB(255, 255, 255) : RGB(120, 120, 120));
    DrawTextW(dc, L"×", -1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (oldFont != nullptr) SelectObject(dc, oldFont);
}

// 关闭区位于按钮右上角，与绘制时的矩形保持一致。
static bool IsShortcutCloseZone(HWND hWnd, LPARAM lParam)
{
    RECT client = {};
    GetClientRect(hWnd, &client);
    int x = static_cast<short>(LOWORD(lParam));
    return x >= client.right - SHORTCUT_DELETE_SIZE - 2;
}

static void SetShortcutButtonHoverState(HWND hWnd, int state)
{
    size_t index = static_cast<size_t>(GetDlgCtrlID(hWnd) - ID_SHORTCUT_SEND_BASE);
    if (index >= g_shortcutButtonHover.size() || g_shortcutButtonHover[index] == state)
        return;
    g_shortcutButtonHover[index] = static_cast<char>(state);
    InvalidateRect(hWnd, nullptr, FALSE);
}

static constexpr int SHORTCUT_TOOLTIP_ID = 1;
static HWND g_shortcutTooltip = nullptr;
static HWND g_shortcutTooltipButton = nullptr;

// 按钮文字装不下时才需要提示：按绘制时的可用宽度量一次，放不下就返回真。
static bool ShortcutLabelNeedsTooltip(HWND button)
{
    WCHAR text[128] = {};
    if (GetWindowTextW(button, text, static_cast<int>(_countof(text))) == 0)
        return false;

    RECT client = {};
    GetClientRect(button, &client);
    // 和 DrawShortcutCell 留给文字的区域保持一致：右侧让出关闭区。
    int availableWidth = client.right - SHORTCUT_DELETE_SIZE - 4;
    int availableHeight = client.bottom;
    if (availableWidth <= 0 || availableHeight <= 0)
        return false;

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0));
    HDC dc = GetDC(button);
    if (dc == nullptr)
        return false;
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;

    RECT measure = { 0, 0, availableWidth, availableHeight };
    DrawTextW(dc, text, -1, &measure, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);

    if (oldFont != nullptr) SelectObject(dc, oldFont);
    ReleaseDC(button, dc);

    return (measure.bottom - measure.top) > availableHeight ||
        (measure.right - measure.left) > availableWidth;
}

static void HideShortcutTooltip()
{
    if (g_shortcutTooltip == nullptr || g_shortcutTooltipButton == nullptr)
        return;

    TOOLINFOW tool = {};
    tool.cbSize = sizeof(tool);
    tool.hwnd = GetParent(g_shortcutTooltipButton);
    tool.uId = SHORTCUT_TOOLTIP_ID;
    SendMessageW(g_shortcutTooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    g_shortcutTooltipButton = nullptr;
}

// 提示里按一行显示完整组合，所以去掉按钮标签里的折行符。
static void ShowShortcutTooltip(HWND button)
{
    if (g_shortcutTooltip == nullptr)
        return;
    if (g_shortcutTooltipButton == button)
        return;
    HideShortcutTooltip();
    if (!ShortcutLabelNeedsTooltip(button))
        return;

    WCHAR text[128] = {};
    GetWindowTextW(button, text, static_cast<int>(_countof(text)));
    WCHAR* write = text;
    for (const WCHAR* read = text; *read != L'\0'; ++read)
        if (*read != L'\n') *write++ = *read;
    *write = L'\0';

    TOOLINFOW tool = {};
    tool.cbSize = sizeof(tool);
    tool.hwnd = GetParent(button);
    tool.uId = SHORTCUT_TOOLTIP_ID;
    tool.lpszText = text;
    SendMessageW(g_shortcutTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));

    // 默认贴在按钮下沿，下方放不下就翻到按钮上方。
    RECT rect = {};
    GetWindowRect(button, &rect);
    int x = rect.left;
    int y = rect.bottom + 2;

    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) != FALSE)
    {
        int estimatedWidth = static_cast<int>(wcslen(text)) * 8 + 16;
        if (x + estimatedWidth > info.rcWork.right) x = info.rcWork.right - estimatedWidth;
        if (x < info.rcWork.left) x = info.rcWork.left;
        if (y + 24 > info.rcWork.bottom) y = rect.top - 24;
    }

    SendMessageW(g_shortcutTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
    SendMessageW(g_shortcutTooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
    g_shortcutTooltipButton = button;
}

static void ApplyShortcutReorder(HWND hWnd, int from, int to);

// 把光标位置换算成条目下标，落在空白或面板外时贴到最近的一条。
static int HitTestShortcutIndex(HWND hWnd, POINT screen)
{
    if (g_shortcuts.empty())
        return -1;

    POINT client = screen;
    ScreenToClient(hWnd, &client);

    int columns = GetShortcutPanelColumns(hWnd);
    int columnStep = GetShortcutPanelCellWidth(hWnd, columns) + SHORTCUT_PANEL_GAP;
    int column = (client.x - SHORTCUT_PANEL_MARGIN) / columnStep;
    if (column < 0) column = 0;
    if (column >= columns) column = columns - 1;

    int row = (client.y - SHORTCUT_PANEL_MARGIN + g_shortcutScrollOffset) / GetShortcutPanelRowStep();
    if (row < 0) row = 0;

    int index = row * columns + column;
    int last = static_cast<int>(g_shortcuts.size()) - 1;
    if (index > last) index = last;
    return index;
}

static void UpdateShortcutDropHighlight(int dropIndex)
{
    for (size_t index = 0; index < g_shortcutButtons.size(); ++index)
    {
        if (g_shortcutButtons[index] == nullptr) continue;
        int state = SHORTCUT_HOVER_NONE;
        if (static_cast<int>(index) == dropIndex) state = SHORTCUT_HOVER_DROP;
        else if (static_cast<int>(index) == g_shortcutDragIndex) state = SHORTCUT_HOVER_PRESSED;
        SetShortcutButtonHoverState(g_shortcutButtons[index], state);
    }
}

static void FinishShortcutDrag()
{
    for (HWND button : g_shortcutButtons)
        if (button != nullptr) SetShortcutButtonHoverState(button, SHORTCUT_HOVER_NONE);
    g_shortcutDragIndex = -1;
    g_shortcutDropIndex = -1;
    g_shortcutDragging = false;
}

// 按下时只记录起点，移动超过系统拖动阈值才算拖动，单击行为保持不变。
static void BeginShortcutDrag(HWND hWnd)
{
    size_t index = static_cast<size_t>(GetDlgCtrlID(hWnd) - ID_SHORTCUT_SEND_BASE);
    if (index >= g_shortcuts.size())
        return;

    GetCursorPos(&g_shortcutDragOrigin);
    g_shortcutDragIndex = static_cast<int>(index);
    g_shortcutDropIndex = static_cast<int>(index);
    g_shortcutDragging = false;
    SetCapture(hWnd);
    SetShortcutButtonHoverState(hWnd, SHORTCUT_HOVER_PRESSED);
}

// 返回真表示消息已经被拖动逻辑接管，不再做悬停处理。
static bool UpdateShortcutDrag(HWND hWnd)
{
    if (g_shortcutDragIndex < 0)
        return false;

    POINT cursor = {};
    GetCursorPos(&cursor);
    if (!g_shortcutDragging)
    {
        if (abs(cursor.x - g_shortcutDragOrigin.x) < GetSystemMetrics(SM_CXDRAG) &&
            abs(cursor.y - g_shortcutDragOrigin.y) < GetSystemMetrics(SM_CYDRAG))
            return true;
        g_shortcutDragging = true;
    }

    int target = HitTestShortcutIndex(GetParent(hWnd), cursor);
    if (target != g_shortcutDropIndex)
    {
        g_shortcutDropIndex = target;
        UpdateShortcutDropHighlight(target);
    }
    return true;
}

// 记录是否已经请求过 TME_LEAVE，避免每次移动都重新登记。
static LRESULT CALLBACK ShortcutButtonSubclass(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR refData)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(refData);

    switch (message)
    {
    case WM_ERASEBKGND:
        // 自绘会填满整个客户区，先擦成白色再画只会多闪一下。
        return 1;
    case WM_MOUSEMOVE:
        if (UpdateShortcutDrag(hWnd))
            break;
        if (GetWindowLongPtrW(hWnd, GWLP_USERDATA) == 0)
        {
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hWnd;
            if (TrackMouseEvent(&track) != FALSE)
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, 1);
        }
        SetShortcutButtonHoverState(hWnd, IsShortcutCloseZone(hWnd, lParam) ? SHORTCUT_HOVER_DELETE
            : SHORTCUT_HOVER_CELL);
        // 关闭区上不提示；文字装不下的按钮才显示完整组合。
        if (IsShortcutCloseZone(hWnd, lParam)) HideShortcutTooltip();
        else ShowShortcutTooltip(hWnd);
        break;
    case WM_MOUSELEAVE:
        if (g_shortcutDragIndex >= 0)
            break;
        HideShortcutTooltip();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        SetShortcutButtonHoverState(hWnd, SHORTCUT_HOVER_NONE);
        break;
    case WM_LBUTTONDOWN:
        HideShortcutTooltip();
        // 关闭区自己处理，不能交给按钮，否则会触发发送快捷键。
        if (IsShortcutCloseZone(hWnd, lParam))
        {
            SetCapture(hWnd);
            return 0;
        }
        BeginShortcutDrag(hWnd);
        return 0;
    case WM_CAPTURECHANGED:
        if (g_shortcutDragIndex >= 0)
            FinishShortcutDrag();
        break;
    case WM_LBUTTONUP:
        if (GetCapture() == hWnd)
        {
            int dragIndex = g_shortcutDragIndex;
            bool dragging = g_shortcutDragging;
            int dropIndex = g_shortcutDropIndex;
            bool overClose = IsShortcutCloseZone(hWnd, lParam);
            // ReleaseCapture 会同步触发 WM_CAPTURECHANGED，状态在那里统一清理。
            ReleaseCapture();
            if (dragIndex >= 0)
            {
                // 拖到别的位置就调整顺序，只是点了一下就还按触发处理。
                if (dragging)
                    PostMessageW(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(ID_SHORTCUT_REORDER, BN_CLICKED),
                        MAKELPARAM(static_cast<WORD>(dragIndex), static_cast<WORD>(dropIndex)));
                else
                    PostMessageW(GetParent(hWnd), WM_COMMAND,
                        MAKEWPARAM(ID_SHORTCUT_SEND_BASE + dragIndex, BN_CLICKED),
                        reinterpret_cast<LPARAM>(hWnd));
                return 0;
            }
            if (overClose)
            {
                // 延后到消息循环里删除，避免在按钮自己的消息处理中销毁它。
                PostMessageW(GetParent(hWnd), WM_COMMAND,
                    MAKEWPARAM(ID_SHORTCUT_DELETE_BASE + GetDlgCtrlID(hWnd) - ID_SHORTCUT_SEND_BASE, BN_CLICKED),
                    reinterpret_cast<LPARAM>(hWnd));
            }
            // 在关闭区按下过就吃掉抬起消息，避免按钮把它当成一次点击。
            return 0;
        }
        break;
    case WM_NCDESTROY:
        if (g_shortcutDragIndex >= 0)
            FinishShortcutDrag();
        RemoveWindowSubclass(hWnd, ShortcutButtonSubclass, subclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

static void RebuildShortcutPanel(HWND hWnd)
{
    if (hWnd == nullptr)
        return;

    // 销毁和重建上百个按钮期间保持画面不动，最后一次性刷新。
    BeginShortcutPanelUpdate(hWnd);

    // 提示可能还对着即将销毁的按钮，先收起来，避免留下失效的窗口句柄。
    HideShortcutTooltip();

    for (HWND control : g_shortcutButtons)
        if (control != nullptr) DestroyWindow(control);
    g_shortcutButtons.clear();
    g_shortcutButtonHover.assign(g_shortcuts.size(), SHORTCUT_HOVER_NONE);

    for (size_t index = 0; index < g_shortcuts.size(); ++index)
    {
        std::wstring label = GetShortcutButtonLabel(g_shortcuts[index]);
        // 先建为隐藏，交给布局决定显示哪些，避免创建瞬间闪出未定位的按钮。
        HWND button = CreateWindowW(L"BUTTON", label.c_str(), WS_CHILD | BS_OWNERDRAW,
            0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SHORTCUT_SEND_BASE + index)), hInst, nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SetWindowSubclass(button, ShortcutButtonSubclass, 0, 0);
        g_shortcutButtons.push_back(button);
    }

    UpdateShortcutPanelScroll(hWnd);
    LayoutShortcutPanel(hWnd);

    EndShortcutPanelUpdate(hWnd);
}

static bool SaveShortcuts(std::wstring& errorMessage)
{
    std::wstring configPath = GetConfigurationPath(L"shortcuts.ini");
    if (configPath.empty())
    {
        errorMessage = L"The shortcuts configuration path could not be resolved.";
        return false;
    }

    std::wstring text = L"; ProcQuit shortcut configuration\r\n";
    // 按当前顺序写出，分类变化时才写一次段头。拖动调整顺序后同一个分类
    // 可能出现多次，读取端按行解析，因此顺序可以完整保留。
    std::wstring currentCategory;
    bool hasCategory = false;
    for (const ShortcutItem& item : g_shortcuts)
    {
        if (!hasCategory || currentCategory != item.category)
        {
            currentCategory = item.category;
            hasCategory = true;
            text += L"\r\n[";
            text += currentCategory;
            text += L"]\r\n";
        }
        text += item.name;
        text += L'=';
        text += item.shortcut;
        text += L"\r\n";
    }

    if (text.size() > 32767)
    {
        errorMessage = L"The shortcuts configuration is too large to save.";
        return false;
    }

    // 以 UTF-16LE + BOM 写入，保证中文条目名在读写时保持一致。
    std::wstring temporaryPath = configPath + L".tmp." + std::to_wstring(GetCurrentProcessId());
    HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        DWORD openError = GetLastError();
        errorMessage = L"The shortcuts configuration could not be saved. Check write access to shortcuts.ini.";
        AppendDiagnosticLog(L"Unable to open the temporary shortcuts file for writing, error=" + std::to_wstring(openError));
        return false;
    }

    const unsigned char bom[2] = { 0xFF, 0xFE };
    DWORD bomWritten = 0;
    DWORD textWritten = 0;
    DWORD textBytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    BOOL saved = WriteFile(file, bom, sizeof(bom), &bomWritten, nullptr);
    if (saved != FALSE && bomWritten == sizeof(bom))
        saved = WriteFile(file, text.c_str(), textBytes, &textWritten, nullptr);
    if (saved != FALSE && textWritten == textBytes)
        saved = FlushFileBuffers(file);
    DWORD writeError = ERROR_SUCCESS;
    if (saved == FALSE)
        writeError = GetLastError();
    else if (bomWritten != sizeof(bom) || textWritten != textBytes)
        writeError = ERROR_WRITE_FAULT;
    CloseHandle(file);

    if (saved == FALSE || bomWritten != sizeof(bom) || textWritten != textBytes)
    {
        DeleteFileW(temporaryPath.c_str());
        errorMessage = L"The shortcuts configuration could not be saved. Check write access to shortcuts.ini.";
        AppendDiagnosticLog(L"Unable to save shortcuts.ini, error=" + std::to_wstring(writeError));
        return false;
    }

    if (MoveFileExW(temporaryPath.c_str(), configPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
    {
        DWORD replaceError = GetLastError();
        DeleteFileW(temporaryPath.c_str());
        errorMessage = L"The shortcuts configuration could not be replaced. Check write access to shortcuts.ini.";
        AppendDiagnosticLog(L"Unable to replace shortcuts.ini, error=" + std::to_wstring(replaceError));
        return false;
    }
    return true;
}

static void ClearShortcuts(HWND hWnd)
{
    HWND owner = hWnd;
    if (MessageBoxW(owner, L"Clear all shortcuts? Custom changes will be lost.", L"Clear Shortcuts",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    std::vector<ShortcutItem> previous = g_shortcuts;
    g_shortcuts.clear();
    std::wstring errorMessage;
    if (!SaveShortcuts(errorMessage))
    {
        g_shortcuts = std::move(previous);
        MessageBoxW(owner, errorMessage.c_str(), L"Shortcuts", MB_OK | MB_ICONERROR);
        return;
    }

    AppendDiagnosticLog(L"All shortcuts cleared");
    RebuildShortcutPanel(hWnd);
}

static void ResetShortcuts(HWND hWnd)
{
    HWND owner = hWnd;
    if (MessageBoxW(owner, L"Reset all shortcuts to the built-in defaults?", L"Reset Shortcuts",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    std::vector<ShortcutItem> defaults;
    if (!LoadDefaultShortcuts(defaults))
    {
        MessageBoxW(owner, L"The built-in shortcut defaults could not be loaded.", L"Shortcuts",
            MB_OK | MB_ICONERROR);
        AppendDiagnosticLog(L"Unable to load the built-in shortcut defaults");
        return;
    }

    std::vector<ShortcutItem> previous = g_shortcuts;
    g_shortcuts = std::move(defaults);
    std::wstring errorMessage;
    if (!SaveShortcuts(errorMessage))
    {
        g_shortcuts = std::move(previous);
        MessageBoxW(owner, errorMessage.c_str(), L"Shortcuts", MB_OK | MB_ICONERROR);
        return;
    }

    AppendDiagnosticLog(L"Shortcuts reset to built-in defaults");
    RebuildShortcutPanel(hWnd);
}

static bool AddShortcutItem(const std::wstring& name, const std::wstring& keys, std::wstring& errorMessage)
{
    std::wstring trimmedName = Trim(name);
    if (trimmedName.empty())
    {
        errorMessage = L"Enter a shortcut name.";
        return false;
    }
    if (trimmedName.find_first_of(L"=[]\r\n") != std::wstring::npos)
    {
        errorMessage = L"The shortcut name cannot contain the characters = [ ] or line breaks.";
        return false;
    }
    if (g_shortcuts.size() >= MAX_SHORTCUTS)
    {
        errorMessage = L"The maximum number of shortcuts has been reached.";
        return false;
    }

    std::wstring loweredName = ToLower(trimmedName);
    for (const ShortcutItem& existing : g_shortcuts)
    {
        if (existing.category != SHORTCUT_CUSTOM_CATEGORY) continue;
        if (ToLower(existing.name) == loweredName)
        {
            errorMessage = L"A shortcut with this name already exists.";
            return false;
        }
    }

    ShortcutItem item = {};
    item.category = SHORTCUT_CUSTOM_CATEGORY;
    item.name = trimmedName;
    item.shortcut = Trim(keys);
    if (!ParseShortcut(item.shortcut, item))
    {
        errorMessage = L"Enter a valid key combination, for example Ctrl+Shift+K.";
        return false;
    }

    for (const ShortcutItem& existing : g_shortcuts)
    {
        if (existing.category != SHORTCUT_CUSTOM_CATEGORY) continue;
        if (existing.virtualKey == item.virtualKey && existing.control == item.control &&
            existing.alt == item.alt && existing.shift == item.shift && existing.windows == item.windows)
        {
            errorMessage = L"This key combination is already assigned to another shortcut.";
            return false;
        }
    }

    g_shortcuts.push_back(item);
    if (!SaveShortcuts(errorMessage))
    {
        g_shortcuts.pop_back();
        return false;
    }

    AppendDiagnosticLog(L"Shortcut added: " + item.name);
    RebuildShortcutPanel(g_shortcutWindow);
    return true;
}

static void DeleteShortcut(HWND hWnd, size_t index)
{
    if (index >= g_shortcuts.size())
        return;

    std::vector<ShortcutItem> previous = g_shortcuts;
    g_shortcuts.erase(g_shortcuts.begin() + static_cast<std::vector<ShortcutItem>::difference_type>(index));

    std::wstring errorMessage;
    if (!SaveShortcuts(errorMessage))
    {
        g_shortcuts = previous;
        HWND owner = GetWindow(hWnd, GW_OWNER);
        if (owner == nullptr) owner = hWnd;
        MessageBoxW(owner, errorMessage.c_str(), L"Shortcuts", MB_OK | MB_ICONERROR);
        return;
    }

    AppendDiagnosticLog(L"Shortcut deleted: " + previous[index].name);
    RebuildShortcutPanel(hWnd);
}

// 把 from 处的条目移动到 to 处；保存失败时回滚，面板顺序保持不变。
static void ApplyShortcutReorder(HWND hWnd, int from, int to)
{
    if (hWnd == nullptr || from < 0 || to < 0 || from == to)
        return;
    if (from >= static_cast<int>(g_shortcuts.size()) || to >= static_cast<int>(g_shortcuts.size()))
        return;

    std::vector<ShortcutItem> previous = g_shortcuts;
    ShortcutItem item = g_shortcuts[static_cast<size_t>(from)];
    g_shortcuts.erase(g_shortcuts.begin() + from);
    g_shortcuts.insert(g_shortcuts.begin() + to, item);

    std::wstring errorMessage;
    if (!SaveShortcuts(errorMessage))
    {
        g_shortcuts = previous;
        HWND owner = GetWindow(hWnd, GW_OWNER);
        if (owner == nullptr) owner = hWnd;
        MessageBoxW(owner, errorMessage.c_str(), L"Shortcuts", MB_OK | MB_ICONERROR);
        return;
    }

    AppendDiagnosticLog(L"Shortcut reordered: " + item.name);
    RebuildShortcutPanel(hWnd);
}

static void ShowAddShortcutDialog(HWND hWnd);

// 按键录制：把用户在输入框里直接按下的组合翻译成 ParseShortcut 认识的写法。

static bool IsShortcutModifierKey(WORD key)
{
    return key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
        key == VK_MENU || key == VK_LMENU || key == VK_RMENU ||
        key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
        key == VK_LWIN || key == VK_RWIN;
}

static bool IsShortcutModifierDown()
{
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0 || (GetKeyState(VK_MENU) & 0x8000) != 0 ||
        (GetKeyState(VK_SHIFT) & 0x8000) != 0 || (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
        (GetKeyState(VK_RWIN) & 0x8000) != 0;
}

static std::wstring GetShortcutKeyName(WORD key)
{
    if ((key >= L'A' && key <= L'Z') || (key >= L'0' && key <= L'9'))
        return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= VK_F1 && key <= VK_F24)
        return L"F" + std::to_wstring(key - VK_F1 + 1);
    switch (key)
    {
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_TAB: return L"Tab";
    case VK_ESCAPE: return L"Esc";
    case VK_DELETE: return L"Delete";
    case VK_BACK: return L"Backspace";
    case VK_INSERT: return L"Insert";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_SNAPSHOT: return L"PrintScreen";
    case VK_PAUSE: return L"Pause";
    case VK_NUMLOCK: return L"NumLock";
    case VK_OEM_PLUS: return L"Plus";
    case VK_OEM_MINUS: return L"Minus";
    case VK_OEM_PERIOD: return L"Period";
    case VK_OEM_COMMA: return L"Comma";
    case VK_OEM_1: return L"Semicolon";
    case VK_OEM_2: return L"Slash";
    }
    return std::wstring();
}

// key 为 0 表示当前只按下了修饰键，此时末尾不留加号。
static std::wstring BuildShortcutCaptureText(WORD key)
{
    std::wstring text;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) text += L"Ctrl+";
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) text += L"Alt+";
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) text += L"Shift+";
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) text += L"Win+";
    if (key != 0) text += GetShortcutKeyName(key);
    else if (!text.empty()) text.erase(text.size() - 1);
    return text;
}

// 没有修饰键的普通字符键交给编辑框正常输入，其余按键由录制逻辑接管，
// 这样既可以直接按键录制，也可以手工输入 Win 这类会被系统抢先响应的组合。
static bool ShouldCaptureShortcutKey(WORD key)
{
    if (IsShortcutModifierKey(key)) return true;
    if (IsShortcutModifierDown()) return true;
    if (key == VK_BACK) return false;
    if (key == VK_SPACE || (key >= L'0' && key <= L'9') || (key >= L'A' && key <= L'Z')) return false;
    return true;
}

static LRESULT CALLBACK ShortcutKeysSubclass(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR refData)
{
    UNREFERENCED_PARAMETER(refData);
    switch (message)
    {
    case WM_GETDLGCODE:
        if (lParam != 0)
        {
            const MSG* msg = reinterpret_cast<const MSG*>(lParam);
            if ((msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) &&
                !ShouldCaptureShortcutKey(static_cast<WORD>(msg->wParam)))
                break;
        }
        return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        {
            WORD key = static_cast<WORD>(wParam);
            if (!ShouldCaptureShortcutKey(key))
                break;
            if (IsShortcutModifierKey(key))
            {
                SetWindowTextW(hWnd, BuildShortcutCaptureText(0).c_str());
                return 0;
            }
            if (GetShortcutKeyName(key).empty())
                return 0;
            SetWindowTextW(hWnd, BuildShortcutCaptureText(key).c_str());
            return 0;
        }
    case WM_CHAR:
    case WM_SYSCHAR:
        // Ctrl+字母 会额外产生一个控制字符，丢掉它避免混进组合文本。
        if (IsShortcutModifierDown())
            return 0;
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, ShortcutKeysSubclass, subclassId);
        break;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

INT_PTR CALLBACK AddShortcutDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        {
            HWND keys = GetDlgItem(hDlg, IDC_SHORTCUT_KEYS);
            if (keys != nullptr) SetWindowSubclass(keys, ShortcutKeysSubclass, 0, 0);
            SetFocus(keys);
        }
        return static_cast<INT_PTR>(FALSE);
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            WCHAR keys[128] = {};
            GetDlgItemTextW(hDlg, IDC_SHORTCUT_KEYS, keys, static_cast<int>(_countof(keys)));
            std::wstring errorMessage;
            // 条目不再要求名称，面板上也只显示按键组合，直接用组合文本作为配置里的名字。
            if (!AddShortcutItem(keys, keys, errorMessage))
            {
                MessageBoxW(hDlg, errorMessage.c_str(), L"Add Shortcut", MB_OK | MB_ICONERROR);
                return static_cast<INT_PTR>(TRUE);
            }
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

static void ShowAddShortcutDialog(HWND hWnd)
{
    HWND owner = GetWindow(hWnd, GW_OWNER);
    if (owner == nullptr) owner = hWnd;
    if (DialogBox(hInst, MAKEINTRESOURCE(IDD_ADD_SHORTCUT), owner, AddShortcutDialog) == -1)
        AppendDiagnosticLog(L"Unable to open the add shortcut dialog, error=" + std::to_wstring(GetLastError()));
}

static void DrawMainButton(const DRAWITEMSTRUCT* item);
static HWND CreateMainButton(HWND parent, const wchar_t* text, int id);

static LRESULT CALLBACK ShortcutPanelProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        g_shortcutAddButton = CreateWindowW(L"BUTTON", L"Add Shortcut", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SHORTCUT_ADD)), hInst, nullptr);
        g_shortcutClearButton = CreateWindowW(L"BUTTON", L"Clear All", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SHORTCUT_CLEAR)), hInst, nullptr);
        g_shortcutResetButton = CreateWindowW(L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SHORTCUT_RESET)), hInst, nullptr);
        g_shortcutCloseButton = CreateMainButton(hWnd, L"×", ID_SHORTCUT_CLOSE);
        g_shortcutMinimizeButton = CreateMainButton(hWnd, L"—", ID_SHORTCUT_MINIMIZE);
        g_shortcutHintText = CreateWindowW(L"STATIC", L"Drag empty space to move, drag an edge to resize.",
            WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, hWnd, nullptr, hInst, nullptr);
        SendMessageW(g_shortcutAddButton, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageW(g_shortcutClearButton, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageW(g_shortcutResetButton, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageW(g_shortcutHintText, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        ShowScrollBar(hWnd, SB_VERT, FALSE);
        g_shortcutScrollVisible = false;
        // 只建一个提示窗口，用跟踪模式按按钮手动激活，不参与按钮自身的子类化。
        g_shortcutTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hWnd, nullptr, hInst, nullptr);
        if (g_shortcutTooltip != nullptr)
        {
            SetWindowPos(g_shortcutTooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            TOOLINFOW tool = {};
            tool.cbSize = sizeof(tool);
            tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            tool.hwnd = hWnd;
            tool.uId = SHORTCUT_TOOLTIP_ID;
            tool.lpszText = const_cast<LPWSTR>(L"");
            SendMessageW(g_shortcutTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
        RebuildShortcutPanel(hWnd);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_GETMINMAXINFO:
        {
            MINMAXINFO* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            int width = 0;
            int height = 0;
            AdjustShortcutPanelWindowRect(GetShortcutPanelMinClientWidth(), GetShortcutPanelMinClientHeight(),
                width, height);
            limits->ptMinTrackSize.x = width;
            limits->ptMinTrackSize.y = height;
        }
        return 0;
    case WM_NCHITTEST:
        {
            LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);
            if (hit == HTCLIENT)
            {
                POINT point = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
                ScreenToClient(hWnd, &point);
                HWND child = ChildWindowFromPoint(hWnd, point);
                if (point.y >= 0 && point.y < SHORTCUT_PANEL_TITLE_BAR_HEIGHT &&
                    (child == nullptr || child == hWnd))
                    return HTCAPTION;
            }
            return hit;
        }
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT client = {};
            GetClientRect(hWnd, &client);
            RECT titleRect = { SHORTCUT_PANEL_MARGIN, 0, client.right - SHORTCUT_PANEL_MARGIN, SHORTCUT_PANEL_TITLE_BAR_HEIGHT };
            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ oldFont = SelectObject(hdc, font);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(64, 64, 64));
            DrawTextW(hdc, L"Shortcuts", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldFont);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(224, 224, 224));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            MoveToEx(hdc, 0, SHORTCUT_PANEL_TITLE_BAR_HEIGHT - 1, nullptr);
            LineTo(hdc, client.right, SHORTCUT_PANEL_TITLE_BAR_HEIGHT - 1);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            EndPaint(hWnd, &ps);
        }
        return 0;
    case WM_SIZE:
        RefreshShortcutPanel(hWnd);
        return 0;
    case WM_DRAWITEM:
        {
            const DRAWITEMSTRUCT* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item == nullptr || item->CtlType != ODT_BUTTON)
                break;
            int controlId = static_cast<int>(item->CtlID);
            if (controlId == ID_SHORTCUT_MINIMIZE || controlId == ID_SHORTCUT_CLOSE)
            {
                DrawMainButton(item);
                return TRUE;
            }
            int hoverState = SHORTCUT_HOVER_NONE;
            if (controlId >= ID_SHORTCUT_SEND_BASE &&
                static_cast<size_t>(controlId - ID_SHORTCUT_SEND_BASE) < g_shortcutButtonHover.size())
                hoverState = g_shortcutButtonHover[controlId - ID_SHORTCUT_SEND_BASE];
            DrawShortcutCell(item, hoverState);
            return TRUE;
        }
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(120, 120, 120));
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_VSCROLL:
        {
            SCROLLINFO info = {};
            info.cbSize = sizeof(info);
            info.fMask = SIF_ALL;
            GetScrollInfo(hWnd, SB_VERT, &info);
            int position = info.nPos;
            switch (LOWORD(wParam))
            {
            case SB_LINEUP: position -= GetShortcutPanelRowStep(); break;
            case SB_LINEDOWN: position += GetShortcutPanelRowStep(); break;
            case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: position = info.nTrackPos; break;
            default: return 0;
            }
            int maximum = info.nMax - static_cast<int>(info.nPage) + 1;
            if (maximum < 0) maximum = 0;
            if (position < 0) position = 0;
            if (position > maximum) position = maximum;
            if (position != g_shortcutScrollOffset)
            {
                g_shortcutScrollOffset = position;
                SetScrollPos(hWnd, SB_VERT, position, TRUE);
                LayoutShortcutPanel(hWnd);
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        SendMessageW(hWnd, WM_VSCROLL, MAKEWPARAM(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? SB_LINEUP : SB_LINEDOWN, 0), 0);
        return 0;
    case WM_LBUTTONDOWN:
        if (!g_shortcutPanelDragging && GetCursorPos(&g_shortcutPanelDragCursor))
        {
            g_shortcutPanelDragging = true;
            GetWindowRect(hWnd, &g_shortcutPanelDragBounds);
            SetCapture(hWnd);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (g_shortcutPanelDragging)
        {
            POINT cursor = {};
            if (GetCursorPos(&cursor))
                SetWindowPos(hWnd, nullptr,
                    g_shortcutPanelDragBounds.left + cursor.x - g_shortcutPanelDragCursor.x,
                    g_shortcutPanelDragBounds.top + cursor.y - g_shortcutPanelDragCursor.y,
                    0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_shortcutPanelDragging)
        {
            g_shortcutPanelDragging = false;
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_shortcutPanelDragging = false;
        return 0;
    case WM_COMMAND:
        {
            int commandId = LOWORD(wParam);
            if (commandId == ID_SHORTCUT_CLOSE)
            {
                HideShortcutTooltip();
                ShowWindow(hWnd, SW_HIDE);
                return 0;
            }
            if (commandId == ID_SHORTCUT_MINIMIZE)
            {
                ShowWindow(hWnd, SW_MINIMIZE);
                return 0;
            }
            if (commandId == ID_SHORTCUT_ADD)
            {
                ShowAddShortcutDialog(hWnd);
                return 0;
            }
            if (commandId == ID_SHORTCUT_CLEAR)
            {
                ClearShortcuts(hWnd);
                return 0;
            }
            if (commandId == ID_SHORTCUT_RESET)
            {
                ResetShortcuts(hWnd);
                return 0;
            }
            if (commandId == ID_SHORTCUT_REORDER)
            {
                // 起止下标通过 lParam 传递，避免和按钮通知里作为控件句柄的 lParam 混淆。
                ApplyShortcutReorder(hWnd, static_cast<int>(LOWORD(lParam)), static_cast<int>(HIWORD(lParam)));
                return 0;
            }
            if (commandId >= ID_SHORTCUT_SEND_BASE && commandId < ID_SHORTCUT_DELETE_BASE &&
                static_cast<size_t>(commandId - ID_SHORTCUT_SEND_BASE) < g_shortcuts.size())
            {
                SendShortcut(g_shortcuts[commandId - ID_SHORTCUT_SEND_BASE]);
                return 0;
            }
            if (commandId >= ID_SHORTCUT_DELETE_BASE &&
                static_cast<size_t>(commandId - ID_SHORTCUT_DELETE_BASE) < g_shortcuts.size())
            {
                DeleteShortcut(hWnd, static_cast<size_t>(commandId - ID_SHORTCUT_DELETE_BASE));
                return 0;
            }
        }
        break;
    case WM_CLOSE:
        HideShortcutTooltip();
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        g_shortcutAddButton = nullptr;
        g_shortcutClearButton = nullptr;
        g_shortcutResetButton = nullptr;
        g_shortcutCloseButton = nullptr;
        g_shortcutMinimizeButton = nullptr;
        g_shortcutHintText = nullptr;
        g_shortcutButtons.clear();
        g_shortcutButtonHover.clear();
        g_shortcutPanelDragging = false;
        g_shortcutScrollOffset = 0;
        g_shortcutScrollVisible = false;
        g_shortcutDragIndex = -1;
        g_shortcutDropIndex = -1;
        g_shortcutDragging = false;
        g_shortcutTooltip = nullptr;
        g_shortcutTooltipButton = nullptr;
        g_shortcutWindow = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

static void RegisterShortcutPanelClass(HINSTANCE instance)
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    // 不用 CS_HREDRAW/CS_VREDRAW：尺寸变化时只需要重画新增的空白区域，
    // 否则每次拖动边框都会整窗重画，子控件多时非常闪。
    wcex.style = 0;
    wcex.lpfnWndProc = ShortcutPanelProc;
    wcex.hInstance = instance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wcex.lpszClassName = SHORTCUT_PANEL_CLASS;
    RegisterClassExW(&wcex);
}

static void CreateShortcutPanel()
{
    if (g_shortcutWindow != nullptr)
        return;

    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);

    int clientWidth = GetShortcutPanelInitialClientWidth();
    int maxClientWidth = workArea.right - workArea.left - 80;
    if (clientWidth > maxClientWidth) clientWidth = maxClientWidth;
    if (clientWidth < GetShortcutPanelMinClientWidth()) clientWidth = GetShortcutPanelMinClientWidth();

    int clientHeight = GetShortcutPanelMinClientHeight();
    int maxClientHeight = workArea.bottom - workArea.top - 120;
    if (maxClientHeight < clientHeight) maxClientHeight = clientHeight;

    int windowWidth = 0;
    int windowHeight = 0;
    AdjustShortcutPanelWindowRect(clientWidth, clientHeight, windowWidth, windowHeight);
    g_shortcutWindow = CreateWindowExW(SHORTCUT_PANEL_EX_STYLE, SHORTCUT_PANEL_CLASS, L"Shortcuts",
        SHORTCUT_PANEL_STYLE, workArea.left + 40, workArea.top + 40, windowWidth, windowHeight, nullptr,
        nullptr, hInst, nullptr);
    if (g_shortcutWindow == nullptr)
    {
        AppendDiagnosticLog(L"Unable to create the shortcut panel, error=" + std::to_wstring(GetLastError()));
        return;
    }

    // 按目标宽度量一次内容高度；超出可用桌面区域时改为滚动查看，避免窗口占满屏幕。
    int contentHeight = WalkShortcutPanel(g_shortcutWindow, 0, false) + SHORTCUT_PANEL_MARGIN + SHORTCUT_PANEL_FOOTER;
    if (contentHeight > maxClientHeight) contentHeight = maxClientHeight;
    if (contentHeight > clientHeight)
    {
        AdjustShortcutPanelWindowRect(clientWidth, contentHeight, windowWidth, windowHeight);
        SetWindowPos(g_shortcutWindow, nullptr, workArea.left + 40, workArea.top + 40, windowWidth, windowHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    RefreshShortcutPanel(g_shortcutWindow);
}

static void ShowShortcutPanel()
{
    if (g_shortcutWindow == nullptr)
        CreateShortcutPanel();
    if (g_shortcutWindow == nullptr)
        return;
    ShowWindow(g_shortcutWindow, SW_SHOWNOACTIVATE);
    SetWindowPos(g_shortcutWindow, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    INITCOMMONCONTROLSEX controls = { sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&controls);

    // TODO: 在此处放置代码。

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PROCQUIT, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    RegisterShortcutPanelClass(hInstance);

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

    wcex.style          = 0;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PROCQUIT));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wcex.lpszMenuName   = nullptr;
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

   RECT workArea = {};
   SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
   int width = 960;
   int height = 620;
   if (width > workArea.right - workArea.left - 80) width = workArea.right - workArea.left - 80;
   if (height > workArea.bottom - workArea.top - 80) height = workArea.bottom - workArea.top - 80;
   int x = workArea.left + ((workArea.right - workArea.left) - width) / 2;
   int y = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;

   // 无标题栏：去掉 WS_CAPTION，用 WS_THICKFRAME 保留缩放，WS_CLIPCHILDREN 减少闪烁。
   // WS_EX_APPWINDOW 让窗口继续出现在任务栏，最小化到任务栏也能正常工作。
   HWND hWnd = CreateWindowExW(WS_EX_APPWINDOW, szWindowClass, szTitle,
      WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN,
      x, y, width, height, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

static void ShowMainWindow(HWND hWnd)
{
    ShowWindow(hWnd, SW_RESTORE);
    SetForegroundWindow(hWnd);
}

static void HideMainWindow(HWND hWnd)
{
    ShowWindow(hWnd, SW_HIDE);
}

static void RemoveTrayIcon()
{
    if (g_trayIcon.cbSize != 0)
    {
        Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
        g_trayIcon = {};
    }
}

static bool AddTrayIcon(HWND hWnd)
{
    g_trayIcon = {};
    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = hWnd;
    g_trayIcon.uID = TRAY_UID;
    g_trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIcon.uCallbackMessage = WM_TRAYICON;
    g_trayIcon.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_SMALL));
    wcscpy_s(g_trayIcon.szTip, szTitle);
    if (!Shell_NotifyIconW(NIM_ADD, &g_trayIcon))
    {
        g_trayIcon = {};
        return false;
    }
    return true;
}

static void ShowTrayMenu(HWND hWnd)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
        return;

    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"显示窗口");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出程序");

    POINT cursor = {};
    GetCursorPos(&cursor);
    SetForegroundWindow(hWnd);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0, hWnd, nullptr);
    DestroyMenu(menu);
    if (command == ID_TRAY_SHOW)
        ShowMainWindow(hWnd);
    else if (command == ID_TRAY_EXIT)
        DestroyWindow(hWnd);
}

// 主窗口按钮自绘：白底细边框，悬停变浅蓝、按下变深蓝，与快捷键浮窗一致。
static void DrawMainButton(const DRAWITEMSTRUCT* item)
{
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    bool hovered = GetWindowLongPtrW(item->hwndItem, GWLP_USERDATA) != 0;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;

    COLORREF fill = RGB(255, 255, 255);
    if (pressed) fill = RGB(214, 232, 250);
    else if (hovered) fill = RGB(240, 247, 254);

    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);

    COLORREF border = (pressed || hovered) ? RGB(0, 120, 212) : RGB(200, 200, 200);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    WCHAR text[128] = {};
    GetWindowTextW(item->hwndItem, text, static_cast<int>(_countof(text)));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(32, 32, 32));
    DrawTextW(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont != nullptr) SelectObject(dc, oldFont);
}

// 主窗口按钮的悬停跟踪；GWLP_USERDATA 为 1 表示鼠标正悬停在上方。
static LRESULT CALLBACK MainButtonSubclass(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR refData)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(refData);
    switch (message)
    {
    case WM_ERASEBKGND:
        // 自绘会填满整个客户区，先擦成默认底色再画只会闪一下。
        return 1;
    case WM_MOUSEMOVE:
        if (GetWindowLongPtrW(hWnd, GWLP_USERDATA) == 0)
        {
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hWnd;
            if (TrackMouseEvent(&track) != FALSE)
            {
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, 1);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        break;
    case WM_MOUSELEAVE:
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, MainButtonSubclass, subclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

static HWND CreateMainButton(HWND parent, const wchar_t* text, int id)
{
    HWND button = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 1, 1, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SetWindowSubclass(button, MainButtonSubclass, 0, 0);
    return button;
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
        g_refreshButton = CreateMainButton(hWnd, L"Refresh", ID_REFRESH);
        g_exitButton = CreateMainButton(hWnd, L"Request Exit", ID_REQUEST_EXIT);
        g_terminateButton = CreateMainButton(hWnd, L"Force Terminate", ID_TERMINATE_PROCESS);
        g_taskbarOnlyCheck = CreateWindowW(L"BUTTON", L"Taskbar processes only", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TASKBAR_ONLY)), hInst, nullptr);
        SendMessageW(g_taskbarOnlyCheck, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        g_processSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROCESS_SEARCH)), hInst, nullptr);
        SendMessageW(g_processSearch, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageW(g_processSearch, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search processes..."));
        g_taskbarMenuButton = CreateMainButton(hWnd, L"Right-click Taskbar", ID_TASKBAR_MENU);
        g_copyLogsButton = CreateMainButton(hWnd, L"Copy Logs", ID_COPY_LOGS);
        g_shortcutPanelButton = CreateMainButton(hWnd, L"Shortcuts", ID_SHORTCUT_PANEL_BUTTON);
        g_settingsButton = CreateMainButton(hWnd, L"Settings", ID_MAIN_SETTINGS);
        g_programExitButton = CreateMainButton(hWnd, L"Exit Program", ID_MAIN_EXIT_PROGRAM);
        g_mainMinimizeButton = CreateMainButton(hWnd, L"—", ID_MAIN_MINIMIZE);
        g_mainCloseButton = CreateMainButton(hWnd, L"×", ID_MAIN_CLOSE);
        g_processList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
            0, 0, 1, 1, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROCESS_LIST)), hInst, nullptr);
        ListView_SetExtendedListViewStyle(g_processList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);
        LVCOLUMNW column = { LVCF_TEXT | LVCF_WIDTH, 0, 230, const_cast<LPWSTR>(L"Name") };
        ListView_InsertColumn(g_processList, 0, &column);
        column.cx = 90; column.pszText = const_cast<LPWSTR>(L"PID");
        ListView_InsertColumn(g_processList, 1, &column);
        column.cx = 280; column.pszText = const_cast<LPWSTR>(L"Window title");
        ListView_InsertColumn(g_processList, 2, &column);
        column.cx = 120; column.pszText = const_cast<LPWSTR>(L"Exit availability");
        ListView_InsertColumn(g_processList, 3, &column);
        column.cx = 300; column.pszText = const_cast<LPWSTR>(L"Process directory");
        ListView_InsertColumn(g_processList, 4, &column);
        column.cx = 480; column.pszText = const_cast<LPWSTR>(L"Command line");
        ListView_InsertColumn(g_processList, 5, &column);
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
        g_statusText = CreateWindowW(L"STATIC", g_shortcuts.empty() ? L"No valid shortcuts found in shortcuts.ini" : L"Click a shortcut to send it to the active application.",
            WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, hWnd, nullptr, hInst, nullptr);
        LayoutMainWindow(hWnd);
        RefreshProcesses();
        if (!AddTrayIcon(hWnd))
            AppendDiagnosticLog(L"Unable to add the tray icon, error=" + std::to_wstring(GetLastError()));
        return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.x = 800;
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.y = 340;
        return 0;
    case WM_NCHITTEST:
        {
            // 标题栏区域用于拖动窗口；子按钮和调整大小边缘仍由系统处理。
            LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);
            if (hit == HTCLIENT)
            {
                POINT point = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
                ScreenToClient(hWnd, &point);
                HWND child = ChildWindowFromPoint(hWnd, point);
                if (point.y >= 0 && point.y < MAIN_TITLE_BAR_HEIGHT &&
                    (child == nullptr || child == hWnd))
                    return HTCAPTION;
            }
            return hit;
        }
    case WM_DRAWITEM:
        {
            const DRAWITEMSTRUCT* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item != nullptr && item->CtlType == ODT_BUTTON)
            {
                DrawMainButton(item);
                return TRUE;
            }
        }
        break;
    case WM_SIZE:
        LayoutMainWindow(hWnd);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(120, 120, 120));
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lParam)->idFrom == ID_PROCESS_LIST &&
            reinterpret_cast<NMHDR*>(lParam)->code == LVN_COLUMNCLICK)
        {
            const int column = reinterpret_cast<NMLISTVIEW*>(lParam)->iSubItem;
            if (column == 0 || column == 1)
            {
                g_sortDescending = column == g_sortColumn ? !g_sortDescending : false;
                g_sortColumn = column;
                DisplayProcesses();
            }
            return 0;
        }
        if (reinterpret_cast<NMHDR*>(lParam)->idFrom == ID_PROCESS_LIST &&
            reinterpret_cast<NMHDR*>(lParam)->code == LVN_ITEMCHANGED && !g_refreshingProcesses)
        {
            static bool syncing = false;
            auto* change = reinterpret_cast<NMLISTVIEW*>(lParam);
            if (!syncing && change->iItem >= 0 && (change->uChanged & LVIF_STATE) != 0)
            {
                syncing = true;
                if ((change->uOldState & LVIS_STATEIMAGEMASK) != (change->uNewState & LVIS_STATEIMAGEMASK))
                {
                    ListView_SetItemState(g_processList, change->iItem,
                        ListView_GetCheckState(g_processList, change->iItem) ? LVIS_SELECTED : 0, LVIS_SELECTED);
                }
                else if ((change->uOldState & LVIS_SELECTED) != (change->uNewState & LVIS_SELECTED))
                {
                    ListView_SetCheckState(g_processList, change->iItem, (change->uNewState & LVIS_SELECTED) != 0);
                }
                syncing = false;
            }
            UpdateExitButton();
        }
        return 0;
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(wParam) == g_processList)
        {
            POINT point = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            int index = -1;
            if (point.x == -1 && point.y == -1)
            {
                index = ListView_GetNextItem(g_processList, -1, LVNI_FOCUSED);
                RECT bounds = {};
                if (index >= 0 && ListView_GetItemRect(g_processList, index, &bounds, LVIR_BOUNDS))
                {
                    point = { bounds.left, bounds.bottom };
                    ClientToScreen(g_processList, &point);
                }
            }
            else
            {
                LVHITTESTINFO hit = {};
                hit.pt = point;
                ScreenToClient(g_processList, &hit.pt);
                index = ListView_HitTest(g_processList, &hit);
            }
            ShowProcessContextMenu(hWnd, index, point);
            return 0;
        }
        break;
    case WM_EXIT_FINISHED:
        if (g_exitWorker.joinable()) g_exitWorker.join();
        g_exitPending = false;
        EnableWindow(g_refreshButton, TRUE);
        EnableWindow(g_taskbarOnlyCheck, TRUE);
        RefreshProcesses();
        SetWindowTextW(g_statusText, (L"Batch completed. Exited: " + std::to_wstring(wParam) +
            L" / " + std::to_wstring(lParam) + L". See logs for failures or unsupported processes.").c_str());
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
            if (wmId == ID_PROCESS_SEARCH && HIWORD(wParam) == EN_CHANGE)
            {
                DisplayProcesses();
                return 0;
            }
            if (wmId == ID_MAIN_MINIMIZE) { ShowWindow(hWnd, SW_MINIMIZE); return 0; }
            if (wmId == ID_MAIN_CLOSE) { HideMainWindow(hWnd); return 0; }
            if (wmId == ID_REFRESH) { RefreshProcesses(); return 0; }
            if (wmId == ID_REQUEST_EXIT) { RequestSelectedExit(hWnd); return 0; }
            if (wmId == ID_TERMINATE_PROCESS) { RequestSelectedExit(hWnd, true); return 0; }
            if (wmId == ID_TASKBAR_ONLY) { if (!g_exitPending) RefreshProcesses(); return 0; }
            if (wmId == ID_TASKBAR_MENU) { RequestSelectedTaskbarMenu(); return 0; }
            if (wmId == ID_COPY_LOGS) { CopyDiagnosticLogs(); return 0; }
            if (wmId == ID_SHORTCUT_PANEL_BUTTON) { ShowShortcutPanel(); return 0; }
            if (wmId == ID_MAIN_EXIT_PROGRAM) { DestroyWindow(hWnd); return 0; }
            if (wmId == ID_MAIN_SETTINGS)
            {
                g_settingsOpen = true;
                DialogBox(hInst, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, Settings);
                g_settingsOpen = false;
                return 0;
            }
            if (wmId == IDM_SETTINGS)
            {
                g_settingsOpen = true;
                DialogBox(hInst, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, Settings);
                g_settingsOpen = false;
                return 0;
            }
            if (wmId == IDM_SHORTCUTS)
            {
                ShowShortcutPanel();
                return 0;
            }
            // 分析菜单选择:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                HideMainWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_TRAYICON:
        if (wParam == TRAY_UID)
        {
            if (lParam == WM_LBUTTONDBLCLK)
                ShowMainWindow(hWnd);
            else if (lParam == WM_RBUTTONUP)
                ShowTrayMenu(hWnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        HideMainWindow(hWnd);
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: 在此处添加使用 hdc 的任何绘图代码...
            RECT client = {};
            GetClientRect(hWnd, &client);
            RECT titleRect = { 12, 0, client.right - 12, MAIN_TITLE_BAR_HEIGHT };
            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ oldFont = SelectObject(hdc, font);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(64, 64, 64));
            DrawTextW(hdc, szTitle, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldFont);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(224, 224, 224));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            MoveToEx(hdc, 0, MAIN_TITLE_BAR_HEIGHT - 1, nullptr);
            LineTo(hdc, client.right, MAIN_TITLE_BAR_HEIGHT - 1);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        RemoveTrayIcon();
        if (g_mouseTriggerPending)
            KillTimer(hWnd, ID_MOUSE_TRIGGER_TIMER);
        if (g_mouseTriggerRegistered)
        {
            UnregisterHotKey(hWnd, ID_MOUSE_TRIGGER_HOTKEY);
            g_mouseTriggerRegistered = false;
        }
        if (g_exitWorker.joinable()) g_exitWorker.join();
        if (g_shortcutWindow != nullptr)
            DestroyWindow(g_shortcutWindow);
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
