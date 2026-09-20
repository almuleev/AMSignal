#pragma once
#include "gui_platform.hpp"
#include "frf_worker.hpp"

namespace gui {
extern lvm::FrfWorker g_frf_worker;
void invalidate_frf(bool reset_view = false);
void compute_frf_from_current_source();
bool ensure_current_frf();
void apply_frf_result(lvm::FrfBatchResult result);
bool set_frf_channels(std::vector<int> references, std::vector<int> responses);
void clear_frf_channel_selection(bool supports);
std::wstring frf_curve_label(std::size_t response);
void poll_frf_result();
void on_frf_processing_changed();
std::wstring frf_status_text();
std::wstring frf_error_text(lvm::FrfError error);
void create_frf_panel(HWND parent, HINSTANCE instance);
void layout_frf_panel();
void refresh_frf_controls(bool repopulate = false);
bool set_frf_frequency_range(double low, double high);
// The two axis representations describe the same visible interval. Keep the
// inactive representation current after mouse navigation.
void sync_frf_frequency_limits();
void reset_frf_view();
bool frf_command_supported(int command);
} // namespace gui
