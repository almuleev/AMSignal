#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace lvm {

// UTF-8 tabular content. Paths retain their native Unicode representation.
std::string csv_field(const std::string& value);
std::string tsv_header_field(std::string value);
bool split_csv_record(const std::string& record, std::vector<std::string>& fields);
// For binary streams, optional physical byte count includes BOM, CRLF and quoted lines.
bool read_text_record(std::istream& in, std::string& record, bool csv, std::size_t* bytes_read = nullptr);
std::string strip_utf8_bom(std::string text);

// Never truncate the destination on a failed write, flush, close or validation.
bool atomic_write_file(const std::filesystem::path& destination,
                       const std::function<bool(std::ofstream&)>& write,
                       std::string* error = nullptr);

} // namespace lvm
