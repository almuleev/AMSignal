#pragma once
#include "gui_platform.hpp"
#include "recording_source.hpp"

namespace gui {

std::shared_ptr<const lvm::RecordingSource> current_recording_source();
lvm::TimeRange analysis_recording_bounds();

bool has_fft_window();

void clear_fft_window();

void clamp_time_window(double& start, double& end);

void set_fft_window(double start, double end);

bool current_fft_source_window(double& start, double& end, bool& from_selection);
bool change_analysis_range(lvm::AnalysisRange range);

bool fft_window_contains_time(double t);

bool build_time_window_dataset(const lvm::Dataset& in, double start, double end, lvm::Dataset& out,
                               const std::vector<std::size_t>* selected_channels = nullptr, bool apply_processing = true);

} // namespace gui
