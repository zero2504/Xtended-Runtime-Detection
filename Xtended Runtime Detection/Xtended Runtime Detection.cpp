#include <windows.h>
#include <string_view>
#include <memory>
#include <stdexcept>
#include "ClipboardWatcher.h"
#include "TrayLogic.h"

// Application-wide constants
static constexpr std::wstring_view MUTEX_NAME = L"Global\\XtendedRuntimeDetection_Mutex";
static constexpr std::wstring_view PATTERN_FILE = L"patterns.txt";

// RAII wrapper to manage HANDLE lifetimes safely
struct HandleDeleter {
    void operator()(HANDLE handle) const noexcept {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};
using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleDeleter>;

int WINAPI wWinMain(
    HINSTANCE hInstance,          // Handle to the current instance
    HINSTANCE /*hPrevInstance*/,  // Unused parameter
    LPWSTR /*lpCmdLine*/,         // Command line arguments (unused)
    int /*nCmdShow*/)             // Show window flag (unused)
{
    try {
        // Ensure only one instance runs by creating a named mutex
        UniqueHandle mutex(CreateMutexW(nullptr, FALSE, MUTEX_NAME.data()));
        if (!mutex) {
            throw std::runtime_error("Failed to create mutex");
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            // Another instance is already running; exit gracefully
            return EXIT_SUCCESS;
        }

        // Initialize system tray icon
        HWND trayWindow = nullptr;
        UINT trayIconId = 0;
        if (!InitTray(hInstance, trayWindow, trayIconId)) {
            throw std::runtime_error("Tray icon initialization failed");
        }

        // Start clipboard watcher with specified pattern file
        ClipboardWatcher watcher(PATTERN_FILE.data());
        if (!watcher.Start(hInstance, trayWindow, trayIconId)) {
            CleanupTray();
            throw std::runtime_error("Failed to start clipboard watcher");
        }
        watcher.ForceInitialScan();  // Perform an immediate initial scan

        // Main message loop: dispatch Windows messages until WM_QUIT
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Clean up resources in reverse order of creation
        watcher.Stop();
        CleanupTray();
        return static_cast<int>(msg.wParam);
    }
    catch (const std::exception& ex) {
        // Display error message box before exiting
        MessageBoxA(nullptr, ex.what(), "Application Error", MB_ICONERROR | MB_OK);
        return EXIT_FAILURE;
    }
}
