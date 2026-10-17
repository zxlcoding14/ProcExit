// header.h: 标准系统包含文件的包含文件，
// 或特定于项目的包含文件
//

#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
// Windows 头文件
#include <windows.h>
// C 运行时头文件
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

inline UINT GetWindowDpi(HWND hWnd)
{
    return hWnd != nullptr ? GetDpiForWindow(hWnd) : GetDpiForSystem();
}

inline int ScaleForDpi(HWND hWnd, int value)
{
    return MulDiv(value, static_cast<int>(GetWindowDpi(hWnd)), 96);
}
