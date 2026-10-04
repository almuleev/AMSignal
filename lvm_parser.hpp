// LVM parser (C++ port of the Python `read_lvm_file` / `prepare_loaded_data`).
//
// Parses LabVIEW Measurement (.lvm) files, tab-separated numeric .txt files,
// and comma-separated .csv files into a Time column plus numeric channel
// columns. The parsing rules mirror the reference Python implementation in
// lvm_viewer.py so results stay consistent.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lvm {

// Structural counters collected while streaming the file.
struct ParseStats {
    int header_markers = 0;   // number of ***End_of_Header*** lines
    int data_sections = 0;    // sections that contained numeric data
    long long data_rows = 0;  // numeric rows collected (before dropping NaN time)
    int max_columns = 0;      // widest row seen
};

struct ScanIndexCheckpoint {
    std::uint64_t offset = 0;  // file position for resuming at the next row
    double adjusted_time = 0.0;
    double prev_raw_time = 0.0;
    double prev_adjusted_time = 0.0;
    double time_offset = 0.0;
    double fallback_step = 1e-6;
    bool have_time_state = false;
    bool have_section_anchor = false;
    double section_anchor_seconds = 0.0;
    bool active_section_valid = false;
    double active_section_offset_seconds = 0.0;
    double active_section_x0 = 0.0;
};

struct ScanIndex {
    double range_start = 0.0;
    double range_end = 0.0;
    std::vector<std::string> column_labels;
    std::vector<ScanIndexCheckpoint> checkpoints;
    std::vector<std::string> export_comments;
    std::uintmax_t file_size = 0;
    std::filesystem::file_time_type modified{};
};

struct LoadOptions {
    bool use_time_window = false;
    double time_start = 0.0;
    double time_end = 0.0;
    const std::atomic<bool>* cancel_flag = nullptr;
    std::shared_ptr<const ScanIndex> scan_index;
};

// Parsed dataset: a time vector and aligned channel columns.
struct Dataset {
    std::vector<double> time;                  // first column (X / time)
    std::vector<double> raw_time;              // original first column before any normalization
    std::vector<std::string> names;            // channel names, e.g. "Channel_1"
    std::vector<char> generated_names;          // names assigned for missing/axis labels
    std::vector<std::vector<double>> channels; // one vector per channel, aligned with time
    std::vector<std::string> export_comments;  // optional `# ...` metadata from exported files
    ParseStats stats;
    bool partial = false;                      // true when loading stopped early by options
    bool frequency_axis = false;               // first column contains stored spectrum frequencies
    bool ok = false;
    std::string error;

    std::size_t rows() const { return time.size(); }
    std::size_t channel_count() const { return channels.size(); }
};

// Read and parse a file. Returns ok=false with `error` set on failure.
Dataset read_lvm_file(const std::filesystem::path& path);
Dataset read_lvm_file(const std::filesystem::path& path, const LoadOptions& options);
bool scan_time_bounds(const std::filesystem::path& path, double& out_start, double& out_end, std::string& error,
                      const std::atomic<bool>* cancel_flag = nullptr, ScanIndex* out_index = nullptr);

// Rebuild a monotonically increasing timeline (mirrors the Python "prepare"
// step that flattens Multi_Headings sections which reset local time).
void make_monotonic(std::vector<double>& time, const std::atomic<bool>* cancel_flag = nullptr);

// Drop channels that merely duplicate the time/X axis. Uses the supplied raw
// time as the reference. Returns the names of dropped channels.
std::vector<std::string> drop_duplicate_time_channels(Dataset& ds,
                                                       const std::vector<double>& raw_time,
                                                       const std::atomic<bool>* cancel_flag = nullptr);

}  // namespace lvm
