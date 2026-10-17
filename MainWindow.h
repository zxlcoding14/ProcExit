#pragma once

#include <windows.h>

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
void LoadMainWindowStrings(HINSTANCE hInstance);
