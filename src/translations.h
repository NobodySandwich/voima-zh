#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct TranslationEntry {
    std::string key;
    std::string source;
    std::string translation;
    bool pattern = false;
    std::vector<std::string> literals;
    std::vector<int> placeholders;
    int page = -1;
};

struct FontPage {
    std::vector<uint16_t> chars;
    std::vector<size_t> entries;
};

class TranslationDb {
public:
    bool Load(const std::string& path);
    void AssignPages(size_t pageCapacity);
    bool Lookup(const std::vector<std::string>& keys, const std::string& input,
                std::string& output, std::string& matchedKey, std::string& matchedSource,
                int* page = nullptr) const;
    size_t size() const { return entries_.size(); }
    size_t keyCount() const { return index_.size(); }
    size_t pageCount() const { return pages_.size(); }
    std::vector<int> PagesForPrefix(const std::string& prefix) const;
    const std::vector<uint16_t>& pageChars(size_t page) const { return pages_[page].chars; }
    const std::vector<size_t>& pageEntries(size_t page) const { return pages_[page].entries; }
    const std::string& lastError() const { return lastError_; }

private:
    static bool MatchEntry(const TranslationEntry& e, const std::string& normalized,
                           std::string& translated);
    std::vector<TranslationEntry> entries_;
    std::unordered_map<std::string, std::vector<size_t>> index_;
    std::vector<FontPage> pages_;
    std::string lastError_;
};
