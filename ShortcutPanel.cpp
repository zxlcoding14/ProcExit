#include "framework.h"
#include "ShortcutPanel.h"
#include "ShortcutConfig.h"
#include "ProcQuit.h"
#include "ProcessExit.h"
#include "WindowControls.h"
#include <commctrl.h>
#include <algorithm>
#include <cwctype>
#include <utility>
#include <vector>

static HINSTANCE hInst = nullptr;
static HWND g_statusText = nullptr;
static std::vector<ShortcutItem> g_shortcuts;
static std::vector<HWND> g_shortcutButtons;
static constexpr int ID_SHORTCUT_ADD = 1907;
static constexpr int ID_SHORTCUT_REORDER = 1909;
static constexpr int ID_SHORTCUT_CLOSE = 1910;
static constexpr int ID_SHORTCUT_MINIMIZE = 1914;
static constexpr int ID_SHORTCUT_CLEAR = 1915;
static constexpr int ID_SHORTCUT_RESET = 1916;
static constexpr int ID_SHORTCUT_SEND_BASE = 2000;
static constexpr int ID_SHORTCUT_DELETE_BASE = 3000;
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

static std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
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
    return ScaleForDpi(nullptr, SHORTCUT_CELL_HEIGHT + SHORTCUT_PANEL_GAP);
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
    int width = GetShortcutPanelInnerWidth(hWnd) - ScaleForDpi(hWnd, SHORTCUT_PANEL_MARGIN * 2);
    return width < 0 ? 0 : width;
}

static int GetShortcutPanelColumns(HWND hWnd)
{
    int available = GetShortcutPanelAvailableWidth(hWnd);
    int columns = (available + ScaleForDpi(hWnd, SHORTCUT_PANEL_GAP)) / (ScaleForDpi(hWnd, SHORTCUT_CELL_WIDTH + SHORTCUT_PANEL_GAP));
    return columns < SHORTCUT_MIN_COLUMNS ? SHORTCUT_MIN_COLUMNS : columns;
}

static int GetShortcutPanelCellWidth(HWND hWnd, int columns)
{
    int available = GetShortcutPanelAvailableWidth(hWnd);
    int cellWidth = columns > 0 ? (available - (columns - 1) * ScaleForDpi(hWnd, SHORTCUT_PANEL_GAP)) / columns : SHORTCUT_CELL_WIDTH;
    return cellWidth < ScaleForDpi(hWnd, SHORTCUT_MIN_CELL_WIDTH) ? ScaleForDpi(hWnd, SHORTCUT_MIN_CELL_WIDTH) : cellWidth;
}

static int GetShortcutPanelMinClientWidth()
{
    return ScaleForDpi(nullptr, SHORTCUT_PANEL_MARGIN * 2 + SHORTCUT_MIN_COLUMNS * SHORTCUT_CELL_WIDTH + (SHORTCUT_MIN_COLUMNS - 1) * SHORTCUT_PANEL_GAP) + GetSystemMetrics(SM_CXVSCROLL);
}

static int GetShortcutPanelMinClientHeight()
{
    return ScaleForDpi(nullptr, SHORTCUT_PANEL_MARGIN - SHORTCUT_PANEL_GAP + SHORTCUT_PANEL_FOOTER + SHORTCUT_PANEL_TITLE_BAR_HEIGHT) + SHORTCUT_MIN_ROWS * GetShortcutPanelRowStep();
}

