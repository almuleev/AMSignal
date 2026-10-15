#pragma once
#include "overview_worker.hpp"

namespace gui {
extern lvm::OverviewWorker g_overview_worker;
// Keep expensive resident filtering out of paint/navigation callbacks.
void enable_background_signal_processing();
// Full-source view and exact Snap. Never substitutes display data for Dataset.
bool request_recording_overview(const std::filesystem::path& cache,lvm::AnalysisRange range,
                               std::vector<std::size_t> channels,std::size_t pixels,bool display_density=false);
void cancel_recording_overview();
bool poll_recording_overview();
bool synchronize_overview_viewport(std::size_t pixels);
bool overview_view_matches_viewport();
bool overview_view_can_render();
bool overview_time_yrange(double& minimum,double& maximum);
// UI-only callback. Source, viewport, processing, visibility and annotation
// revision are checked again before it can publish an original sample.
// analysis_range permits locked annotations and reports a valid no-point result.
bool request_overview_snap(double time,double value,
                           std::function<void(const lvm::RecordingSnapResult&)> accept,
                           bool analysis_range=false);
bool poll_overview_snap();
bool overview_analysis_range_snap_pending();
void cancel_overview_snap();
} // namespace gui
