// XrdLogger.h
#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>

/**
 * @brief Thread-safe, resilient logger for XR_D paste events.
 *
 * - Writes UTF-8 (with BOM) log entries under:
 *     <exe_dir>/xtended Runtime Detection/LogFiles/xrd_log_file.txt
 * - Rotates the log file when it exceeds 100 MB.
 * - Streams fields directly, catching all exceptions.
 */
class XrdLogger {
public:
    XrdLogger();

    /**
     * @brief Log an event, always with the full (or capped) content.
     * @param user       Username performing the action.
     * @param host       Hostname of the machine.
     * @param sourceApp  Originating application name.
     * @param destApp    Destination application name.
     * @param content    Full content of the clipboard (capped internally).
     * @param action     Description of the action taken.
     */
    void logEvent(const std::wstring& user,
        const std::wstring& host,
        const std::wstring& sourceApp,
        const std::wstring& destApp,
        std::wstring content,
        const std::wstring& action);

private:
    void ensureInitialized();            // called once to set up dirs, BOM/header, rotation, open stream
    void rotateLogIfNeeded();            // moves old log aside if too large
    std::string formatTimestamp();       // YYYY-MM-DD HH:MM:SS

    std::filesystem::path _logFilePath;
    std::ofstream         _logStream;    // kept open for appends
    std::mutex            _fileMutex;    // protects _logStream

    bool                  _initialized = false;
    static inline std::once_flag _initFlag;

    // thresholds
    static constexpr std::uintmax_t MAX_LOG_SIZE = 100ULL * 1024 * 1024; // 100 MB
    static constexpr size_t         MAX_CONTENT_LENGTH = 50ULL * 1024 * 1024; // 50 MB
};
