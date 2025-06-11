// TrayLogic.cpp
#include "TrayLogic.h"
#include "resource.h"     // IDI_APP_MAIN, IDM_TRAY_* commands
#include <shellapi.h>
#include <memory>
#include <filesystem>
#include <string.h> // For wcscpy_s

// Path where logs are stored (adjust as needed) - deprecated, using dynamic path now
//static constexpr wchar_t LOGS_PATH[] = L"C:\\";

// URL to your donation page
static constexpr wchar_t DONATE_URL[] = L"https://example.com/donate";

// Tray icon identifiers
static constexpr UINT TRAY_ICON_ID = 2001;
static constexpr UINT TRAY_MESSAGE = WM_APP + 1;

// Global state for the tray icon
static NOTIFYICONDATAW g_nid{};
static HWND              g_trayWindow = nullptr;

// Forward declaration of the window procedure
static LRESULT CALLBACK TrayWndProc(HWND, UINT, WPARAM, LPARAM);

// RAII wrapper for HMENU
struct MenuDeleter {
    void operator()(HMENU h) const noexcept {
        if (h) DestroyMenu(h);
    }
};
using UniqueMenu = std::unique_ptr<std::remove_pointer_t<HMENU>, MenuDeleter>;

bool InitTray(HINSTANCE hInstance, HWND& outHwnd, UINT& outId)
{
    // Register a hidden window class to receive tray callbacks
    WNDCLASSW wc{};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TrayWindowClass";
    if (!RegisterClassW(&wc)) {
        return false;
    }

    // Create a message-only (invisible) window
    g_trayWindow = CreateWindowExW(
        0,
        wc.lpszClassName,
        nullptr, 0, 0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        hInstance,
        nullptr
    );
    if (!g_trayWindow) {
        return false;
    }

    // Prepare NOTIFYICONDATA for our tray icon
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_trayWindow;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = TRAY_MESSAGE;
    g_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP_MAIN));
    wcscpy_s(g_nid.szTip, L"xTended Runtime Detection");

    // Add the icon to the tray
    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        DestroyWindow(g_trayWindow);
        g_trayWindow = nullptr;
        return false;
    }

    outHwnd = g_trayWindow;
    outId = TRAY_ICON_ID;
    return true;
}

void CleanupTray()
{
    if (g_trayWindow) {
        // Remove the icon and destroy our hidden window
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        DestroyWindow(g_trayWindow);
        g_trayWindow = nullptr;
    }
}

// Display an About message box
static void ShowAbout(HWND owner)
{
    MessageBoxW(
        owner,
        L"xTended Runtime Detection\n"
        L"Version 1.0\n\n"
        L"Developers:\n"
        L"  • Ogulcan Ugur\n"
        L"  • Niklas Messerschmid\n",
        L"About",
        MB_ICONINFORMATION | MB_OK
    );
}

static std::wstring GetLogFolder()
{
    wchar_t modulePath[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exePath(modulePath);
    // exakt derselbe Pfad, den XrdLogger anlegt:
    auto logDir = exePath.parent_path()
        / L"xtended Runtime Detection"
        / L"LogFiles";
    // sicherstellen, dass er existiert
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    return logDir.wstring();
}

// Handle tray icon events and menu commands
static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == TRAY_MESSAGE && lParam == WM_RBUTTONUP) {
        // Show context menu at current cursor position
        POINT pt;
        GetCursorPos(&pt);

        UniqueMenu menu{ CreatePopupMenu() };
        InsertMenuW(menu.get(), 0, MF_BYPOSITION, IDM_TRAY_OPENLOGS, L"Open Logs");
        InsertMenuW(menu.get(), 1, MF_BYPOSITION, IDM_TRAY_DONATE, L"Donate");
        InsertMenuW(menu.get(), 2, MF_BYPOSITION, IDM_TRAY_ABOUT, L"About");
        InsertMenuW(menu.get(), 3, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        InsertMenuW(menu.get(), 4, MF_BYPOSITION, IDM_TRAY_EXIT, L"Exit");

        // Ensure the popup closes properly
        SetForegroundWindow(hwnd);
        UINT cmd = TrackPopupMenu(
            menu.get(),
            TPM_RETURNCMD | TPM_RIGHTBUTTON,
            pt.x, pt.y, 0,
            hwnd,
            nullptr
        );

        switch (cmd) {
        case IDM_TRAY_OPENLOGS:
            ShellExecuteW(
                nullptr,
                L"explore",
                GetLogFolder().c_str(),  // dynamisch ermittelter Pfad
                nullptr,
                nullptr,
                SW_SHOWNORMAL
            );
            break;
        case IDM_TRAY_DONATE:
            ShellExecuteW(nullptr, L"open", DONATE_URL, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case IDM_TRAY_ABOUT:
            ShowAbout(hwnd);
            break;
        case IDM_TRAY_EXIT:
            PostQuitMessage(0);
            break;
        default:
            break;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
