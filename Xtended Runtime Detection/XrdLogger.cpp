// XrdLogger.cpp
#include "XrdLogger.h"
#include <windows.h>
#include <sstream>
#include <iomanip>

//----------------------------------------------------------------------------
// Helper: Convert std::wstring → UTF-8 std::string
//----------------------------------------------------------------------------
static std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size_needed = ::WideCharToMultiByte(
        CP_UTF8, 0,
        w.data(), (int)w.size(),
        nullptr, 0,
        nullptr, nullptr
    );
    std::string s(size_needed, '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0,
        w.data(), (int)w.size(),
        s.data(), size_needed,
        nullptr, nullptr
    );
    return s;
}

//----------------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------------
XrdLogger::XrdLogger() {
    // Defer heavy setup until first logEvent
    std::call_once(_initFlag, [this]() {
        ensureInitialized();
        _initialized = true;
        });
}

//----------------------------------------------------------------------------
// Public API
//----------------------------------------------------------------------------
void XrdLogger::logEvent(const std::wstring& user,
    const std::wstring& host,
    const std::wstring& sourceApp,
    const std::wstring& destApp,
    std::wstring content,
    const std::wstring& action)
{
    // Ensure we've initialized once
    if (!_initialized) {
        std::call_once(_initFlag, [this]() {
            ensureInitialized();
            _initialized = true;
            });
    }

    // Cap content length to avoid out-of-memory or huge logs
    if (content.size() > MAX_CONTENT_LENGTH) {
        content.resize(MAX_CONTENT_LENGTH);
        content += L"\n…(truncated)…\n";
    }

    try {
        std::lock_guard lock(_fileMutex);

        // Timestamp
        std::string ts = formatTimestamp();

        // Stream fields directly to the open file
        _logStream
            << "-------------------------------------------------------\n"
            << "Time       : " << ts << "\n"
            << "User       : " << to_utf8(user) << "\n"
            << "Host       : " << to_utf8(host) << "\n"
            << "SourceApp  : " << to_utf8(sourceApp) << "\n"
            << "DestApp    : " << to_utf8(destApp) << "\n"
            << "Content    : "  // label
            << to_utf8(content) << "\n"
            << "Action     : " << to_utf8(action) << "\n"
            << "Length     : " << content.size() << "\n\n";

        _logStream.flush();  // ensure it's on disk
    }
    catch (const std::exception& e) {
        // If logging fails, show a system-modal message box
        MessageBoxW(nullptr,
            (L"XRD Logger write error:\n" + std::wstring(e.what(), e.what() + strlen(e.what()))).c_str(),
            L"Logging Error",
            MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
    }
}

//----------------------------------------------------------------------------
// One-time setup: create dirs, rotate old log, write BOM/header, open stream
//----------------------------------------------------------------------------
void XrdLogger::ensureInitialized()
{
    // Determine executable folder
    wchar_t modulePath[MAX_PATH]{};
    DWORD len = ::GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exeDir(modulePath, modulePath + len);
    auto baseDir = exeDir.parent_path() / L"xtended Runtime Detection";
    auto logDir = baseDir / L"LogFiles";

    // Create directories
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    if (ec) {
        throw std::runtime_error("Failed to create log directories: " + ec.message());
    }

    // Prepare log file path
    _logFilePath = logDir / L"xrd_log_file.txt";

    // Rotate if it's too big
    rotateLogIfNeeded();

    // If brand-new, write BOM + header
    bool isNew = !std::filesystem::exists(_logFilePath);
    if (isNew) {
        std::ofstream header(_logFilePath, std::ios::binary);
        if (!header) {
            throw std::runtime_error("Unable to create log file at " + _logFilePath.string());
        }
        // UTF-8 BOM
        header.write("\xEF\xBB\xBF", 3);
        // Header line
        header << to_utf8(L"==================== XRD Log File ====================\n\n");
    }

    // Open the stream for all future appends
    _logStream.open(_logFilePath, std::ios::binary | std::ios::app);
    if (!_logStream) {
        throw std::runtime_error("Unable to open log file for appending: " + _logFilePath.string());
    }
}

//----------------------------------------------------------------------------
// If the log exceeds MAX_LOG_SIZE, rename it with a timestamp suffix.
//----------------------------------------------------------------------------
void XrdLogger::rotateLogIfNeeded()
{
    if (std::filesystem::exists(_logFilePath)) {
        auto size = std::filesystem::file_size(_logFilePath);
        if (size > MAX_LOG_SIZE) {
            // Generate timestamp for backup filename
            auto now = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            localtime_s(&tm, &tt);
            std::wostringstream woss;
            woss << std::put_time(&tm, L"%Y%m%d_%H%M%S");

            auto backup = _logFilePath.parent_path()
                / (L"xrd_log_" + woss.str() + L".txt");
            std::filesystem::rename(_logFilePath, backup);
        }
    }
}

//----------------------------------------------------------------------------
// Return current local time as "YYYY-MM-DD HH:MM:SS"
//----------------------------------------------------------------------------
std::string XrdLogger::formatTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
