# Taskbar Application Context Menu

## Purpose

ProcQuit now lists candidate windows that ordinarily receive taskbar buttons instead of every process in the system. Selecting an entry and clicking `Right-click Taskbar` requests the context menu associated with that window's taskbar button.

## Enumeration

The application performs one `EnumWindows` pass and keeps visible, uncloaked top-level windows that are unowned unless explicitly marked with `WS_EX_APPWINDOW`. Tool windows are excluded unless they explicitly use `WS_EX_APPWINDOW`. Windows without a title and the ProcQuit window itself are excluded. Each qualifying window is listed separately, so multiple windows owned by one process can be selected independently.

Process names are collected from one Toolhelp process snapshot. Creation time is read with `PROCESS_QUERY_LIMITED_INFORMATION` and remains the process-instance check used by the existing graceful-exit action. The refresh cost is approximately O(P + W), where P is the process count and W is the top-level window count.

## Right-click behavior

The primary path uses Microsoft UI Automation:

1. Locate primary and secondary taskbars by the `Shell_TrayWnd` and `Shell_SecondaryTrayWnd` window classes.
2. Enumerate descendant button controls and retain only controls using Explorer's observed `Appid:` application-button convention. This is an implementation heuristic and does not guarantee that every system control is excluded on every Windows version.
3. Require the accessible button name to exactly equal the selected window title.
4. Require one unique match and a complete successful search across all discovered taskbars.
5. Send an absolute mouse move followed by right-button down/up input at the matched button center.

If no unique high-confidence taskbar button can be identified, ProcQuit reports the failure and sends no input. It does not substitute an application-window context menu because that menu is not the Explorer taskbar menu.

## Compatibility and limitations

- Taskbar button discovery is heuristic because Windows does not expose a documented API that maps an arbitrary HWND directly to its taskbar button.
- Grouped taskbar buttons, localized accessible names, customized shells, accessibility-disabled environments, and future Windows taskbar implementations can prevent a reliable match.
- The `Appid:` Automation ID convention is shell implementation behavior rather than a documented HWND-to-button contract. If Explorer does not expose it, ProcQuit refuses to send input.
- The displayed rows are taskbar-window candidates rather than a direct enumeration of Explorer's internal taskbar model. Applications can explicitly add or remove taskbar tabs, so some rows may not have a matching button.
- Simulated input can be blocked by UIPI, secure desktop boundaries, or a higher-integrity shell/target. ProcQuit does not elevate itself or inject code.
- The actual pointer moves to the taskbar button when the right-click is sent. This mirrors a user input event and makes the destination visible.
- ProcQuit opts into per-monitor V2 DPI awareness before creating windows so UI Automation bounds and absolute input coordinates use a consistent coordinate space.

## Implementation scope

The change is intentionally limited to `ProcQuit.cpp` and this document. Existing graceful-exit behavior, shortcut parsing, shortcut input, diagnostic-log copying, resources, and third-party WTL headers remain unchanged. UI Automation properties are cached in one request per taskbar to reduce cross-process calls; discovery still runs synchronously only when the user requests the taskbar action.
