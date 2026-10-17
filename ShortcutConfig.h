#pragma once

#include <windows.h>
#include <string>
#include <vector>

constexpr size_t MAX_SHORTCUTS = 500;

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

std::wstring Trim(const std::wstring& value);
bool ParseShortcut(const std::wstring& text, ShortcutItem& item);
std::wstring GetConfigurationPath(const wchar_t* fileName);
bool ReadConfigurationText(const std::wstring& path, std::wstring& text);
bool ParseShortcutsText(const std::wstring& text, std::vector<ShortcutItem>& shortcuts, bool rejectInvalid = false);
void LoadShortcuts(std::vector<ShortcutItem>& shortcuts);
bool LoadDefaultShortcuts(HINSTANCE instance, std::vector<ShortcutItem>& shortcuts);
