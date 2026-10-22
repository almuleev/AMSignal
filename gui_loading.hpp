#pragma once
#include "gui_platform.hpp"
#include "resource_budget.hpp"
#include "recording_source.hpp"

namespace gui {

extern std::thread g_load_worker;

struct AsyncScanResult {
    std::shared_ptr<lvm::MemoryBudget::Reservation> resident_reservation;
    unsigned long long token = 0;
    std::wstring path;
    bool ok = false;
    bool cancelled = false;
    double range_start = 0.0;
    double range_end = 0.0;
    std::string error;
    std::shared_ptr<lvm::ScanIndex> index;
    bool index_from_disk_cache=false;
    lvm::Dataset source_metadata;
    std::uint64_t source_rows=0;
    lvm::Dataset resident_data;
    double resident_gap_step=0;
    double resident_filter_step=0;
};

struct AsyncLoadResult {
    // Declared before ds, so samples are destroyed before admission is released.
    std::shared_ptr<lvm::MemoryBudget::Reservation> memory_reservation;
    unsigned long long token = 0;
    std::wstring path;
    lvm::Dataset ds;
    bool ok = false;
    bool cancelled = false;
    double cached_global_gap_step = 0.0;
    bool cached_global_gap_step_ready = false;
    std::string error;
    std::uintmax_t source_size=0;
    std::filesystem::file_time_type source_modified{};
    bool source_stamp_valid=false;
};

void request_async_load_cancel();

// Registers the per-user Windows association for AMSignal project files.
void register_project_file_association();

bool apply_loaded_dataset(lvm::Dataset ds, const std::wstring& wpath,
                          bool source_preview, double cached_global_gap_step,
                          bool cached_global_gap_step_ready,
                          std::optional<lvm::RecordingStamp> expected_stamp = {},double source_filter_step=0);
// Releases full resident samples without changing the numerical input/settings.
bool move_resident_recordings_to_disk(std::uint64_t required=0);

bool start_async_scan_task(const std::wstring& wpath,bool adaptive_resident=false);

// Publishes a bounded metadata snapshot and opens the full source overview.
bool apply_scanned_recording(AsyncScanResult& result);

// Per-load display allowance; reserves headroom for other documents and processing.
std::size_t display_load_memory_budget(std::uint64_t available_physical,
                                     std::uint64_t available_commit);

bool start_async_load_task(const std::wstring& wpath);

bool load_path_interactive(const std::wstring& wpath);

// Recovery after resident admission failure: open the complete bounded source.
bool retry_load_as_source(AsyncLoadResult& result);

void open_file();

void show_recent_files_menu(HWND owner);

LRESULT handle_loading_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

} // namespace gui
