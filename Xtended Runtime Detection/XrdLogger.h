// XrdLogger.h
#pragma once

#include <string>
#include <filesystem>

/**
 * @brief Thread-safe logger for XR_D paste events.
 *
 * Records timestamped events to a log file under "<exe_dir>/xtended Runtime Detection/LogFiles".
 * Each entry includes: Time, User, Host, SourceApp, DestApp, Content, Action, Length.
 */
class XrdLogger {
public:
    XrdLogger();

    /**
     * @brief Log an event, always with the full content.
     * @param user       Username performing the action.
     * @param host       Hostname of the machine.
     * @param sourceApp  Originating application name.
     * @param destApp    Destination application name.
     * @param content    Full content of the clipboard.
     * @param action     Description of the action taken.
     */
    void logEvent(const std::wstring& user,
        const std::wstring& host,
        const std::wstring& sourceApp,
        const std::wstring& destApp,
        const std::wstring& content,
        const std::wstring& action);

private:
    void ensureInitialized();

    bool                            _initialized = false;
    std::filesystem::path           _logFilePath;
    static inline std::once_flag    _initFlag;
};
