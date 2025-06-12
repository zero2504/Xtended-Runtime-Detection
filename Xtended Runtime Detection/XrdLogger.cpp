// XrdLogger.cpp
#include "XrdLogger.h"
#include <windows.h>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>

//–– Helper: convert std::wstring → UTF-8 std::string
static std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size_needed = ::WideCharToMultiByte(
        CP_UTF8, 0,
        w.data(), (int)w.size(),
        nullptr, 0,
        nullptr, nullptr
    );
    std::string s;
    s.resize(size_needed);
    ::WideCharToMultiByte(
        CP_UTF8, 0,
        w.data(), (int)w.size(),
        &s[0], size_needed,
        nullptr, nullptr
    );
    return s;
}

XrdLogger::XrdLogger()
{
    std::call_once(_initFlag, [this]() {
        ensureInitialized();
        _initialized = true;
        });
}

void XrdLogger::ensureInitialized()
{
    // 1) EXE-Verzeichnis ermitteln
    wchar_t modulePath[MAX_PATH]{};
    DWORD length = ::GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exePath(modulePath, modulePath + length);
    auto baseDir = exePath.parent_path() / L"xtended Runtime Detection";
    auto logDir = baseDir / L"LogFiles";

    // 2) Ordner anlegen
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    if (ec) {
        throw std::runtime_error("Failed to create log directories: " + ec.message());
    }

    // 3) Log-Datei initialisieren (BOM + Header)
    _logFilePath = logDir / L"xrd_log_file.txt";
    if (!std::filesystem::exists(_logFilePath)) {
        std::ofstream headerStream(_logFilePath, std::ios::binary);
        if (!headerStream) {
            throw std::runtime_error("Unable to create log file at " + _logFilePath.string());
        }
        // UTF-8 BOM
        headerStream.write("\xEF\xBB\xBF", 3);
        // Header
        headerStream << to_utf8(L"==================== XRD Log File ====================\n\n");
    }
}

void XrdLogger::logEvent(const std::wstring& user,
    const std::wstring& host,
    const std::wstring& sourceApp,
    const std::wstring& destApp,
    const std::wstring& content,
    const std::wstring& action)
{
    // Ensure initialization
    if (!_initialized) {
        std::call_once(_initFlag, [this]() {
            ensureInitialized();
            _initialized = true;
            });
    }

    // 1) Timestamp erzeugen
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::wostringstream wtime;
    wtime << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S");

    // 2) Atomar an Log anhängen
    static std::mutex fileMutex;
    std::lock_guard<std::mutex> lock(fileMutex);

    std::ofstream out(_logFilePath, std::ios::binary | std::ios::app);
    if (!out) {
        throw std::runtime_error("Failed to open log file for appending.");
    }

    std::ostringstream oss;
    // Separator
    oss << to_utf8(L"-------------------------------------------------------\n");
    // Fields
    oss << to_utf8(L"Time       : ") << to_utf8(wtime.str()) << "\n"
        << to_utf8(L"User       : ") << to_utf8(user) << "\n"
        << to_utf8(L"Host       : ") << to_utf8(host) << "\n"
        << to_utf8(L"SourceApp  : ") << to_utf8(sourceApp) << "\n"
        << to_utf8(L"DestApp    : ") << to_utf8(destApp) << "\n"
        << to_utf8(L"Content    : ") << to_utf8(content) << "\n"
        << to_utf8(L"Action     : ") << to_utf8(action) << "\n"
        << to_utf8(L"Length     : ") << std::to_string(content.size()) << "\n\n";

    out << oss.str();
}
