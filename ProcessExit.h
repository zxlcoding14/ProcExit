#pragma once

#include <windows.h>
#include <string>

enum class GracefulExitStatus
{
    Exited,
    RequestSent,
    StillRunning,
    Unsupported,
    Failed
};

struct GracefulExitResult
{
    GracefulExitStatus status = GracefulExitStatus::Failed;
    DWORD errorCode = ERROR_SUCCESS;
    UINT windowsContacted = 0;
};

GracefulExitResult RequestGracefulExit(DWORD processId, ULONGLONG creationTime, DWORD timeoutMs);
void AppendDiagnosticLog(const std::wstring& message);
std::wstring GetDiagnosticLog();
