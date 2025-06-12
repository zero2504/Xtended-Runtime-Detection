// XrdLogger.cpp
#include "XrdLogger.h"
#include <windows.h>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <mutex>

XrdLogger::XrdLogger()
{
    // Ensure initialization happens only once, even in multithreaded contexts
    std::call_once(_initFlag, [this]() {
        ensureInitialized();
        _initialized = true;
        });
}

void XrdLogger::ensureInitialized()
{
    // 1) Determine the directory of the current executable
    wchar_t modulePath[MAX_PATH]{};
    DWORD length = ::GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exePath(modulePath, modulePath + length);
    std::filesystem::path baseDir = exePath.parent_path() / L"xtended Runtime Detection";
    std::filesystem::path logDir = baseDir / L"LogFiles";

    // 2) Create directories if they do not exist
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    if (ec) {
        throw std::runtime_error("Failed to create log directories: " + ec.message());
    }

    // 3) Initialize log file path
    _logFilePath = logDir / L"xrd_log_file.txt";
    if (!std::filesystem::exists(_logFilePath)) {
        std::wofstream headerStream(_logFilePath, std::ios::out);
        if (!headerStream) {
            throw std::runtime_error("Unable to create log file at " + _logFilePath.string());
        }
        headerStream << L"==================== XRD Log File ===================="
            << L"\n\n";
    }
}

void XrdLogger::logEvent(const std::wstring& user,
    const std::wstring& host,
    const std::wstring& sourceApp,
    const std::wstring& destApp,
    const std::wstring& content,
    const std::wstring& action,
    size_t maxLen)
{
    // Ensure initialization (idempotent)
    if (!_initialized) {
        std::call_once(_initFlag, [this]() {
            ensureInitialized();
            _initialized = true;
            });
    }

    // 1) Generate timestamp
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &in_time_t);
    std::wostringstream timeStream;
    timeStream << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S");

    // 2) Prepare content preview
    std::wstring preview = content;
    if (preview.size() > maxLen) {
        preview.resize(maxLen);
        preview += L"...";
    }
    size_t length = preview.size();

    // 3) Write entry atomically
    static std::mutex fileMutex;
    std::lock_guard<std::mutex> lock(fileMutex);
    std::wofstream outFile(_logFilePath, std::ios::app);
    if (!outFile) {
        throw std::runtime_error("Failed to open log file for appending.");
    }

    outFile << L"Time       : " << timeStream.str() << L"\n"
        << L"User       : " << user << L"\n"
        << L"Host       : " << host << L"\n"
        << L"SourceApp  : " << sourceApp << L"\n"
        << L"DestApp    : " << destApp << L"\n"
        << L"Content    : " << preview << L"\n"
        << L"Action     : " << action << L"\n"
        << L"Length     : " << length << L"\n"
        << L"-------------------------------------------------------"
        << L"\n\n";
}