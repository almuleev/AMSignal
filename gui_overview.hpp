#pragma once
#include "overview_worker.hpp"

namespace gui {
extern lvm::OverviewWorker g_overview_worker;
// Infrastructure for the upcoming disk Signal renderer. No Dataset substitution.
bool request_recording_overview(const std::filesystem::path& cache,lvm::AnalysisRange range,
                               std::vector<std::size_t> channels,std::size_t pixels,bool display_density=false);
void cancel_recording_overview();
bool poll_recording_overview();
bool synchronize_overview_viewport(std::size_t pixels);
bool overview_processing_supported();
bool overview_view_matches_viewport();
bool overview_view_can_render();
bool overview_time_yrange(double& minimum,double& maximum);
// UI-only callback. Source, viewport, processing, visibility and annotation
// revision are checked again before it can publish an original sample.
bool request_overview_snap(double time,double value,
                           std::function<void(const lvm::RecordingSnapResult&)> accept);
bool poll_overview_snap();
void cancel_overview_snap();
} // namespace gui
