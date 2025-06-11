/**
 * @file ClipboardWatcher.cpp
 * @brief Implements the ClipboardWatcher class for detecting and handling suspicious clipboard content.
 *
 * Loads regex patterns, listens for clipboard changes, prompts the user,
 * and logs actions via XrdLogger.
 *
 * @note Consider minimizing clipboard lock duration to reduce potential UI blocking.
 */

#ifndef UNICODE
#   define UNICODE
#endif

#pragma comment(linker,                                  \
    "\"/manifestdependency:type='win32' "             \
    "name='Microsoft.Windows.Common-Controls' "         \
    "version='6.0.0.0' "                                \
    "processorArchitecture='*' "                        \
    "publicKeyToken='6595b64144ccf1df' "                \
    "language='*'\"")

#include "ClipboardWatcher.h"

#include <Lmcons.h>
#include <psapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <fstream>
#include <sstream>
#include <Shlwapi.h>    // for PathFindExtensionW

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "Shlwapi.lib")
 //------------------------------------------------------------------------------
 // Static member definitions
 //------------------------------------------------------------------------------
ClipboardWatcher* ClipboardWatcher::s_this = nullptr;
HHOOK             ClipboardWatcher::s_kbHook = nullptr;
HHOOK             ClipboardWatcher::s_mouseHook = nullptr;

namespace {
    /**
     * @brief Displays an error task dialog.
     * @param owner Owner window handle (nullptr for no owner).
     * @param message Wide-string message text.
     */
    void ShowError(HWND owner, const wchar_t* message)
    {
        TaskDialog(owner, nullptr, L"Error", message,
            nullptr, TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
    }

    /**
     * @brief Prompts a Yes/No dialog.
     * @param owner Owner window handle.
     * @param title Dialog title.
     * @param text Main instruction.
     * @param footer Optional footer text.
     * @return IDYES or IDNO.
     */
    int AskYesNo(HWND owner,
        const wchar_t* title,
        const wchar_t* text,
        const wchar_t* footer = nullptr)
    {
        int result = 0;
        TaskDialog(owner, nullptr, title, text, footer,
            TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
            TD_WARNING_ICON, &result);
        return result;
    }

    /**
     * @brief Checks for Ctrl-based copy/paste keys.
     * @param vk Virtual key code.
     * @param ctrl Ctrl key state.
     * @param shift Shift key state.
     * @return true if it's a copy, cut, or paste shortcut.
     */
    bool IsCopyCutPaste(DWORD vk, bool ctrl, bool shift)
    {
        if (!ctrl)
            return false;
        switch (vk) {
        case 'C': case 'X': case 'V':
            return true;
        default:
            return shift && (vk == VK_INSERT);
        }
    }
} // anonymous namespace

//------------------------------------------------------------------------------
// Constructor / Destructor
//------------------------------------------------------------------------------
ClipboardWatcher::ClipboardWatcher(std::wstring patternFile)
    : _patternFile(std::move(patternFile))
{
}

ClipboardWatcher::~ClipboardWatcher()
{
    Stop();
}

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------
bool ClipboardWatcher::Start(HINSTANCE instance,
    HWND trayHwnd,
    UINT trayId)
{
    _trayHwnd = trayHwnd;
    _trayID = trayId;

    // Initialize common controls once
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    if (!LoadPatterns())                 return false;
    if (!CreateMsgWindow(instance))      return false;

    // Cache user and host names for logging
    wchar_t userBuffer[UNLEN + 1] = {};
    DWORD userLen = UNLEN + 1;
    GetUserNameW(userBuffer, &userLen);
    _user.assign(userBuffer, userLen - 1);

    wchar_t hostBuffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD hostLen = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(hostBuffer, &hostLen);
    _host.assign(hostBuffer, hostLen);

    s_this = this;
    return true;
}

void ClipboardWatcher::Stop()
{
    UninstallHooks();
    if (_hWnd) {
        RemoveClipboardFormatListener(_hWnd);
        DestroyWindow(_hWnd);
    }
    _hWnd = nullptr;
    _patterns.clear();  // free regex objects
    s_this = nullptr;
}

void ClipboardWatcher::ForceInitialScan()
{
    OnClipboardUpdate();
}

//------------------------------------------------------------------------------
// Pattern handling
//------------------------------------------------------------------------------
bool ClipboardWatcher::LoadPatterns()
{
    std::wifstream infile(_patternFile);
    if (!infile.is_open()) {
        ShowError(nullptr, L"Pattern file not found");
        return false;
    }

    std::wstring line;
    size_t lineNumber = 0;
    std::vector<std::wstring> patternStrings;
    while (std::getline(infile, line)) {
        ++lineNumber;
        // Trim whitespace and strip comments
        const auto first = line.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) continue;

        const auto commentPos = line.find(L'#', first);
        std::wstring raw = (commentPos == std::wstring::npos)
            ? line.substr(first)
            : line.substr(first, commentPos - first);

        const auto last = raw.find_last_not_of(L" \t\r\n");
        if (last == std::wstring::npos) continue;
        raw.resize(last + 1);

        // Remove inline case-insensitive flag
        if (raw.rfind(L"(?i)", 0) == 0)
            raw.erase(0, 4);

        if (raw.empty()) continue;

        auto tryCompile = [&](const std::wstring& pattern) {
            try {
                _patterns.emplace_back(pattern, std::regex_constants::icase);
                return true;
            }
            catch (...) {
                return false;
            }
            };

        if (tryCompile(raw))
            continue;

        // Escape braces and retry
        std::wstring escaped;
        escaped.reserve(raw.size() * 2);
        for (wchar_t ch : raw) {
            if (ch == L'{' || ch == L'}')
                escaped.push_back(L'\\');
            escaped.push_back(ch);
        }
        if (!tryCompile(escaped)) {
            std::wstringstream err;
            err << L"Invalid regex (line " << lineNumber << L"): " << raw;
            ShowError(nullptr, err.str().c_str());
        }
    }

