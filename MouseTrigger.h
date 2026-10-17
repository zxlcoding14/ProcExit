#pragma once

#include "framework.h"
#include <string>

constexpr int ID_MOUSE_TRIGGER_HOTKEY = 1905;
constexpr UINT_PTR ID_MOUSE_TRIGGER_TIMER = 1906;

void InitializeMouseTrigger(HWND owner, HWND statusWindow);
void ShutdownMouseTrigger(HWND owner);
void HandleMouseTriggerHotkey(HWND owner);
void HandleMouseTriggerTimer(HWND owner);
const std::wstring& GetMouseTriggerShortcut();
bool UpdateMouseTrigger(HWND owner, const std::wstring& shortcut, std::wstring& errorMessage);
bool SendRightClick(const RECT& bounds);
