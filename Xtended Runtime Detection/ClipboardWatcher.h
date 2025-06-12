#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <regex>

#include "XrdLogger.h"

#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE 0x031D
#endif

/**
 * @class ClipboardWatcher
 * @brief Monitors the Windows clipboard for suspicious patterns and intercepts paste operations.
 *
 * Loads regex patterns from a file, listens to clipboard updates, and prompts the user
 * to confirm or discard content matching any pattern. Logs events via XrdLogger.
 */
class ClipboardWatcher
{
public:
    /**
     * @brief Constructs the watcher with the given pattern file path.
     * @param patternFile Path to the file containing regex patterns.
     */
    explicit ClipboardWatcher(std::wstring patternFile);

    /**
     * @brief Destructor; stops watching and cleans up resources.
     */
    ~ClipboardWatcher();

    /**
     * @brief Initializes the watcher.
     * @param inst The HINSTANCE of the application.
     * @param trayHwnd The window handle owning the notification icon.
     * @param trayId The ID of the tray icon.
     * @return True if initialization succeeded.
     */
    bool Start(HINSTANCE inst, HWND trayHwnd, UINT trayId);

    /**
     * @brief Stops watching clipboard updates and removes hooks.
     */
    void Stop();

    /**
     * @brief Forces an initial scan of the current clipboard content.
     */
    void ForceInitialScan();

private:
    // Helper functions

    /** @brief Loads regex patterns from the configured file. */
    bool LoadPatterns();

    /**
     * @brief Checks if the given text matches any loaded pattern.
     * @param txt Clipboard text to check.
     * @return True if any pattern matches.
     */
    bool ContainsBad(const std::wstring& txt) const;

    /** @brief Creates a hidden message-only window for receiving clipboard events. */
    bool CreateMsgWindow(HINSTANCE inst);

    /** @brief Called when the clipboard content changes. */
    void OnClipboardUpdate();

    /** @brief Installs keyboard and mouse hooks to intercept actions. */
    void InstallHooks();

    /** @brief Uninstalls keyboard and mouse hooks. */
    void UninstallHooks();

    /**
     * @brief Logs the final paste action to the logger.
     * @param destApp The destination application name.
     */
    void LogFinalPaste(const std::wstring& destApp);

    // Callback procedures

    /** @brief Window procedure for the hidden message window. */
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    /** @brief Low-level keyboard hook procedure. */
    static LRESULT CALLBACK LLKBProc(int code, WPARAM wParam, LPARAM lParam);

    // Static members

    static ClipboardWatcher* s_this; ///< Pointer to current instance for hooks
    static HHOOK s_kbHook;           ///< Handle to the keyboard hook
    static HHOOK s_mouseHook;        ///< Handle to the mouse hook

    // Configuration

    std::wstring _patternFile;         ///< Path to regex pattern file
    std::vector<std::wregex> _patterns;///< Compiled regex patterns

    // Runtime state

    HWND _hWnd = nullptr;             ///< Hidden window for message loop
    HWND _trayHwnd = nullptr;         ///< Tray icon owner window
    UINT _trayID = 0;                 ///< Tray icon ID

    bool _decisionPending = false;    ///< True if user confirmation dialog is open
    bool _awaitPaste = false;         ///< True if awaiting a single paste action
    bool _tokenUsed = false;          ///< True if the allowed paste has occurred
    bool _holdClipboard = false;      ///< True to ignore nested clipboard events

    std::wstring _suspicious;         ///< Cached suspicious text preview
    std::wstring _srcApp;             ///< Source application of clipboard text
    std::wstring _preview;            ///< Preview of the clipboard content
    std::wstring _user;               ///< Username for logging
    std::wstring _host;               ///< Hostname for logging
	std::wstring _fullContent;        /// Full content of the clipboard

    XrdLogger _logger;                ///< Logger for events
};