    if (_patterns.empty()) {
        ShowError(nullptr, L"No valid patterns loaded");
        return false;
    }
    return true;
}

bool ClipboardWatcher::ContainsBad(const std::wstring& text) const
{
    // Suggestion: For high-performance, consider combining patterns into a single regex
    for (const auto& regex : _patterns) {
        if (std::regex_search(text, regex))
            return true;
    }
    return false;
}

//------------------------------------------------------------------------------
// Message-only window
//------------------------------------------------------------------------------
bool ClipboardWatcher::CreateMsgWindow(HINSTANCE instance)
{
    constexpr auto WindowClassName = L"XRD_ClipWatcherWnd";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = WindowClassName;

    if (!RegisterClassW(&wc))
        return false;  // TODO: Log error

    _hWnd = CreateWindowExW(0, WindowClassName, nullptr, 0,
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr,
        instance, this);
    return (_hWnd && AddClipboardFormatListener(_hWnd));
}

LRESULT CALLBACK ClipboardWatcher::WndProc(HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    if (msg == WM_CREATE) {
        const auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* self = reinterpret_cast<ClipboardWatcher*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (self && msg == WM_CLIPBOARDUPDATE) {
        self->OnClipboardUpdate();
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

//------------------------------------------------------------------------------
// Clipboard update handler
//------------------------------------------------------------------------------
void ClipboardWatcher::OnClipboardUpdate()
{
    // If we're already holding the clipboard due to a pending decision, ignore nested events
    if (_holdClipboard)
        return;

    // Try to open the clipboard; if it fails, bail out
    if (!OpenClipboard(_hWnd))
        return;

    bool suspicious = false;
    std::wstring snippet;
    const size_t kFileReadLimit = 16 * 1024;       // maximum 16 KB per file
    const double kLsbLower = 0.4,                  // LSB anomaly detection thresholds
        kLsbUpper = 0.6;

    //
    // A) Check for Unicode text
    //
    if (!suspicious)
    {
        if (HANDLE hText = GetClipboardData(CF_UNICODETEXT))
        {
            if (const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(hText)))
            {
                // Copy clipboard text into a std::wstring
                std::wstring content(data);
                GlobalUnlock(hText);

                // If the text contains suspicious patterns, mark it
                if (ContainsBad(content))
                {
                    // Take a snippet of up to 100 characters for preview
                    snippet = content.substr(0, 100)
                        + (content.size() > 100 ? L"…" : L"");
                    suspicious = true;
                }
            }
        }
    }

    //
    // B) Check for file drops (CF_HDROP) - Experimental
    /*
    if (!suspicious)
    {
        if (HANDLE hDrop = GetClipboardData(CF_HDROP))
        {
            // How many files are on the clipboard?
            UINT count = DragQueryFileW((HDROP)hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count && !suspicious; ++i)
            {
                wchar_t path[MAX_PATH] = {};
                if (DragQueryFileW((HDROP)hDrop, i, path, MAX_PATH))
                {
                    // Only scan certain extensions
                    std::wstring ext = PathFindExtensionW(path);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    static const std::vector<std::wstring> allowed = {
                        L".txt", L".log", L".csv", L".json", L".xml", L".svg",
                        L".ps1", L".bat", L".sh",  L".docx", L".xlsx", L".html"
                    };
                    if (std::find(allowed.begin(), allowed.end(), ext)
                        == allowed.end())
                        continue;  // skip executables, DLLs, images, etc.

                    // Read up to kFileReadLimit bytes from the file
                    std::wifstream in(path, std::ios::binary);
                    if (!in)
                        continue;
                    std::wstring buffer;
                    buffer.resize(kFileReadLimit);
                    in.read(&buffer[0], buffer.size());
                    buffer.resize(in.gcount());

                    // Check for suspicious content inside the file
                    if (ContainsBad(buffer))
                    {
                        snippet = buffer.substr(0, 100)
                            + (buffer.size() > 100 ? L"…" : L"");
                        suspicious = true;
                    }
                }
            }
        }
    }*/

    //
    // C) (Optional) Check for images (CF_DIB)
    //    This section is currently commented out. It would:
    //    1) Scan image metadata header (up to 64 KB) for bad patterns
    //    2) Perform an LSB entropy check to detect steganography
    /*
    if (!suspicious)
    {
        if (HANDLE hDib = GetClipboardData(CF_DIB))
        {
            if (void* ptr = GlobalLock(hDib))
            {
                SIZE_T size = GlobalSize(hDib);
                std::vector<BYTE> data((BYTE*)ptr, (BYTE*)ptr + size);
                GlobalUnlock(hDib);

                // 1) Check metadata header
                size_t metaLen = std::min<size_t>(data.size(), 64 * 1024);
                std::wstring header(reinterpret_cast<wchar_t*>(data.data()),
                    metaLen / sizeof(wchar_t));
                if (ContainsBad(header))
                {
                    snippet = L"[Image metadata]";
                    suspicious = true;
                }

                // 2) LSB anomaly check
                if (!suspicious)
                {
                    size_t zeros = 0, ones = 0;
                    const size_t offset = sizeof(BITMAPINFOHEADER);
                    for (size_t i = offset; i + 3 < data.size(); i += 4)
                    {
                        zeros += ((data[i]   & 1) == 0);
                        zeros += ((data[i+1] & 1) == 0);
                        zeros += ((data[i+2] & 1) == 0);
                        ones  += ((data[i]   & 1) == 1);
                        ones  += ((data[i+1] & 1) == 1);
                        ones  += ((data[i+2] & 1) == 1);
                    }
                    double ratio = double(ones) / (zeros + ones);
                    if (ratio > kLsbLower && ratio < kLsbUpper)
                    {
                        snippet = L"[Image LSB anomaly]";
                        suspicious = true;
                    }
                }
            }
            else
            {
                GlobalUnlock(hDib);
            }
        }
    }
    */

    // Release the clipboard
    CloseClipboard();

    // If nothing suspicious was found, we're done
    if (!suspicious)
        return;

    //
    // Original workflow upon detection:
    //   - Install hooks to intercept next paste
    //   - Show dialog asking whether to discard or allow paste
    //   - Log the event
    //
    _holdClipboard = true;
    _decisionPending = true;
    InstallHooks();

    _suspicious = snippet;
    _preview = snippet;

    // Determine the source application
    _srcApp = L"Unknown";
    if (HWND owner = GetClipboardOwner())
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(owner, &pid);
        if (auto proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, pid))
        {
            wchar_t buf[MAX_PATH] = {};
            DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(proc, 0, buf, &len))
            {
                std::wstring full(buf, len);
                auto pos = full.find_last_of(L"\\/");
                _srcApp = (pos == std::wstring::npos)
                    ? full
                    : full.substr(pos + 1);
            }
            CloseHandle(proc);
        }
    }

    // Ask the user whether to discard the suspicious content
    int choice = AskYesNo(
        _hWnd,
        L"Security Alert – Extended Runtime Detection",
        L"Suspicious clipboard content detected.\nDiscard?",
        _preview.c_str()
    );

    if (choice == IDNO)
    {
        // User chose to discard: clear the clipboard and notify
        if (OpenClipboard(_hWnd))
        {
            EmptyClipboard();
            CloseClipboard();
        }
        NOTIFYICONDATA nid{ sizeof(nid) };
        nid.hWnd = _trayHwnd;
        nid.uID = _trayID;
        nid.uFlags = NIF_INFO;
        wcscpy_s(nid.szInfoTitle, L"Clipboard verdict");
        wcscpy_s(nid.szInfo, L"Content discarded.");
        nid.dwInfoFlags = NIIF_ERROR;
        Shell_NotifyIconW(NIM_MODIFY, &nid);

        _logger.logEvent(_user, _host, _srcApp,
            L"N/A", _preview, L"Discard");

        _holdClipboard = _decisionPending = false;
        UninstallHooks();
    }
    else
    {
        // User chose to allow: notify to paste and wait for next paste action
        NOTIFYICONDATA nid{ sizeof(nid) };
        nid.hWnd = _trayHwnd;
        nid.uID = _trayID;
        nid.uFlags = NIF_INFO;
        wcscpy_s(nid.szInfoTitle, L"Clipboard verdict");
        wcscpy_s(nid.szInfo, L"Paste now (Ctrl+V / Shift+Ins / right-click).");
        //nid.dwInfoFlags = NIIF_INFO;
        nid.dwInfoFlags = NIIF_USER;
        Shell_NotifyIconW(NIM_MODIFY, &nid);

        _decisionPending = false;
        _awaitPaste = true;
        _tokenUsed = false;
    }
}



