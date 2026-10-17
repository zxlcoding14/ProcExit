#include "framework.h"
#include "ProcQuit.h"
#include "MainWindow.h"
#include "ProcessExit.h"
#include "WindowControls.h"
#include "ShortcutPanel.h"
#include "MouseTrigger.h"
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
static HINSTANCE hInst;                                // 当前实例
static WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
static WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名

// 此代码模块中包含的函数的前向声明:
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    Settings(HWND, UINT, WPARAM, LPARAM);

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
static constexpr int ID_SHORTCUT_PANEL_BUTTON = 1908;
static constexpr int ID_MAIN_MINIMIZE = 1911;
static constexpr int ID_MAIN_CLOSE = 1912;
static constexpr int ID_MAIN_SETTINGS = 1913;
static constexpr int ID_MAIN_EXIT_PROGRAM = 1917;
static constexpr int ID_TERMINATE_PROCESS = 1918;
static constexpr int ID_TASKBAR_ONLY = 1919;
static constexpr int ID_PROCESS_SEARCH = 1920;
static constexpr int ID_COPY_NAME = 1921;
static constexpr int ID_COPY_PATH = 1922;
static constexpr int ID_COPY_COMMAND = 1923;
static constexpr int ID_OPEN_PATH = 1924;
static constexpr int MAIN_TITLE_BAR_HEIGHT = 32;
static bool g_settingsOpen = false;

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

static void LayoutMainWindow(HWND hWnd)
{
    RECT client = {};
    GetClientRect(hWnd, &client);
    const int margin = ScaleForDpi(hWnd, 12);
    const int buttonHeight = ScaleForDpi(hWnd, 28);
    const int titleBarHeight = ScaleForDpi(hWnd, MAIN_TITLE_BAR_HEIGHT);
    const int gap = ScaleForDpi(hWnd, 8);
    int top = titleBarHeight + gap;
    int listTop = top + (buttonHeight + gap) * 2;
    int listHeight = client.bottom - listTop - ScaleForDpi(hWnd, 40);
    if (listHeight < ScaleForDpi(hWnd, 40)) listHeight = ScaleForDpi(hWnd, 40);

    HDWP defer = BeginDeferWindowPos(14);
    auto place = [&defer](HWND window, int x, int y, int width, int height) {
        if (window == nullptr || defer == nullptr) return;
        defer = DeferWindowPos(defer, window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    place(g_mainCloseButton, client.right - ScaleForDpi(hWnd, 36) - margin, (titleBarHeight - ScaleForDpi(hWnd, 24)) / 2, ScaleForDpi(hWnd, 36), ScaleForDpi(hWnd, 24));
    place(g_mainMinimizeButton, client.right - ScaleForDpi(hWnd, 36) - margin - ScaleForDpi(hWnd, 40), (titleBarHeight - ScaleForDpi(hWnd, 24)) / 2, ScaleForDpi(hWnd, 36), ScaleForDpi(hWnd, 24));
    place(g_refreshButton, margin, top, ScaleForDpi(hWnd, 90), buttonHeight);
    place(g_exitButton, margin + ScaleForDpi(hWnd, 100), top, ScaleForDpi(hWnd, 130), buttonHeight);
    place(g_taskbarMenuButton, margin + ScaleForDpi(hWnd, 240), top, ScaleForDpi(hWnd, 160), buttonHeight);
    place(g_copyLogsButton, margin + ScaleForDpi(hWnd, 410), top, ScaleForDpi(hWnd, 120), buttonHeight);
    place(g_shortcutPanelButton, margin + ScaleForDpi(hWnd, 540), top, ScaleForDpi(hWnd, 110), buttonHeight);
    place(g_settingsButton, margin + ScaleForDpi(hWnd, 660), top, ScaleForDpi(hWnd, 100), buttonHeight);
    place(g_terminateButton, margin, top + buttonHeight + gap, ScaleForDpi(hWnd, 150), buttonHeight);
    place(g_taskbarOnlyCheck, margin + ScaleForDpi(hWnd, 166), top + buttonHeight + gap, ScaleForDpi(hWnd, 220), buttonHeight);
    place(g_processSearch, margin + ScaleForDpi(hWnd, 400), top + buttonHeight + gap, client.right - margin * 2 - ScaleForDpi(hWnd, 400), buttonHeight);
    place(g_processList, margin, listTop, client.right - margin * 2, listHeight);
    place(g_statusText, margin, client.bottom - ScaleForDpi(hWnd, 30), client.right - margin * 2 - ScaleForDpi(hWnd, 114), ScaleForDpi(hWnd, 20));
    place(g_programExitButton, client.right - margin - ScaleForDpi(hWnd, 104), client.bottom - ScaleForDpi(hWnd, 34), ScaleForDpi(hWnd, 104), ScaleForDpi(hWnd, 26));
    if (defer != nullptr) EndDeferWindowPos(defer);
}

// 初始化全局字符串
void LoadMainWindowStrings(HINSTANCE hInstance)
{
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PROCQUIT, szWindowClass, MAX_LOADSTRING);
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
   const UINT dpi = GetDpiForSystem();
   int width = MulDiv(960, static_cast<int>(dpi), 96);
   int height = MulDiv(620, static_cast<int>(dpi), 96);
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
        g_statusText = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, hWnd, nullptr, hInst, nullptr);
        InitializeShortcutPanel(hInst, g_statusText);
        SetWindowTextW(g_statusText, HasShortcuts() ? L"Click a shortcut to send it to the active application." : L"No valid shortcuts found in shortcuts.ini");
        InitializeMouseTrigger(hWnd, g_statusText);
        LayoutMainWindow(hWnd);
        RefreshProcesses();
        if (!AddTrayIcon(hWnd))
            AppendDiagnosticLog(L"Unable to add the tray icon, error=" + std::to_wstring(GetLastError()));
        return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.x = ScaleForDpi(hWnd, 800);
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize.y = ScaleForDpi(hWnd, 340);
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
                if (point.y >= 0 && point.y < ScaleForDpi(hWnd, MAIN_TITLE_BAR_HEIGHT) &&
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
    case WM_DPICHANGED:
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) SetWindowPos(hWnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            LayoutMainWindow(hWnd);
        }
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
        if (wParam == ID_MOUSE_TRIGGER_HOTKEY && !g_settingsOpen)
        {
            HandleMouseTriggerHotkey(hWnd);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == ID_MOUSE_TRIGGER_TIMER)
        {
            HandleMouseTriggerTimer(hWnd);
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
            RECT titleRect = { ScaleForDpi(hWnd, 12), 0, client.right - ScaleForDpi(hWnd, 12), ScaleForDpi(hWnd, MAIN_TITLE_BAR_HEIGHT) };
            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ oldFont = SelectObject(hdc, font);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(64, 64, 64));
            DrawTextW(hdc, szTitle, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldFont);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(224, 224, 224));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            MoveToEx(hdc, 0, ScaleForDpi(hWnd, MAIN_TITLE_BAR_HEIGHT) - ScaleForDpi(hWnd, 1), nullptr);
            LineTo(hdc, client.right, ScaleForDpi(hWnd, MAIN_TITLE_BAR_HEIGHT) - ScaleForDpi(hWnd, 1));
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        RemoveTrayIcon();
        ShutdownMouseTrigger(hWnd);
        if (g_exitWorker.joinable()) g_exitWorker.join();
        ShutdownShortcutPanel();
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
        SetDlgItemTextW(hDlg, IDC_MOUSE_TRIGGER_SHORTCUT, GetMouseTriggerShortcut().c_str());
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