static int GetShortcutPanelInitialClientWidth()
{
    return ScaleForDpi(nullptr, SHORTCUT_PANEL_MARGIN * 2 + SHORTCUT_INITIAL_COLUMNS * SHORTCUT_CELL_WIDTH + (SHORTCUT_INITIAL_COLUMNS - 1) * SHORTCUT_PANEL_GAP) + GetSystemMetrics(SM_CXVSCROLL);
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
    const int margin = ScaleForDpi(hWnd, SHORTCUT_PANEL_MARGIN);
    const int footer = ScaleForDpi(hWnd, SHORTCUT_PANEL_FOOTER);
    const int titleBarHeight = ScaleForDpi(hWnd, SHORTCUT_PANEL_TITLE_BAR_HEIGHT);
    const int cellHeight = ScaleForDpi(hWnd, SHORTCUT_CELL_HEIGHT);
    const int gap = ScaleForDpi(hWnd, SHORTCUT_PANEL_GAP);
    RECT client = {};
    GetClientRect(hWnd, &client);
    int viewportBottom = client.bottom - footer;
    int column = 0;
    int x = margin;
    int y = margin + titleBarHeight;
    int bottom = margin + titleBarHeight;

    // 所有按钮的位置一次提交，避免逐个移动时画出中间状态。
    HDWP defer = apply ? BeginDeferWindowPos(static_cast<int>(g_shortcuts.size())) : nullptr;

    for (size_t index = 0; index < g_shortcuts.size(); ++index)
    {
        if (apply && index < g_shortcutButtons.size() && g_shortcutButtons[index] != nullptr)
        {
            HWND button = g_shortcutButtons[index];
            bool visible = y + yOffset >= 0 && y + yOffset + cellHeight <= viewportBottom;
            // 状态没变就不要调用，否则每次排版都会多一次无谓的重画。
            if ((IsWindowVisible(button) != FALSE) != visible)
                ShowWindow(button, visible ? SW_SHOWNA : SW_HIDE);
            if (defer != nullptr)
                defer = DeferWindowPos(defer, button, nullptr, x, y + yOffset, cellWidth, cellHeight,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            else
                MoveWindow(button, x, y + yOffset, cellWidth, cellHeight, TRUE);
        }
        bottom = y + cellHeight;

        ++column;
        if (column >= columns)
        {
            column = 0;
            x = margin;
            y += rowStep;
        }
        else
        {
            x += cellWidth + gap;
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
    const int margin = ScaleForDpi(hWnd, SHORTCUT_PANEL_MARGIN);
    const int titleBarHeight = ScaleForDpi(hWnd, SHORTCUT_PANEL_TITLE_BAR_HEIGHT);
    const int footer = ScaleForDpi(hWnd, SHORTCUT_PANEL_FOOTER);
    const int buttonGap = ScaleForDpi(hWnd, 40);

    WalkShortcutPanel(hWnd, -g_shortcutScrollOffset, true);

    if (g_shortcutMinimizeButton != nullptr)
        MoveWindow(g_shortcutMinimizeButton, client.right - ScaleForDpi(hWnd, 36) - margin - buttonGap,
            (titleBarHeight - ScaleForDpi(hWnd, 24)) / 2, ScaleForDpi(hWnd, 36), ScaleForDpi(hWnd, 24), TRUE);
    if (g_shortcutCloseButton != nullptr)
        MoveWindow(g_shortcutCloseButton, client.right - ScaleForDpi(hWnd, 36) - margin,
            (titleBarHeight - ScaleForDpi(hWnd, 24)) / 2, ScaleForDpi(hWnd, 36), ScaleForDpi(hWnd, 24), TRUE);

    int footerTop = client.bottom - footer + ScaleForDpi(hWnd, 4);
    if (g_shortcutAddButton != nullptr)
        MoveWindow(g_shortcutAddButton, margin, footerTop, ScaleForDpi(hWnd, 86), ScaleForDpi(hWnd, 22), TRUE);
    if (g_shortcutClearButton != nullptr)
        MoveWindow(g_shortcutClearButton, margin + ScaleForDpi(hWnd, 92), footerTop, ScaleForDpi(hWnd, 68), ScaleForDpi(hWnd, 22), TRUE);
    if (g_shortcutResetButton != nullptr)
        MoveWindow(g_shortcutResetButton, margin + ScaleForDpi(hWnd, 166), footerTop, ScaleForDpi(hWnd, 58), ScaleForDpi(hWnd, 22), TRUE);
    if (g_shortcutHintText != nullptr)
    {
        int hintWidth = client.right - margin * 2 - ScaleForDpi(hWnd, 232);
        ShowWindow(g_shortcutHintText, hintWidth >= ScaleForDpi(hWnd, 120) ? SW_SHOWNA : SW_HIDE);
        MoveWindow(g_shortcutHintText, margin + ScaleForDpi(hWnd, 232), footerTop + ScaleForDpi(hWnd, 3),
            hintWidth > 0 ? hintWidth : 0, ScaleForDpi(hWnd, 16), TRUE);
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
    if (!LoadDefaultShortcuts(hInst, defaults))
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
                if (point.y >= 0 && point.y < ScaleForDpi(hWnd, SHORTCUT_PANEL_TITLE_BAR_HEIGHT) &&
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
            RECT titleRect = { ScaleForDpi(hWnd, SHORTCUT_PANEL_MARGIN), 0, client.right - ScaleForDpi(hWnd, SHORTCUT_PANEL_MARGIN), ScaleForDpi(hWnd, SHORTCUT_PANEL_TITLE_BAR_HEIGHT) };
            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ oldFont = SelectObject(hdc, font);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(64, 64, 64));
            DrawTextW(hdc, L"Shortcuts", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldFont);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(224, 224, 224));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            MoveToEx(hdc, 0, ScaleForDpi(hWnd, SHORTCUT_PANEL_TITLE_BAR_HEIGHT) - ScaleForDpi(hWnd, 1), nullptr);
            LineTo(hdc, client.right, ScaleForDpi(hWnd, SHORTCUT_PANEL_TITLE_BAR_HEIGHT) - ScaleForDpi(hWnd, 1));
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            EndPaint(hWnd, &ps);
        }
        return 0;
    case WM_SIZE:
        RefreshShortcutPanel(hWnd);
        return 0;
    case WM_DPICHANGED:
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) SetWindowPos(hWnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshShortcutPanel(hWnd);
        }
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

void ShowShortcutPanel()
{
    if (g_shortcutWindow == nullptr)
        CreateShortcutPanel();
    if (g_shortcutWindow == nullptr)
        return;
    ShowWindow(g_shortcutWindow, SW_SHOWNOACTIVATE);
    SetWindowPos(g_shortcutWindow, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void InitializeShortcutPanel(HINSTANCE instance, HWND statusText)
{
    hInst = instance;
    g_statusText = statusText;
    RegisterShortcutPanelClass(instance);
    LoadShortcuts(g_shortcuts);
}

bool HasShortcuts()
{
    return !g_shortcuts.empty();
}

void ShutdownShortcutPanel()
{
    if (g_shortcutWindow != nullptr)
        DestroyWindow(g_shortcutWindow);
    g_statusText = nullptr;
    g_shortcuts.clear();
}
