#pragma once
#include "gui_platform.hpp"

namespace gui {

extern lvm::SpectrumWorker g_spectrum_worker;

bool last_fft_source_window(double& start, double& end, bool& from_selection);

std::wstring fft_window_status(double start, double end, bool from_selection);

std::wstring spectrum_sampling_status(const lvm::Spectrum& spec);

void clear_spectrum_cache_state();

void refresh_spec_channel_indices();

void apply_spectrum_result(lvm::Spectrum spectrum);

bool spectrum_needs_visible_channels();

// True when the cached (or in-flight) FFT was requested for the current
// selection/visible time window. Entering the FFT tab must not rerun it.
bool spectrum_matches_current_source();

void compute_spectrum_for_window(double start, double end, bool from_selection);

void compute_spectrum_from_current_source();

void compute_spectrum();

bool ensure_current_spectrum();

} // namespace gui
