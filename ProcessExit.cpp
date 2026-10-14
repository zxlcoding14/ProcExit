#include "framework.h"
#include "ProcessExit.h"

#include <vector>
#include <mutex>
#include <sstream>

namespace
{
std::mutex g_logMutex;
std::wstring g_diagnosticLog;
}

void AppendDiagnosticLog(const std::wstring& message)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    wchar_t prefix[64] = {};
    swprintf_s(prefix, L"[%04u-%02u-%02u %02u:%02u:%02u] ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    g_diagnosticLog += prefix;
    g_diagnosticLog += message;
    g_diagnosticLog += L"\r\n";
    OutputDebugStringW(message.c_str());
    OutputDebugStringW(L"\n");
}

std::wstring GetDiagnosticLog()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_diagnosticLog;
}

namespace
{
struct WindowCandidate
{
    HWND handle = nullptr;
};

BOOL CALLBACK CollectTopLevelWindows(HWND window, LPARAM parameter)
{
    auto* candidates = reinterpret_cast<std::vector<WindowCandidate>*>(parameter);
    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(window, &windowProcessId);

    if (windowProcessId == 0 || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr)
        return TRUE;

    candidates->push_back({ window });
    return TRUE;
}

bool IsSameProcessInstance(HANDLE process, ULONGLONG expectedCreationTime)
{
    FILETIME creationTime = {};
    FILETIME exitTime = {};
    FILETIME kernelTime = {};
    FILETIME userTime = {};

    if (!GetProcessTimes(process, &creationTime, &exitTime, &kernelTime, &userTime))
        return false;

    ULARGE_INTEGER actual = {};
    actual.LowPart = creationTime.dwLowDateTime;
    actual.HighPart = creationTime.dwHighDateTime;
    return actual.QuadPart == expectedCreationTime;
}
}

GracefulExitResult RequestGracefulExit(DWORD processId, ULONGLONG creationTime, DWORD timeoutMs)
{
    GracefulExitResult result;
    AppendDiagnosticLog(L"Request started for PID " + std::to_wstring(processId));
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, processId);
    if (process == nullptr)
    {
        result.errorCode = GetLastError();
        AppendDiagnosticLog(L"OpenProcess failed, error=" + std::to_wstring(result.errorCode));
        return result;
    }

    if (!IsSameProcessInstance(process, creationTime))
    {
        result.status = GracefulExitStatus::Failed;
        result.errorCode = ERROR_INVALID_DATA;
        AppendDiagnosticLog(L"Process instance validation failed");
        CloseHandle(process);
        return result;
    }

    std::vector<WindowCandidate> candidates;
    EnumWindows(CollectTopLevelWindows, reinterpret_cast<LPARAM>(&candidates));
    AppendDiagnosticLog(L"Top-level windows enumerated: " + std::to_wstring(candidates.size()));

    for (const auto& candidate : candidates)
    {
        DWORD windowProcessId = 0;
        GetWindowThreadProcessId(candidate.handle, &windowProcessId);
        if (windowProcessId != processId || !IsWindow(candidate.handle))
            continue;

        if (PostMessageW(candidate.handle, WM_CLOSE, 0, 0))
            ++result.windowsContacted;
        else
            AppendDiagnosticLog(L"WM_CLOSE failed for HWND " + std::to_wstring(reinterpret_cast<ULONG_PTR>(candidate.handle)) + L", error=" + std::to_wstring(GetLastError()));
    }

    DWORD firstWaitMs = timeoutMs / 2;
    DWORD waitResult = WaitForSingleObject(process, firstWaitMs);
    AppendDiagnosticLog(L"First wait completed with result " + std::to_wstring(waitResult));
    if (waitResult == WAIT_OBJECT_0)
    {
        result.status = GracefulExitStatus::Exited;
        CloseHandle(process);
        return result;
    }

    if (waitResult == WAIT_FAILED)
    {
        result.errorCode = GetLastError();
        result.status = GracefulExitStatus::Failed;
        CloseHandle(process);
        return result;
    }

    for (const auto& candidate : candidates)
    {
        DWORD windowProcessId = 0;
        GetWindowThreadProcessId(candidate.handle, &windowProcessId);
        if (windowProcessId != processId || !IsWindow(candidate.handle))
            continue;

        if (PostMessageW(candidate.handle, WM_SYSCOMMAND, SC_CLOSE, 0))
            ++result.windowsContacted;
        else
            AppendDiagnosticLog(L"SC_CLOSE failed for HWND " + std::to_wstring(reinterpret_cast<ULONG_PTR>(candidate.handle)) + L", error=" + std::to_wstring(GetLastError()));
    }

    waitResult = WaitForSingleObject(process, timeoutMs - firstWaitMs);
    AppendDiagnosticLog(L"Second wait completed with result " + std::to_wstring(waitResult) + L", windows contacted=" + std::to_wstring(result.windowsContacted));
    if (waitResult == WAIT_OBJECT_0)
        result.status = GracefulExitStatus::Exited;
    else if (waitResult == WAIT_TIMEOUT)
        result.status = result.windowsContacted == 0 ? GracefulExitStatus::Unsupported : GracefulExitStatus::StillRunning;
    else
    {
        result.errorCode = GetLastError();
        result.status = GracefulExitStatus::Failed;
    }

    CloseHandle(process);
    return result;
}
