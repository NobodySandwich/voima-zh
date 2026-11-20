#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
namespace rfp {
namespace fs = std::filesystem;
inline std::wstring U8(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
inline void Restore(const std::wstring& modDir) {
    auto root = fs::path(modDir).parent_path();
    auto font = root / L"VOIMA_Data" / L"StreamingAssets" / L"Font";
    auto backup = fs::path(modDir) / L".runtime_font_backup";
    auto journal = fs::path(modDir) / L".runtime_font_journal";
    std::ifstream in(journal, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto name = U8(line);
        auto target = font / name;
        auto saved = backup / name;
        std::error_code ec;
        fs::remove(target, ec);
        if (fs::exists(saved, ec)) fs::copy_file(saved, target, fs::copy_options::overwrite_existing, ec);
    }
    in.close();
    std::error_code ec;
    fs::remove_all(backup, ec);
    fs::remove(journal, ec);
}
inline bool Prepare(const std::wstring& modDir) {
    auto root = fs::path(modDir).parent_path();
    auto font = root / L"VOIMA_Data" / L"StreamingAssets" / L"Font";
    auto srcFont = fs::path(modDir) / L"Font";
    auto backup = fs::path(modDir) / L".runtime_font_backup";
    auto journal = fs::path(modDir) / L".runtime_font_journal";
    Restore(modDir);
    std::ofstream dbg(fs::path(modDir) / L"launcher_font_debug.log", std::ios::trunc);
    dbg << "font=" << font.string() << " src=" << srcFont.string() << std::endl;
    std::ifstream in(srcFont / L"runtime_font_manifest.tsv", std::ios::binary);
    if (!in) { dbg << "no manifest" << std::endl; return false; }
    std::string line;
    if (!std::getline(in, line)) { dbg << "empty manifest" << std::endl; return false; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t a = line.find('\t');
    if (a == std::string::npos) { dbg << "no tab" << std::endl; return false; }
    size_t b = line.find('\t', a + 1);
    std::string targetName = line.substr(0, a);
    std::string sourceName = line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    auto target = font / U8(targetName);
    auto source = srcFont / U8(sourceName);
    std::error_code ec;
    bool te = fs::exists(target, ec), se = fs::exists(source, ec);
    dbg << "target=" << target.string() << " exists=" << te << " source=" << source.string() << " exists=" << se << std::endl;
    if (!te || !se) return false;
    fs::create_directories(backup, ec);
    fs::copy_file(target, backup / U8(targetName), fs::copy_options::overwrite_existing, ec);
    dbg << "backup ec=" << ec.message() << std::endl;
    if (ec) return false;
    std::ofstream out(journal, std::ios::binary | std::ios::trunc);
    out << targetName << std::endl;
    out.close();
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    dbg << "copy ec=" << ec.message() << std::endl;
    return !ec;
}
}