//------------------------------------------------------------------------------
// Hook management
//------------------------------------------------------------------------------
void ClipboardWatcher::InstallHooks()
{
    if (!s_kbHook) {
        s_kbHook = SetWindowsHookExW(
            WH_KEYBOARD_LL,
            LLKBProc,
            GetModuleHandle(nullptr),
            0);
        // TODO: check for failure and log
    }
    if (!s_mouseHook) {
        s_mouseHook = SetWindowsHookExW(
            WH_MOUSE_LL,
            [](int code, WPARAM wp, LPARAM lp) -> LRESULT {
                if (code == HC_ACTION && s_this) {
                    auto* self = s_this;
                    // Block clicks during decision dialog
                    if (self->_decisionPending &&
                        (wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP ||
                            wp == WM_MBUTTONDOWN || wp == WM_MBUTTONUP))
                        return 1;
                    // Handle single allowed paste
                    if (self->_awaitPaste) {
                        if (self->_tokenUsed &&
                            (wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP ||
                                wp == WM_MBUTTONDOWN || wp == WM_MBUTTONUP))
                            return 1;
                        if (wp == WM_RBUTTONUP) {
                            // TODO: factor out common paste-finalization code
                            self->_tokenUsed = true;
                            self->UninstallHooks();
                            // Restore clipboard and log paste
                            if (OpenClipboard(self->_hWnd)) {
                                HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE,
                                    (self->_suspicious.size() + 1) * sizeof(wchar_t));
                                if (auto ptr = GlobalLock(mem)) {
                                    memcpy(ptr, self->_suspicious.c_str(),
                                        (self->_suspicious.size() + 1) * sizeof(wchar_t));
                                    GlobalUnlock(mem);
                                    EmptyClipboard();
                                    SetClipboardData(CF_UNICODETEXT, mem);
                                }
                                CloseClipboard();
                            }
                            // Determine destination app and log
                            POINT pt; GetCursorPos(&pt);
                            HWND destWin = WindowFromPoint(pt);
                            std::wstring destName = L"Unknown";
                            if (destWin) {
                                DWORD pid = 0;
                                GetWindowThreadProcessId(destWin, &pid);
                                if (auto proc = OpenProcess(
                                    PROCESS_QUERY_LIMITED_INFORMATION,
                                    FALSE, pid)) {
                                    wchar_t path[MAX_PATH] = {};
                                    DWORD len = MAX_PATH;
                                    if (QueryFullProcessImageNameW(
                                        proc, 0, path, &len)) {
                                        std::wstring full(path, len);
                                        auto pos = full.find_last_of(L"/\\");
                                        destName = (pos == std::wstring::npos) ?
                                            full : full.substr(pos + 1);
                                    }
                                    CloseHandle(proc);
                                }
                            }
                            self->LogFinalPaste(destName);
                            return 1;
                        }
                        // Block middle-click paste
                        if (wp == WM_MBUTTONDOWN || wp == WM_MBUTTONUP)
                            return 1;
                    }
                }
                return CallNextHookEx(s_mouseHook, code, wp, lp);
            },
            GetModuleHandle(nullptr),
            0);
        // TODO: check for failure and log
    }
}

