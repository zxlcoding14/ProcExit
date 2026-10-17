#include "framework.h"
#include "WindowControls.h"
#include <commctrl.h>

// 主窗口按钮自绘：白底细边框，悬停变浅蓝、按下变深蓝，与快捷键浮窗一致。
void DrawMainButton(const DRAWITEMSTRUCT* item)
{
    HDC dc = item->hDC;
    RECT rect = item->rcItem;
    bool hovered = GetWindowLongPtrW(item->hwndItem, GWLP_USERDATA) != 0;
    bool pressed = (item->itemState & ODS_SELECTED) != 0;

    COLORREF fill = RGB(255, 255, 255);
    if (pressed) fill = RGB(214, 232, 250);
    else if (hovered) fill = RGB(240, 247, 254);

    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);

    COLORREF border = (pressed || hovered) ? RGB(0, 120, 212) : RGB(200, 200, 200);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    WCHAR text[128] = {};
    GetWindowTextW(item->hwndItem, text, static_cast<int>(_countof(text)));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(32, 32, 32));
    DrawTextW(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont != nullptr) SelectObject(dc, oldFont);
}

// 主窗口按钮的悬停跟踪；GWLP_USERDATA 为 1 表示鼠标正悬停在上方。
static LRESULT CALLBACK MainButtonSubclass(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR refData)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(refData);
    switch (message)
    {
    case WM_ERASEBKGND:
        // 自绘会填满整个客户区，先擦成默认底色再画只会闪一下。
        return 1;
    case WM_MOUSEMOVE:
        if (GetWindowLongPtrW(hWnd, GWLP_USERDATA) == 0)
        {
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hWnd;
            if (TrackMouseEvent(&track) != FALSE)
            {
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, 1);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        break;
    case WM_MOUSELEAVE:
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, MainButtonSubclass, subclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

HWND CreateMainButton(HWND parent, const wchar_t* text, int id)
{
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    HWND button = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 1, 1, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SetWindowSubclass(button, MainButtonSubclass, 0, 0);
    return button;
}
