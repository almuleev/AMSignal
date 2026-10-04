#include "lvm_parser.hpp"
#include "data_io.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <stdexcept>

namespace lvm {
namespace {

const std::unordered_set<std::string>& metadata_keys() {
    static const std::unordered_set<std::string> keys = {
        "LabVIEW Measurement", "Writer_Version", "Reader_Version", "Separator",
        "Decimal_Separator", "Multi_Headings", "X_Columns", "Time_Pref",
        "Operator", "Date", "Time", "Channels", "Samples", "Y_Unit_Label",
        "X_Dimension", "X0", "Delta_X",
    };
    return keys;
}

// Trim leading/trailing ASCII whitespace (matches Python str.strip defaults).
std::string strip(const std::string& s) {
    const char* ws = " \t\r\n\f\v";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool has_csv_extension(const std::filesystem::path& input) {
    const std::string path = input.u8string();
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    if (path.size() - dot != 4) return false;
    const char* ext = path.c_str() + dot;
    return (ext[1] == 'c' || ext[1] == 'C') &&
           (ext[2] == 's' || ext[2] == 'S') &&
           (ext[3] == 'v' || ext[3] == 'V');
}

std::string first_cell(const std::string& line) {
    const auto tab = line.find('\t');
    return strip(tab == std::string::npos ? line : line.substr(0, tab));
}

std::string second_cell(const std::string& line) {
    const auto first_tab = line.find('\t');
    if (first_tab == std::string::npos) return "";
    const auto second_tab = line.find('\t', first_tab + 1);
    return strip(line.substr(first_tab + 1, second_tab == std::string::npos ? std::string::npos : second_tab - first_tab - 1));
}

void split_cells_into(const std::string& line, char delimiter, bool normalize_decimal_commas,
                      std::vector<std::string>& cells) {
    if (delimiter == ',') {
        if (!split_csv_record(line, cells)) cells.clear();
        return;
    }
    cells.clear();
    std::string field;
    field.reserve(std::min<std::size_t>(line.size(), 128));
    for (char ch : line) {
        if (normalize_decimal_commas && ch == ',') ch = '.';
        if (ch == delimiter) {
            cells.push_back(strip(field));
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    cells.push_back(strip(field));
}

bool is_axis_label(const std::string& text) {
    return text == "Time" || text == "X_Value" || text == "X" || text == "Comment";
}

std::string sanitize_channel_label(const std::string& raw, std::size_t fallback_index, bool& generated) {
    std::string label = strip(raw);
    if (label.empty()) {
        generated = true;
        return "Channel_" + std::to_string(fallback_index + 1);
    }
    const auto bracket = label.find('[');
    if (bracket != std::string::npos) {
        const std::string prefix = strip(label.substr(0, bracket));
        if (!prefix.empty()) label = prefix;
    }
    const auto paren = label.find('(');
    if (paren != std::string::npos) {
        const std::string prefix = strip(label.substr(0, paren));
        if (!prefix.empty()) label = prefix;
    }
    if (label.empty() || is_axis_label(label)) {
        generated = true;
        return "Channel_" + std::to_string(fallback_index + 1);
    }
    generated = false;
    return label;
}

// First tab-delimited cell, stripped (Python: line.partition("\t")[0].strip()).
bool is_metadata_line(const std::string& line) {
    return metadata_keys().count(first_cell(line)) != 0;
}

// Parse a single cell to double, requiring the whole token to be numeric
// (matches Python float(): "1.2.3" -> ValueError -> NaN). Empty -> NaN.
double parse_cell(const std::string& cell, bool& is_numeric) {
    is_numeric = false;
    if (cell.empty()) return std::nan("");
    const std::string trimmed = strip(cell);
    const char* begin = trimmed.c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin || *end != '\0') return std::nan("");
    is_numeric = true;
    return std::isfinite(value) ? value : std::nan("");
}

struct RowSummary {
    bool first_numeric = false;
    double first_value = std::nan("");
    int numeric_count = 0;
};

RowSummary summarize_numeric_row(const std::string& line, char delimiter, bool normalize_decimal_commas) {
    RowSummary summary;
    if (delimiter == ',') {
        std::vector<std::string> cells;
        if (!split_csv_record(line, cells)) return summary;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            bool numeric = false;
            const double value = parse_cell(cells[i], numeric);
            if (i == 0) { summary.first_numeric = numeric; summary.first_value = value; }
            if (numeric) ++summary.numeric_count;
        }
        return summary;
    }
    std::string field;
    field.reserve(std::min<std::size_t>(line.size(), 128));
    bool first_field = true;
    auto process_field = [&](const std::string& token) {
        bool is_numeric = false;
        const double value = parse_cell(token, is_numeric);
        if (first_field) {
            summary.first_numeric = is_numeric;
            summary.first_value = value;
            first_field = false;
        }
        if (is_numeric) ++summary.numeric_count;
    };

    for (char ch : line) {
        if (normalize_decimal_commas && ch == ',') ch = '.';
        if (ch == delimiter) {
            process_field(field);
            if (summary.first_numeric && summary.numeric_count >= 2) return summary;
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    process_field(field);
    return summary;
}

struct PendingSectionTime {
    bool have_date = false;
    bool have_time = false;
    bool have_x0 = false;
    std::string date_text;
    std::string time_text;
    double x0 = 0.0;

    void reset() {
        have_date = false;
        have_time = false;
        have_x0 = false;
        date_text.clear();
        time_text.clear();
        x0 = 0.0;
    }
};

struct ActiveSectionTime {
    bool valid = false;
    double offset_seconds = 0.0;
    double x0 = 0.0;
};

constexpr std::size_t kScanCheckpointStride = 4096;

std::uint64_t current_stream_offset(std::ifstream& in) {
    const std::streampos pos = in.tellg();
    if (pos == std::streampos(-1)) return 0;
    return static_cast<std::uint64_t>(pos);
}

void maybe_add_scan_checkpoint(lvm::ScanIndex* index,
                               std::size_t row_index,
                               std::uint64_t offset,
                               double adjusted_time,
                               double prev_raw_time,
                               double prev_adjusted_time,
                               double time_offset,
                               double fallback_step,
                               bool have_time_state,
                               bool have_section_anchor,
                               double section_anchor_seconds,
                               const ActiveSectionTime& active_section) {
    if (!index) return;
    if (row_index == 1 || (row_index % kScanCheckpointStride) == 0) {
        lvm::ScanIndexCheckpoint cp{};
        cp.offset = offset;
        cp.adjusted_time = adjusted_time;
        cp.prev_raw_time = prev_raw_time;
        cp.prev_adjusted_time = prev_adjusted_time;
        cp.time_offset = time_offset;
        cp.fallback_step = fallback_step;
        cp.have_time_state = have_time_state;
        cp.have_section_anchor = have_section_anchor;
        cp.section_anchor_seconds = section_anchor_seconds;
        cp.active_section_valid = active_section.valid;
        cp.active_section_offset_seconds = active_section.offset_seconds;
        cp.active_section_x0 = active_section.x0;
        index->checkpoints.push_back(std::move(cp));
    }
}

const ScanIndexCheckpoint* choose_resume_checkpoint(const ScanIndex& index, double time_start) {
    if (index.checkpoints.empty()) return nullptr;
    auto it = std::lower_bound(
        index.checkpoints.begin(), index.checkpoints.end(), time_start,
        [](const ScanIndexCheckpoint& cp, double value) { return cp.adjusted_time < value; });
    if (it == index.checkpoints.begin()) return nullptr;
    --it;
    return &*it;
}

void restore_scan_checkpoint_state(const ScanIndexCheckpoint& cp,
                                   bool& have_time_state,
                                   double& prev_raw_time,
                                   double& prev_adjusted_time,
                                   double& time_offset,
                                   double& fallback_step,
                                   bool& have_section_anchor,
                                   double& section_anchor_seconds,
                                   ActiveSectionTime& active_section) {
    have_time_state = cp.have_time_state;
    prev_raw_time = cp.prev_raw_time;
    prev_adjusted_time = cp.prev_adjusted_time;
    time_offset = cp.time_offset;
    fallback_step = cp.fallback_step;
    have_section_anchor = cp.have_section_anchor;
    section_anchor_seconds = cp.section_anchor_seconds;
    active_section.valid = cp.active_section_valid;
    active_section.offset_seconds = cp.active_section_offset_seconds;
    active_section.x0 = cp.active_section_x0;
}

bool parse_labview_datetime(const std::string& date_text, const std::string& time_text, double& out_seconds) {
    int year = 0, month = 0, day = 0;
    if (std::sscanf(date_text.c_str(), "%d/%d/%d", &year, &month, &day) != 3) return false;

    std::string normalized_time = time_text;
    for (char& ch : normalized_time) {
        if (ch == ',') ch = '.';
    }

    int hour = 0, minute = 0;
    double seconds = 0.0;
    if (std::sscanf(normalized_time.c_str(), "%d:%d:%lf", &hour, &minute, &seconds) != 3) return false;

    const int whole_seconds = static_cast<int>(std::floor(seconds));
    const double fractional_seconds = seconds - static_cast<double>(whole_seconds);

    std::tm tm_value{};
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = day;
    tm_value.tm_hour = hour;
    tm_value.tm_min = minute;
    tm_value.tm_sec = whole_seconds;
    tm_value.tm_isdst = -1;
    const std::time_t base = std::mktime(&tm_value);
    if (base == static_cast<std::time_t>(-1)) return false;

    out_seconds = static_cast<double>(base) + fractional_seconds;
    return true;
}

void update_section_metadata(const std::string& line, PendingSectionTime& pending) {
    const std::string key = first_cell(line);
    const std::string value = second_cell(line);
    if (value.empty()) return;

    if (key == "Date") {
        pending.have_date = true;
        pending.date_text = value;
        return;
    }
    if (key == "Time") {
        pending.have_time = true;
        pending.time_text = value;
        return;
    }
    if (key == "X0") {
        bool is_numeric = false;
        std::string normalized = value;
        for (char& ch : normalized) {
            if (ch == ',') ch = '.';
        }
        const double parsed = parse_cell(normalized, is_numeric);
        if (is_numeric && std::isfinite(parsed)) {
            pending.have_x0 = true;
            pending.x0 = parsed;
        }
    }
}

void activate_section_time(const PendingSectionTime& pending,
                           bool& have_anchor,
                           double& anchor_seconds,
                           ActiveSectionTime& active) {
    active.valid = false;
    if (!pending.have_date || !pending.have_time || !pending.have_x0) return;

    double absolute_seconds = 0.0;
    if (!parse_labview_datetime(pending.date_text, pending.time_text, absolute_seconds)) return;
    if (!have_anchor) {
        have_anchor = true;
        anchor_seconds = absolute_seconds;
    }

    active.valid = true;
    active.offset_seconds = absolute_seconds - anchor_seconds;
    active.x0 = pending.x0;
}

// Causal normalization shared by full preparation, range scans and indexed reads.
double advance_time(double source, bool& initialized, double& previous_source,
                    double& previous_time, double& offset, double& step) {
    if (!std::isfinite(source)) return std::nan("");
    double adjusted = source + offset;
    if (initialized) {
        const double diff = source - previous_source;
        if (std::isfinite(diff) && diff > 0.0) step = diff;
        if (adjusted <= previous_time) {
            adjusted = previous_time + step;
            if (adjusted <= previous_time) adjusted = std::nextafter(previous_time, INFINITY);
            offset = adjusted - source;
        }
    }
    initialized = true;
    previous_source = source;
    previous_time = adjusted;
    return adjusted;
}

void check_cancel(const std::atomic<bool>* flag) {
    if (flag && flag->load(std::memory_order_relaxed)) throw std::runtime_error("Operation cancelled.");
}

}  // namespace

Dataset read_lvm_file(const std::filesystem::path& path) {
    return read_lvm_file(path, LoadOptions{});
}

static bool is_frequency_data(const std::vector<std::string>& labels, const std::vector<std::string>& comments) {
    if (!labels.empty() && strip(labels.front()) == "Frequency") return true;
    std::string section;
    for (const auto& comment : comments) {
        if (!comment.empty() && comment.front() == '[') section = comment;
        if (section == "[export]" && comment == "plot_mode=frequency") return true;
    }
    return false;
}

bool scan_time_bounds(const std::filesystem::path& path, double& out_start, double& out_end, std::string& error,
                      const std::atomic<bool>* cancel_flag, ScanIndex* out_index) {
    if (out_index) {
        *out_index = ScanIndex{};
        std::error_code ec;
        out_index->file_size = std::filesystem::file_size(path, ec);
        out_index->modified = std::filesystem::last_write_time(path, ec);
        out_index->range_start = 0.0;
        out_index->range_end = 0.0;
        out_index->column_labels.clear();
        out_index->checkpoints.clear();
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Cannot open file: " + path.u8string();
        return false;
    }

    const bool csv_mode = has_csv_extension(path);
    const char delimiter = csv_mode ? ',' : '\t';
    const bool normalize_decimal_commas = !csv_mode;

    bool have_time = false;
    double first_time = 0.0;
    double last_time = 0.0;
    double prev_raw_time = 0.0;
    double prev_adjusted_time = 0.0;
    double time_offset = 0.0;
    double fallback_step = 1e-6;
    bool have_section_anchor = false;
    double section_anchor_seconds = 0.0;
    PendingSectionTime pending_section{};
    ActiveSectionTime active_section{};
    int header_count = 0;
    bool section_metadata_seen = false;
    std::vector<std::string> column_labels;
    std::vector<std::string> cells;
    cells.reserve(64);
    std::size_t numeric_row_index = 0;

    std::string raw_line;
    long long line_index = 0;
    while (true) {
        if (!read_text_record(in, raw_line, csv_mode)) break;
        ++line_index;
        if (cancel_flag && (line_index & 0xFF) == 0 && cancel_flag->load(std::memory_order_relaxed)) {
            error = "Operation cancelled.";
            return false;
        }
        const std::string& line = raw_line;
        if (line.empty()) continue;
        if (starts_with(line, "#")) {
            if (out_index) out_index->export_comments.push_back(strip(line.substr(1)));
            continue;
        }
        if (!csv_mode && starts_with(line, "***End_of_Header***")) {
            ++header_count;
            section_metadata_seen = false;
            activate_section_time(pending_section, have_section_anchor, section_anchor_seconds, active_section);
            pending_section.reset();
            continue;
        }
        if (!csv_mode && starts_with(line, "***")) continue;
        if (!csv_mode && header_count > 0 && !section_metadata_seen && column_labels.empty()) {
            const RowSummary row = summarize_numeric_row(line, delimiter, normalize_decimal_commas);
            if (row.numeric_count == 0) {
                split_cells_into(line, delimiter, normalize_decimal_commas, cells);
                if (cells.size() > 1) {
                    column_labels = cells;
                    continue;
                }
            }
        }
        if (!csv_mode && is_metadata_line(line)) {
            if (header_count > 0) section_metadata_seen = true;
            update_section_metadata(line, pending_section);
            continue;
        }
        if (column_labels.empty()) {
            const RowSummary row = summarize_numeric_row(line, delimiter, normalize_decimal_commas);
            if (row.numeric_count == 0) {
                split_cells_into(line, delimiter, normalize_decimal_commas, cells);
                if (cells.size() > 1) {
                    column_labels = cells;
                    continue;
                }
            }
        }

        const RowSummary row = summarize_numeric_row(line, delimiter, normalize_decimal_commas);
        if (row.numeric_count < 2 || !row.first_numeric || !std::isfinite(row.first_value)) continue;

        const double raw_time = row.first_value;
        double adjusted_time = raw_time;
        if (active_section.valid) {
            adjusted_time = active_section.offset_seconds + (raw_time - active_section.x0);
        }
        const bool first = !have_time;
        adjusted_time = advance_time(adjusted_time, have_time, prev_raw_time, prev_adjusted_time, time_offset, fallback_step);
        if (first) first_time = adjusted_time;
        last_time = adjusted_time;

        ++numeric_row_index;
        const std::uint64_t next_offset = current_stream_offset(in);
        maybe_add_scan_checkpoint(out_index, numeric_row_index, next_offset, adjusted_time, prev_raw_time,
                                  prev_adjusted_time, time_offset, fallback_step, have_time,
                                  have_section_anchor, section_anchor_seconds, active_section);
    }

    if (in.bad()) { error = "Error reading file or incomplete CSV record."; return false; }
    if (cancel_flag && cancel_flag->load(std::memory_order_relaxed)) { error = "Operation cancelled."; return false; }
    if (!have_time) {
        error = "No numeric data found. Expected time + numeric channel columns in a .lvm, tab-separated .txt, or .csv file.";
        return false;
    }

    const auto normalize_bound = [](double v) {
        return (std::isfinite(v) && std::fabs(v) < 1e-12) ? 0.0 : v;
    };
    first_time = normalize_bound(first_time);
    last_time = normalize_bound(last_time);
    if (last_time < first_time) std::swap(last_time, first_time);

    out_start = first_time;
    out_end = last_time;
    if (out_index) {
        out_index->range_start = out_start;
        out_index->range_end = out_end;
        out_index->column_labels = std::move(column_labels);
    }
    return true;
}

Dataset read_lvm_file(const std::filesystem::path& path, const LoadOptions& options) {
    Dataset ds;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ds.error = "Cannot open file: " + path.u8string();
        return ds;
    }

    const bool csv_mode = has_csv_extension(path);
    const char delimiter = csv_mode ? ',' : '\t';
    const bool normalize_decimal_commas = !csv_mode;

    const double nan_value = std::nan("");

    std::vector<std::vector<double>> columns;
    std::vector<char> column_has_value;  // any non-NaN seen in this column
    std::vector<double> raw_time_rows;
    std::vector<std::string> cells;
    std::vector<double> parsed;
    cells.reserve(64);
    parsed.reserve(64);
    bool has_nan_time_rows = false;
    long long row_count = 0;

    int header_count = 0;
    int section_hits = 0;
    bool section_has_data = false;

    std::string raw_line;
    long long line_index = 0;
    bool have_time_state = false;
    double prev_raw_time = 0.0;
    double prev_adjusted_time = 0.0;
    double time_offset = 0.0;
    double fallback_step = 1e-6;
    bool have_section_anchor = false;
    double section_anchor_seconds = 0.0;
    PendingSectionTime pending_section{};
    ActiveSectionTime active_section{};
    bool section_metadata_seen = false;
    std::vector<std::string> column_labels;

    const ScanIndexCheckpoint* resume_checkpoint = nullptr;
    std::error_code index_error;
    const bool index_current = options.scan_index &&
        options.scan_index->file_size == std::filesystem::file_size(path, index_error) && !index_error &&
        options.scan_index->modified == std::filesystem::last_write_time(path, index_error) && !index_error;
    if (options.use_time_window && index_current && !options.scan_index->checkpoints.empty()) {
        resume_checkpoint = choose_resume_checkpoint(*options.scan_index, options.time_start);
        if (resume_checkpoint) {
            column_labels = options.scan_index->column_labels;
            ds.export_comments = options.scan_index->export_comments;
            restore_scan_checkpoint_state(*resume_checkpoint, have_time_state, prev_raw_time, prev_adjusted_time,
                                          time_offset, fallback_step, have_section_anchor, section_anchor_seconds,
                                          active_section);
            in.clear();
            in.seekg(static_cast<std::streamoff>(resume_checkpoint->offset), std::ios::beg);
            if (!in) {
                in.clear();
                in.seekg(0, std::ios::beg);
                resume_checkpoint = nullptr;
                column_labels.clear();
                have_time_state = false;
                prev_raw_time = 0.0;
                prev_adjusted_time = 0.0;
                time_offset = 0.0;
                fallback_step = 1e-6;
                have_section_anchor = false;
                section_anchor_seconds = 0.0;
                active_section = ActiveSectionTime{};
            }
        }
    }

    for (; read_text_record(in, raw_line, csv_mode); ++line_index) {
        if (options.cancel_flag && (line_index & 0xFF) == 0 &&
            options.cancel_flag->load(std::memory_order_relaxed)) {
            ds.error = "Operation cancelled.";
            return ds;
        }
        const std::string& line = raw_line;
        if (line.empty()) continue;

        if (starts_with(line, "#")) {
            const std::string comment = strip(line.substr(1));
            if (!comment.empty()) ds.export_comments.push_back(comment);
            continue;
        }

        if (!csv_mode && starts_with(line, "***End_of_Header***")) {
            ++header_count;
            section_has_data = false;
            activate_section_time(pending_section, have_section_anchor, section_anchor_seconds, active_section);
            section_metadata_seen = false;
            pending_section.reset();
            continue;
        }
        if (!csv_mode && starts_with(line, "***")) {
            continue;
        }
        if (!csv_mode && header_count > 0 && !section_metadata_seen && column_labels.empty()) {
            const RowSummary row = summarize_numeric_row(line, delimiter, normalize_decimal_commas);
            if (row.numeric_count == 0) {
                split_cells_into(line, delimiter, normalize_decimal_commas, cells);
                if (cells.size() > 1) {
                    column_labels = cells;
                    continue;
                }
            }
        }
        if (!csv_mode && is_metadata_line(line)) {
            if (header_count > 0) section_metadata_seen = true;
            update_section_metadata(line, pending_section);
            continue;
        }

        if (column_labels.empty()) {
            const RowSummary row = summarize_numeric_row(line, delimiter, normalize_decimal_commas);
            if (row.numeric_count == 0) {
                split_cells_into(line, delimiter, normalize_decimal_commas, cells);
                if (cells.size() > 1) {
                    column_labels = cells;
                    continue;
                }
            }
        }

        int numeric_count = 0;
        split_cells_into(line, delimiter, normalize_decimal_commas, cells);
        if (csv_mode && cells.empty()) { ds.error = "Malformed CSV record."; return ds; }
        parsed.clear();
        parsed.reserve(cells.size());
        for (const std::string& cell : cells) {
            bool is_numeric = false;
            const double v = parse_cell(cell, is_numeric);
            parsed.push_back(v);
            if (is_numeric) ++numeric_count;
        }

        const int part_count = static_cast<int>(parsed.size());
        if (numeric_count < 2) continue;

        if (row_count == 0) {
            if (std::find(ds.export_comments.begin(), ds.export_comments.end(), "data_kind=frf") != ds.export_comments.end()) {
                ds.ok = false;
                ds.error = "This file contains an exported FRF, not time-domain samples. Open the original recording to calculate FRF.";
                return ds;
            }
            ds.frequency_axis = is_frequency_data(column_labels, ds.export_comments);
        }
        const double raw_time = parsed.empty() ? std::nan("") : parsed[0];
        const bool have_raw_time = std::isfinite(raw_time);
        if (!have_raw_time) continue;
        if (have_raw_time && active_section.valid) {
            parsed[0] = active_section.offset_seconds + (raw_time - active_section.x0);
        }

        bool keep_row = true;
        if (options.use_time_window) {
            if (!have_raw_time) continue;

            double adjusted_time = parsed[0];
            if (!ds.frequency_axis) adjusted_time = advance_time(adjusted_time, have_time_state, prev_raw_time, prev_adjusted_time,
                                                               time_offset, fallback_step);

            if (adjusted_time < options.time_start) {
                keep_row = false;
            } else if (adjusted_time > options.time_end) {
                ds.partial = true;
                break;
            }
            parsed[0] = adjusted_time;
        }

        if (!section_has_data) {
            section_has_data = true;
            ++section_hits;
        }

        if (!keep_row) continue;
        if (parsed.empty() || std::isnan(parsed[0])) has_nan_time_rows = true;

        // Widen the column store to fit this row, back-filling earlier rows.
        if (part_count > static_cast<int>(columns.size())) {
            const int extra = part_count - static_cast<int>(columns.size());
            for (int k = 0; k < extra; ++k) {
                columns.emplace_back(static_cast<std::size_t>(row_count), nan_value);
                column_has_value.push_back(0);
            }
        }
        const int col_count = static_cast<int>(columns.size());

        for (int c = 0; c < part_count; ++c) {
            const double v = parsed[c];
            columns[c].push_back(v);
            if (!std::isnan(v)) column_has_value[c] = 1;
        }
        for (int c = part_count; c < col_count; ++c) {
            columns[c].push_back(nan_value);
        }
        raw_time_rows.push_back(raw_time);
        ++row_count;
    }

    if (in.bad()) { ds.error = "Error reading file or incomplete CSV record."; return ds; }
    if (options.cancel_flag && options.cancel_flag->load(std::memory_order_relaxed)) {
        ds.error = "Operation cancelled."; return ds;
    }
    ds.partial = ds.partial || options.use_time_window;
    ds.stats.header_markers = header_count;
    ds.stats.data_sections = section_hits;
    ds.stats.data_rows = row_count;
    ds.stats.max_columns = static_cast<int>(columns.size());
    if (row_count == 0 || columns.empty()) {
        ds.error = options.use_time_window
            ? "No data found in the selected time range."
            : "No numeric data found. Expected time + numeric channel columns in a .lvm, tab-separated .txt, or .csv file.";
        return ds;
    }

    // First column is time; keep channels that hold at least one real value.
    std::vector<double>& time_col = columns[0];
    std::vector<std::vector<double>> kept_channels;
    std::vector<std::string> kept_names;
    std::vector<char> kept_generated_names;
    for (std::size_t i = 1; i < columns.size(); ++i) {
        if (i < column_has_value.size() && column_has_value[i]) {
            const std::string raw_label = (i < column_labels.size()) ? column_labels[i] : "";
            bool generated = false;
            kept_names.push_back(sanitize_channel_label(raw_label, kept_channels.size(), generated));
            kept_generated_names.push_back(generated ? 1 : 0);
            kept_channels.push_back(std::move(columns[i]));
        }
    }

    // Drop rows whose time is NaN, keeping channels aligned.
    if (!has_nan_time_rows && raw_time_rows.size() == time_col.size()) {
        ds.time = std::move(time_col);
        ds.raw_time = std::move(raw_time_rows);
        ds.channels = std::move(kept_channels);
    } else {
        ds.time.reserve(time_col.size());
        ds.raw_time.reserve(time_col.size());
        ds.channels.assign(kept_channels.size(), {});
        for (auto& ch : ds.channels) ch.reserve(time_col.size());
        for (std::size_t r = 0; r < time_col.size(); ++r) {
            if (std::isnan(time_col[r])) continue;
            ds.time.push_back(time_col[r]);
            ds.raw_time.push_back((r < raw_time_rows.size()) ? raw_time_rows[r] : time_col[r]);
            for (std::size_t c = 0; c < kept_channels.size(); ++c) {
                ds.channels[c].push_back(kept_channels[c][r]);
            }
        }
    }
    ds.names = std::move(kept_names);
    ds.generated_names = std::move(kept_generated_names);
    ds.frequency_axis = is_frequency_data(column_labels, ds.export_comments);
    if (ds.frequency_axis) {
        for (std::size_t i = 0; i < ds.time.size(); ++i) {
            if (!std::isfinite(ds.time[i]) || ds.time[i] < 0 || (i && ds.time[i] <= ds.time[i - 1])) {
                ds.error = "Spectrum frequencies must be finite, non-negative and strictly increasing.";
                return ds;
            }
        }
    }
    ds.ok = !ds.channels.empty();
    if (ds.time.empty() && options.use_time_window) {
        ds.ok = false;
        ds.error = "No data found in the selected time range.";
    } else if (!ds.ok) {
        ds.error = "No data channels available.";
    }
    return ds;
}

void make_monotonic(std::vector<double>& time, const std::atomic<bool>* cancel_flag) {
    if (time.size() <= 1) return;

    double fallback_step = 1e-6;
    double offset = 0.0;
    bool initialized = false;
    double previous_source = 0.0, previous_time = 0.0;
    for (std::size_t i = 0; i < time.size(); ++i) {
        if ((i & 0xFFF) == 0) check_cancel(cancel_flag);
        time[i] = advance_time(time[i], initialized, previous_source, previous_time, offset, fallback_step);
    }
}

std::vector<std::string> drop_duplicate_time_channels(Dataset& ds,
                                                      const std::vector<double>& raw_time,
                                                      const std::atomic<bool>* cancel_flag) {
    if (ds.frequency_axis) return {};
    const auto allclose = [](double a, double b) {
        const double rtol = 1e-9, atol = 1e-12;
        return std::fabs(a - b) <= atol + rtol * std::fabs(b);
    };

    std::vector<std::string> dropped;
    std::vector<std::vector<double>> kept_channels;
    std::vector<std::string> kept_names;
    std::vector<char> kept_generated_names;

    std::vector<std::size_t> keep;
    for (std::size_t c = 0; c < ds.channels.size(); ++c) {
        const auto& col = ds.channels[c];
        bool any_valid = false;
        bool duplicate = true;
        for (std::size_t r = 0; r < col.size() && r < raw_time.size(); ++r) {
            if ((r & 0xFFF) == 0) check_cancel(cancel_flag);
            if (std::isnan(col[r]) || std::isnan(raw_time[r])) continue;
            any_valid = true;
            if (!allclose(col[r], raw_time[r])) {
                duplicate = false;
                break;
            }
        }
        if (any_valid && duplicate) {
            if (c < ds.names.size()) dropped.push_back(ds.names[c]);
        } else {
            keep.push_back(c);
        }
    }

    if (dropped.empty()) return dropped;
    for (std::size_t c : keep) {
        const bool generated = c < ds.generated_names.size() && ds.generated_names[c];
        kept_names.push_back(generated ? "Channel_" + std::to_string(kept_names.size() + 1)
                                       : c < ds.names.size() ? std::move(ds.names[c])
                                                             : "Channel_" + std::to_string(kept_names.size() + 1));
        kept_generated_names.push_back(generated ? 1 : 0);
        kept_channels.push_back(std::move(ds.channels[c]));
    }

    ds.channels = std::move(kept_channels);
    ds.names = std::move(kept_names);
    ds.generated_names = std::move(kept_generated_names);
    return dropped;
}

}  // namespace lvm
