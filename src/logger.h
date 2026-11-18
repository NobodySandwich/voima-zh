#pragma once
#include <string>
#include <unordered_map>

class Logger {
public:
    static Logger& Instance();
    void Init(const std::string& modDir);
    void Log(const std::string& tag, const std::string& message);
    void Trace(const std::string& key, const std::string& source, const std::string& translation);
    void Untranslated(const std::string& key, const std::string& source, const std::string& detail);
    void Counters(int buildCalls, int translatedCalls, int uniqueUntranslated, int fontExpanded, int fontRedirects);
    int UniqueUntranslated() const { return (int)untranslatedSeen_.size(); }
    const std::string& Dir() const { return dir_; }
    bool TestMode() const { return testMode_; }

private:
    Logger() = default;
    void WriteFileLine(void* handle, const std::string& line);
public:
    std::string dir_;
    void* log_ = nullptr;
    void* untranslated_ = nullptr;
    void* trace_ = nullptr;
    bool testMode_ = false;
    mutable std::unordered_map<std::string, int> untranslatedSeen_;
    std::unordered_map<std::string, int> traceSeen_;
};
