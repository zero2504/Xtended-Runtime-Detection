// TrayLogic.h
#pragma once
#include <windows.h>

// Initializes a hidden, message‐only window and adds an icon to the system tray.
// @param hInstance  Application instance handle.
// @param outHwnd    Receives the tray window handle.
// @param outId      Receives the icon ID used for notifications.
// @return true on success, false on failure.
bool InitTray(HINSTANCE hInstance, HWND& outHwnd, UINT& outId);

// Removes the tray icon and destroys the hidden window.
void CleanupTray();