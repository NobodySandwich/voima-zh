#include "logger.h"
#include "util.h"
#include <windows.h>
#include <cstdlib>

namespace {
CRITICAL_SECTION g_lock;
bool g_lockInit = false;
}

Logger& Logger::Instance() {
    static Logger logger;
    return logger;
}

void Logger::WriteFileLine(void* handle, const std::string& line) {
    if (!handle || handle == INVALID_HANDLE_VALUE) return;
    EnterCriticalSection(&g_lock);
    DWORD w = 0;
    WriteFile(handle, line.data(), (DWORD)line.size(), &w, nullptr);
    WriteFile(handle, "\r\n", 2, &w, nullptr);
    LeaveCriticalSection(&g_lock);
}

void Logger::Init(const std::string& modDir) {
    dir_ = modDir;
    if (!g_lockInit) { InitializeCriticalSection(&g_lock); g_lockInit = true; }
    std::string logs = modDir + "\\logs";
    CreateDirectoryW(Utf8ToWide(logs).c_str(), nullptr);
    log_ = CreateFileW(Utf8ToWide(logs + "\\plugin.log").c_str(), GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    untranslated_ = CreateFileW(Utf8ToWide(logs + "\\untranslated.log").c_str(), GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    trace_ = CreateFileW(Utf8ToWide(logs + "\\test_trace.tsv").c_str(), GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const char* test = std::getenv("VOIMA_ZH_TEST");
    testMode_ = test && *test && std::string(test) != "0";
    const char* debug = std::getenv("VOIMA_ZH_DEBUG");
    debugMode_ = debug && *debug && std::string(debug) != "0";
    Log("start", "VOIMA_ZH plugin logger initialised dir=" + modDir + " test=" + (testMode_ ? "1" : "0") + " debug=" + (debugMode_ ? "1" : "0"));
}

void Logger::Log(const std::string& tag, const std::string& message) {
    WriteFileLine(log_, "[" + tag + "] " + message);
}

void Logger::Debug(const std::string& tag, const std::string& message) {
    if (debugMode_) WriteFileLine(log_, "[" + tag + "] " + message);
}

void Logger::Trace(const std::string& key, const std::string& source, const std::string& translation) {
    std::string ident = key + "\x1f" + source;
    if (traceSeen_[ident]++ > 0) return;
    WriteFileLine(trace_, "T\t" + EscapeForLog(key) + "\t" + EscapeForLog(source) + "\t" + EscapeForLog(translation));
}

void Logger::Untranslated(const std::string& key, const std::string& source, const std::string& detail) {
    std::string ident = key + "\x1f" + source;
    int& count = untranslatedSeen_[ident];
    ++count;
    if (count != 1 && count != 10 && count != 100) return;
    WriteFileLine(untranslated_, "U\t" + std::to_string(count) + "\t" + EscapeForLog(key) + "\t" +
                                      EscapeForLog(source) + "\t" + EscapeForLog(detail));
}

void Logger::Counters(int buildCalls, int translatedCalls, int uniqueUntranslated, int fontExpanded, int fontRedirects) {
    Log("heartbeat", "build=" + std::to_string(buildCalls) + " translated=" + std::to_string(translatedCalls) +
                         " untranslated_unique=" + std::to_string(uniqueUntranslated) +
                         " font_expanded=" + std::to_string(fontExpanded) +
                         " font_redirects=" + std::to_string(fontRedirects));
}
