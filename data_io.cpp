#include "data_io.hpp"

#include <atomic>
#include <chrono>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lvm {

std::string strip_utf8_bom(std::string text) {
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    return text;
}

std::string csv_field(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') out += '"';
        out += ch;
    }
    return out + '"';
}

std::string tsv_header_field(std::string value) {
    for (char& ch : value) if (ch == '\t' || ch == '\r' || ch == '\n') ch = ' ';
    return value;
}

bool split_csv_record(const std::string& record, std::vector<std::string>& fields) {
    fields.clear();
    std::string field;
    bool quoted = false, closed = false;
    for (std::size_t i = 0; i < record.size(); ++i) {
        const char ch = record[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < record.size() && record[i + 1] == '"') { field += '"'; ++i; }
                else { quoted = false; closed = true; }
            } else field += ch;
        } else if (ch == ',') {
            fields.push_back(std::move(field)); field.clear(); closed = false;
        } else if (ch == '"' && field.empty() && !closed) {
            quoted = true;
        } else if (closed) {
            if (ch != ' ' && ch != '\t' && ch != '\r') return false;
        } else {
            if (ch == '"') return false;
            field += ch;
        }
    }
    if (quoted) return false;
    fields.push_back(std::move(field));
    return true;
}

bool read_text_record(std::istream& in, std::string& record, bool csv, std::size_t* bytes_read) {
    if (bytes_read) *bytes_read = 0;
    if (!std::getline(in, record)) return false;
    if (bytes_read) *bytes_read = record.size() + (in.eof() ? 0 : 1);
    if (!record.empty() && record.back() == '\r') record.pop_back();
    record = strip_utf8_bom(std::move(record));
    if (!csv || (!record.empty() && record.front() == '#')) return true;
    // Count unescaped quotes, retaining embedded newlines in quoted fields.
    std::size_t quotes = 0;
    for (char ch : record) if (ch == '"') ++quotes;
    while (quotes % 2) {
        std::string next;
        if (!std::getline(in, next)) { in.setstate(std::ios::badbit); return false; }
        if (bytes_read) *bytes_read += next.size() + (in.eof() ? 0 : 1);
        if (!next.empty() && next.back() == '\r') next.pop_back();
        for (char ch : next) if (ch == '"') ++quotes;
        record += '\n'; record += next;
    }
    return true;
}

bool atomic_write_file(const std::filesystem::path& destination,
                       const std::function<bool(std::ofstream&)>& write, std::string* error) {
    namespace fs = std::filesystem;
    static std::atomic<unsigned long long> sequence{0};
    fs::path staging, temporary;
    auto cleanup = [&] {
        std::error_code ignored;
        if (!temporary.empty()) fs::remove(temporary, ignored);
        if (!staging.empty()) fs::remove(staging, ignored);
    };
    try {
        if (error) error->clear();
        const fs::path target = fs::absolute(destination);
        if (fs::is_directory(target)) throw std::runtime_error("Destination is a directory.");
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            auto candidate = target.parent_path() / (".amgv-write-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
            if (fs::create_directory(candidate)) { staging = std::move(candidate); break; }
        }
        if (staging.empty()) throw std::runtime_error("Cannot create export staging directory.");
        temporary = staging / "data.tmp";
        std::ofstream out(temporary, std::ios::binary);
        if (!out || !write(out)) throw std::runtime_error("Export validation or writing failed.");
        out.flush();
        if (!out) throw std::runtime_error("Cannot flush export data.");
        out.close();
        if (!out) throw std::runtime_error("Cannot close export file.");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Cannot replace export file");
#else
        fs::rename(temporary, target);
#endif
        cleanup();
        return true;
    } catch (const std::exception& ex) {
        cleanup();
        if (error) *error = ex.what();
        return false;
    }
}

} // namespace lvm
