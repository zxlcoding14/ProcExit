#pragma once

#include <windows.h>
#include <string>
void InitializeShortcutPanel(HINSTANCE instance, HWND statusText);
bool HasShortcuts();
void ShowShortcutPanel();
void ShutdownShortcutPanel();