void ClipboardWatcher::UninstallHooks()
{
    if (s_kbHook) {
        UnhookWindowsHookEx(s_kbHook);
        s_kbHook = nullptr;
    }
    if (s_mouseHook) {
        UnhookWindowsHookEx(s_mouseHook);
        s_mouseHook = nullptr;
    }
    _awaitPaste = false;
    _tokenUsed = false;
    _decisionPending = false;
}

//------------------------------------------------------------------------------
// Low-level keyboard hook
//------------------------------------------------------------------------------
LRESULT CALLBACK ClipboardWatcher::LLKBProc(int code,
    WPARAM wParam,
    LPARAM lParam)
{
    if (code == HC_ACTION && s_this) {
        auto* self = s_this;
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        if (self->_decisionPending &&
            IsCopyCutPaste(kb->vkCode, ctrl, shift))
            return 1;

        if (self->_awaitPaste &&
            IsCopyCutPaste(kb->vkCode, ctrl, shift)) {
            if (self->_tokenUsed)
                return 1;
            self->_tokenUsed = true;
            self->UninstallHooks();
            // Determine destination and log
            HWND fg = GetForegroundWindow();
            std::wstring dest = L"Unknown";
            if (fg) {
                DWORD pid = 0;
                GetWindowThreadProcessId(fg, &pid);
                if (auto proc = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE, pid)) {
                    wchar_t path[MAX_PATH] = {};
                    DWORD len = MAX_PATH;
                    if (QueryFullProcessImageNameW(proc, 0, path, &len)) {
                        std::wstring full(path, len);
                        auto pos = full.find_last_of(L"/\\");
                        dest = (pos == std::wstring::npos) ? full
                            : full.substr(pos + 1);
                    }
                    CloseHandle(proc);
                }
            }
            self->LogFinalPaste(dest);
            return 1;
        }
    }
    return CallNextHookEx(s_kbHook, code, wParam, lParam);
}

//------------------------------------------------------------------------------
// Final log
//------------------------------------------------------------------------------
void ClipboardWatcher::LogFinalPaste(const std::wstring& destApp)
{
    _logger.logEvent(_user, _host, _srcApp, destApp, _preview, L"Keep");
    _holdClipboard = false;
}

// End of ClipboardWatcher.cpp
