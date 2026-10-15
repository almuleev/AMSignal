// Spectrum: native viewer implementation.
#include "gui_spectrum.hpp"
#include "gui_frequency_view.hpp"
#include "gui_analysis_source.hpp"
#include "gui_processing.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "gui_resources.hpp"
#include "gui_recording_processing.hpp"
#include "disk_spectrum.hpp"
#include "gui_documents.hpp"

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
            ? L"   |   Analysis: interval %.6g..%.6g s"
            : L"   |   Analysis: entire recording %.6g..%.6g s",
            start, end);
    } else {
        swprintf(buf, 220, from_selection
            ? L"   |   Анализ: участок %.6g..%.6g c"
            : L"   |   Анализ: вся запись %.6g..%.6g c",
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
    if(spectrum.recording_source)try{spectrum.recording_source->check_current();}
        catch(const std::exception& e){spectrum={};spectrum.error=e.what();}
    g.spec = std::move(spectrum);
    // The arrays now belong to the document and enter resident accounting.
    g.spec.memory_reservation.reset();
    g.spec_valid = g.spec.ok;
    g.spec_pending = false;
    refresh_spec_channel_indices();
    if (g.spec_valid && g.spec_fit_pending) { g.freq_start = 0; g.freq_end = g.spec.nyquist; }
    g.spec_fit_pending = false;
    trim_document_analysis_caches();
    sync_frequency_view_controls();
}

bool spectrum_needs_visible_channels() {
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
    visible_channels.reserve(g.visible.size());
    for (std::size_t i = 0; i < g.visible.size(); ++i) {
        if (g.visible[i]) visible_channels.push_back(i);
    }
    try {
        if(g.noise_threshold_enabled)share_resident_recording();
        if(g.main) {g_spectrum_worker.cancel();++g.spec_generation;}
        const auto selected_rows=static_cast<std::size_t>(std::upper_bound(g.ds.time.begin(),g.ds.time.end(),end)-
            std::lower_bound(g.ds.time.begin(),g.ds.time.end(),start));
        const auto required=lvm::spectrum_workspace_bytes(selected_rows,visible_channels.size())+
            1024.0L*1024+selected_rows*(visible_channels.size()+1.0L)*8;
        const auto resources=current_analysis_resources(static_cast<std::uint64_t>(required));
        g.spec_source_start=start;g.spec_source_end=end;g.spec_source_from_selection=from_selection;g.spec_source_valid=end>start;
        const auto lo=std::lower_bound(g.ds.time.begin(),g.ds.time.end(),start);
        const auto hi=std::upper_bound(g.ds.time.begin(),g.ds.time.end(),end);
        const auto rows=static_cast<std::size_t>(hi-lo);
        const auto ram_required=lvm::spectrum_workspace_bytes(rows,visible_channels.size())+
            1024.0L*1024+rows*(visible_channels.size()+1.0L)*8;
        const bool source_needed=(g.recording_source && g.ds.rows()<3 && g.recording_rows>g.ds.rows()) ||
            (g.current_file_partial && g.source_scan_index) ||
            (g.recording_source && ram_required>resources.budget->available());
        lvm::SpectrumWorker::Compute disk_retry;
        if(g.recording_source) {
            const auto source=processed_recording_source(true,0,resources.budget);
            lvm::RecordingReadRequest read;read.range=lvm::AnalysisRange::selected(start,end);
            read.channels=visible_channels;read.block_bytes=1024*1024;read.block_rows=4096;
            std::vector<std::string> names;for(auto c:visible_channels)names.push_back(g.ds.names[c]);
            const auto timeline=g.overview.index;
            disk_retry=[source,read,resources,names,timeline](const std::atomic<bool>* cancel) {
                auto io=resources.budget->reserve(2ULL*1024*1024);
                if(!io)throw std::runtime_error("Insufficient memory for FFT source reading.");
                lvm::DiskSpectrumOptions options;options.timeline=timeline;
                return lvm::compute_disk_spectrum(source,read,names,resources,cancel,options);
            };
        }
        if(source_needed) {
            const auto source=processed_recording_source(true,0,resources.budget);
            lvm::RecordingReadRequest read;read.range=lvm::AnalysisRange::selected(start,end);
            read.channels=visible_channels;read.block_bytes=1024*1024;read.block_rows=4096;
            std::vector<std::string> names;
            for(auto c:visible_channels)names.push_back(g.ds.names[c]);
            const auto timeline=g.overview.index;
            auto compute=[source,read,resources,names,timeline](const std::atomic<bool>* cancel) {
                auto io=resources.budget->reserve(2ULL*1024*1024);
                if(!io)throw std::runtime_error("Insufficient memory for FFT source reading.");
                return lvm::compute_recording_spectrum(source,read,names,resources,cancel,timeline);
            };
            g.spec_attempted=true;g.spec_pending=true;g.spec_valid=false;
            g_spectrum_worker.submit_computation(std::move(compute),visible_channels,g.spec_generation);
            g.spec_visible_state=g.visible;return;
        }
        // Admit the snapshot before build_time_window_dataset allocates it.
        auto reservation=reserve_analysis_input(resources,static_cast<std::size_t>(hi-lo),visible_channels.size(),false);
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
            g_spectrum_worker.submit(std::move(view),visible_channels,g.spec_generation,std::move(transforms),resources,std::move(reservation),std::move(disk_retry),g.recording_source);
        } else {
            auto spectrum = lvm::compute_spectrum(view,0,nullptr,resources);
            if(!spectrum.ok && disk_retry && spectrum.error.rfind("FFT resources are insufficient",0)==0) {
                view={};reservation.reset();spectrum={};spectrum=disk_retry(nullptr);
            }
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
    if(g.overview.render_enabled && g.spec_valid) {
        try{current_recording_source();}
        catch(const std::exception& e){clear_spectrum_cache_state();g.spec.error=e.what();g.spec_attempted=true;return false;}
    }
    if (!g.spec_attempted || spectrum_needs_visible_channels()) compute_spectrum_from_current_source();
    return g.spec_valid;
}

} // namespace gui
