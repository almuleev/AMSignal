// Spectrum: native viewer implementation.
#include "gui_spectrum.hpp"
#include "gui_analysis_source.hpp"
#include "gui_processing.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"

namespace gui {

lvm::SpectrumWorker g_spectrum_worker;

bool last_fft_source_window(double& start, double& end, bool& from_selection) {
    if (!g.spec_source_valid || g.spec_source_end <= g.spec_source_start) return false;
    start = g.spec_source_start;
    end = g.spec_source_end;
    from_selection = g.spec_source_from_selection;
    return true;
}

std::wstring fft_window_status(double start, double end, bool from_selection) {
    wchar_t buf[220];
    if (g_str == &kEn) {
        swprintf(buf, 220, from_selection
            ? L"   |   FFT window: selected %.6g..%.6g s (Shift-click or right-click to clear)"
            : L"   |   FFT window: visible %.6g..%.6g s",
            start, end);
    } else {
        swprintf(buf, 220, from_selection
            ? L"   |   FFT окно: выбранный участок %.6g..%.6g c (Shift-клик или ПКМ для сброса)"
            : L"   |   FFT окно: видимый участок %.6g..%.6g c",
            start, end);
    }
    return buf;
}

std::wstring spectrum_sampling_status(const lvm::Spectrum& spec) {
    if (!spec.ok) return {};
    std::wstring text;
    if (spec.gaps_ignored) {
        wchar_t buf[180];
        swprintf(buf, 180, g_str == &kEn
            ? L"   |   FFT ignored timestamp gaps; all selected samples used"
            : L"   |   FFT без учёта пропусков времени; использованы все выбранные отсчёты");
        text = buf;
    }
    if (spec.resampled) text += g_str == &kEn
        ? L"   |   Uneven timestamps: linear interpolation"
        : L"   |   Неравномерное время: линейная интерполяция";
    return text;
}

void clear_spectrum_cache_state() {
    g_spectrum_worker.cancel();
    ++g.spec_generation;
    g.spec_pending = false;
    g.spec_attempted = false;
    g.spec_valid = false;
    g.spec = lvm::Spectrum{};
    g.spec_channel_indices.clear();
    g.spec_visible_state.clear();
}

void refresh_spec_channel_indices() {
    g.spec_channel_indices.assign(g.spec.names.size(), -1);
    for (std::size_t i = 0; i < g.spec.names.size(); ++i) {
        if (i < g.spec.source_channels.size()) g.spec_channel_indices[i] = static_cast<int>(g.spec.source_channels[i]);
    }
}

void apply_spectrum_result(lvm::Spectrum spectrum) {
    g.spec = std::move(spectrum);
    g.spec_valid = g.spec.ok;
    g.spec_pending = false;
    refresh_spec_channel_indices();
    if (g.spec_valid && g.spec_fit_pending) { g.freq_start = 0; g.freq_end = g.spec.nyquist; }
    g.spec_fit_pending = false;
}

bool spectrum_needs_visible_channels() {
    if (!g.light_mode) return false;
    // This mask describes channels included in the current attempt (including
    // an in-flight request), not the current display. Hiding a channel cannot
    // invalidate its FFT. Keep attempted channels even when they have too few
    // finite values, so repainting does not endlessly retry the same failure.
    for (std::size_t c = 0; c < g.visible.size(); ++c) {
        if (g.visible[c] && (c >= g.spec_visible_state.size() || !g.spec_visible_state[c])) return true;
    }
    return false;
}

bool spectrum_matches_current_source() {
    double start=0.0, end=0.0;
    bool from_selection=false;
    return current_fft_source_window(start,end,from_selection) &&
        g.spec_source_valid && g.spec_source_start==start && g.spec_source_end==end &&
        g.spec_source_from_selection==from_selection;
}

void compute_spectrum_for_window(double start, double end, bool from_selection) {
    if (!has_data()) return;
    clamp_time_window(start, end);
    g.spec_fit_pending = g.spec_fit_pending || !g.spec_source_valid || g.spec_source_start != start || g.spec_source_end != end;
    g.spec_source_start = start;
    g.spec_source_end = end;
    g.spec_source_from_selection = from_selection;
    g.spec_source_valid = end > start;
    bool has_visible_channel = false;
    for (char visible : g.visible) {
        if (visible) {
            has_visible_channel = true;
            break;
        }
    }
    if (!has_visible_channel) {
        clear_spectrum_cache_state();
        g.spec_attempted = true;
        g.spec_source_valid = end > start;
        g.spec_visible_state = g.visible;
        return;
    }
    std::vector<std::size_t> visible_channels;
    if (g.light_mode) {
        visible_channels.reserve(g.visible.size());
        for (std::size_t i = 0; i < g.visible.size(); ++i) {
            if (g.visible[i]) visible_channels.push_back(i);
        }
    } else {
        visible_channels.resize(g.ds.channel_count());
        for (std::size_t i = 0; i < visible_channels.size(); ++i) visible_channels[i] = i;
    }
    if (g.light_mode && visible_channels.empty()) {
        clear_spectrum_cache_state();
        g.spec_source_valid = end > start;
        g.spec_visible_state = g.visible;
        return;
    }
    try {
        ensure_channel_formula_vectors();
        bool worker_can_prepare = g.main && !g.noise_threshold_enabled;
        if (worker_can_prepare) {
            for (std::size_t c : visible_channels) {
                if (c >= g.channel_transform_kind.size() ||
                    g.channel_transform_kind[c] == TransformRuntimeKind::CachedFormula) {
                    worker_can_prepare = false;
                    break;
                }
            }
        }
        lvm::Dataset view;
        build_time_window_dataset(g.ds, start, end, view, &visible_channels, !worker_can_prepare);
        g.spec_attempted = true;
        if (g.main) {
            std::vector<lvm::SpectrumWorker::AffineTransform> transforms;
            if (worker_can_prepare) {
                transforms.reserve(visible_channels.size());
                for (std::size_t c : visible_channels) {
                    lvm::SpectrumWorker::AffineTransform transform;
                    if (g.channel_transform_kind[c] == TransformRuntimeKind::Affine) {
                        transform.mul = g.channel_transform_mul[c];
                        transform.add = g.channel_transform_add[c];
                    }
                    transforms.push_back(transform);
                }
            }
            g.spec_pending = true;
            g.spec_valid = false;
            g_spectrum_worker.submit(std::move(view), visible_channels, ++g.spec_generation, std::move(transforms));
        } else {
            auto spectrum = lvm::compute_spectrum(view, 0);
            for (auto& c : spectrum.source_channels) c = visible_channels[c];
            apply_spectrum_result(std::move(spectrum));
        }
    } catch (const std::exception& ex) {
        g.spec = lvm::Spectrum{};
        g.spec.error = ex.what();
        g.spec_valid = false; g.spec_pending = false; g.spec_attempted = true;
    }
    g.spec_visible_state = g.visible;
}

void compute_spectrum_from_current_source() {
    if (!has_data()) return;
    double start = 0.0, end = 0.0;
    bool from_selection = false;
    if (current_fft_source_window(start, end, from_selection)) {
        compute_spectrum_for_window(start, end, from_selection);
    }
}

void compute_spectrum() {
    if (!has_data()) return;
    double start = 0.0, end = 0.0;
    bool from_selection = false;
    if ((g.mode == AnalysisMode::FFT) && last_fft_source_window(start, end, from_selection)) {
        compute_spectrum_for_window(start, end, from_selection);
        return;
    }
    compute_spectrum_from_current_source();
}

bool ensure_current_spectrum() {
    if (!has_data()) return false;
    if (!g.spec_attempted || spectrum_needs_visible_channels()) compute_spectrum();
    return g.spec_valid;
}

} // namespace gui
