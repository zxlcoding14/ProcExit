# Shortcut Trigger

## Configuration

The application reads `shortcuts.ini` from the same directory as the executable. The Visual Studio project copies the sample configuration to the build output directory. Add entries under `[Shortcuts]`; the text before `=` is the button label and the text after it is the simulated key combination.

```ini
[Shortcuts]
Copy=Ctrl+C
Open Search=Ctrl+F
Refresh=F5
```

Supported modifiers are `Ctrl`, `Alt`, `Shift`, and `Win`. Supported keys are A-Z, 0-9, F1-F24, `Space`, `Enter`, `Tab`, `Esc`, `Delete`, and `Backspace`. Invalid entries are skipped.

## Behavior and limitations

The window creates one button per valid entry, lays them out in four columns, and reflows them on resize. Clicking a button sends the modifier and key events through `SendInput` to whichever application is active at that moment. Avoid clicking the trigger window immediately before selecting a target application; select the target first and activate the trigger only when ready to send.

Windows may reject simulated input across integrity levels or to protected/secure desktop applications. `SendInput` reports failure in the status line; run both applications at the same privilege level. Shortcut buttons do not target a specific process.

## Mouse right-click trigger

Open **File > Settings...** to edit the mouse right-click shortcut. The setting is stored separately from the shortcut buttons in `settings.ini`:

```ini
[MouseTrigger]
Shortcut=Ctrl+Shift+1
```

Move the pointer to the target position and press the configured hotkey. The application captures the current screen coordinates, waits until the hotkey and its modifiers are released, and then sends a right-click at the captured position. This feature does not install a global mouse hook and does not listen for mouse clicks.

Only one mouse trigger hotkey can be configured. If `settings.ini` is missing, the application uses `Ctrl+Shift+1` as the built-in default. Changes saved from the settings dialog take effect immediately. Use at least one modifier to avoid globally reserving a normal typing key. If the combination is already registered by another application, the previous shortcut remains active and the diagnostic log records the error. The trigger is canceled if its keys remain pressed for five seconds. Simulated mouse input remains subject to Windows integrity-level and secure-desktop restrictions.
