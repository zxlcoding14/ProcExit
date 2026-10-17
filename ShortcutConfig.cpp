#include "framework.h"
#include "ShortcutConfig.h"
#include "ProcQuit.h"
#include <cwctype>

std::wstring Trim(const std::wstring& value)
{
    size_t start = 0;
    size_t end = value.size();
    while (start < end && iswspace(value[start]) != 0) ++start;
    while (end > start && iswspace(value[end - 1]) != 0) --end;
    return value.substr(start, end - start);
}

bool ParseShortcut(const std::wstring& text, ShortcutItem& item)
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

std::wstring GetConfigurationPath(const wchar_t* fileName)
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
bool ReadConfigurationText(const std::wstring& path, std::wstring& text)
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
bool ParseShortcutsText(const std::wstring& text, std::vector<ShortcutItem>& shortcuts, bool rejectInvalid)
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

void LoadShortcuts(std::vector<ShortcutItem>& shortcuts)
{
    std::wstring configPath = GetConfigurationPath(L"shortcuts.ini");
    if (configPath.empty()) return;

    std::wstring text;
    if (!ReadConfigurationText(configPath, text)) return;

    shortcuts.clear();
    ParseShortcutsText(text, shortcuts);
}

bool LoadDefaultShortcuts(HINSTANCE instance, std::vector<ShortcutItem>& shortcuts)
{
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(IDR_DEFAULT_SHORTCUTS), RT_RCDATA);
    if (resource == nullptr)
        return false;
    HGLOBAL loaded = LoadResource(instance, resource);
    const void* data = loaded != nullptr ? LockResource(loaded) : nullptr;
    DWORD size = SizeofResource(instance, resource);
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
