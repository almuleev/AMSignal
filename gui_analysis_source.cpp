// Shared time selection and synchronized channel snapshots for GUI analyses.
#include "gui_analysis_source.hpp"
#include "gui_processing.hpp"
#include "gui_state.hpp"
#include "gui_frf.hpp"
#include "gui_spectrum.hpp"
#include "gui_state_history.hpp"

namespace gui {

std::shared_ptr<const lvm::RecordingSource> current_recording_source() {
    if(!g.recording_source) {
        if(g.ds.time.empty() || g.source_path.empty() || !g.loaded_source_stamp_valid ||
           g.ds.source_columns.size()!=g.ds.channel_count())
            throw lvm::RecordingFailure(lvm::RecordingError::InvalidRequest,"Document has no replayable recording source.");
        g.recording_source=lvm::RecordingSource::from_file(std::filesystem::path(g.source_path),
            {g.ds.time.front(),g.ds.time.back()},g.ds.source_columns,g.source_scan_index,
            lvm::RecordingStamp{g.loaded_source_size,g.loaded_source_modified});
    }
    g.recording_source->check_current();
    return g.recording_source;
}

bool has_fft_window() {
    return g.fft_window_active && g.fft_window_end > g.fft_window_start;
}

lvm::TimeRange analysis_recording_bounds() {
    if(!g.ds.frequency_axis) {
        if(g.recording_source)return g.recording_source->bounds();
        if(g.source_scan_index)return {g.source_scan_index->range_start,g.source_scan_index->range_end};
    }
    return {g.data_t0,g.data_t1};
}

void clear_fft_window() {
    invalidate_frf();
    g.fft_window_active = false;
    g.fft_window_start = 0.0;
    g.fft_window_end = 0.0;
    clear_spectrum_cache_state();
}

void clamp_time_window(double& start, double& end) {
    if (start > end) std::swap(start, end);
    const auto bounds=analysis_recording_bounds();
    start = std::max(start, bounds.start);
    end = std::min(end, bounds.end);
}

void set_fft_window(double start, double end) {
    clamp_time_window(start, end);
    if (end <= start) {
        clear_fft_window();
        return;
    }
    invalidate_frf();
    g.fft_window_active = true;
    g.fft_window_start = start;
    g.fft_window_end = end;
    clear_spectrum_cache_state();
}

bool current_fft_source_window(double& start, double& end, bool& from_selection) {
    if (!has_data()) return false;
    if (has_fft_window()) {
        start = g.fft_window_start;
        end = g.fft_window_end;
        from_selection = true;
        return true;
    }
    const auto bounds=analysis_recording_bounds();
    start = bounds.start;
    end = bounds.end;
    clamp_time_window(start, end);
    from_selection = false;
    return end > start;
}

bool change_analysis_range(lvm::AnalysisRange range) {
    lvm::TimeRange resolved;
    if(!has_data() || !range.resolve(analysis_recording_bounds(),resolved) || resolved.end<=resolved.start)return false;
    const auto before=capture_settings_snapshot();
    if(range.kind==lvm::AnalysisRange::Kind::EntireRecording)clear_fft_window();
    else set_fft_window(resolved.start,resolved.end);
    record_settings_change(before);
    return true;
}

bool fft_window_contains_time(double t) {
    return has_fft_window() && t >= g.fft_window_start && t <= g.fft_window_end;
}


bool build_time_window_dataset(const lvm::Dataset& in, double start, double end, lvm::Dataset& out,
                               const std::vector<std::size_t>* selected_channels, bool apply_processing) {
    out = lvm::Dataset{};
    out.stats = in.stats;
    out.frequency_axis = in.frequency_axis;
    out.export_comments = in.export_comments;
    out.ok = true;
    if (apply_processing) ensure_channel_formula_vectors();
    if (in.time.empty()) return true;

    std::vector<std::size_t> channel_indices;
    if (selected_channels && !selected_channels->empty()) {
        channel_indices = *selected_channels;
    } else {
        channel_indices.resize(in.channels.size());
        for (std::size_t i = 0; i < in.channels.size(); ++i) channel_indices[i] = i;
    }
    out.names.reserve(channel_indices.size());
    out.channels.resize(channel_indices.size());
    for (std::size_t channel_index : channel_indices) {
        if (channel_index < in.names.size()) out.names.push_back(in.names[channel_index]);
        else out.names.push_back("Channel_" + std::to_string(channel_index + 1));
    }

    const std::size_t lo = static_cast<std::size_t>(
        std::lower_bound(in.time.begin(), in.time.end(), start) - in.time.begin());
    const std::size_t hi = static_cast<std::size_t>(
        std::upper_bound(in.time.begin(), in.time.end(), end) - in.time.begin());
    if (lo >= hi) return true;

    out.time.assign(in.time.begin() + static_cast<std::ptrdiff_t>(lo),
                    in.time.begin() + static_cast<std::ptrdiff_t>(hi));
    for (std::size_t out_index = 0; out_index < channel_indices.size(); ++out_index) {
        const std::size_t c = channel_indices[out_index];
        auto& dst = out.channels[out_index];
        dst.reserve(hi - lo);
        const TransformRuntimeKind kind = (c < g.channel_transform_kind.size())
            ? g.channel_transform_kind[c]
            : TransformRuntimeKind::Identity;
        if (!apply_processing || kind == TransformRuntimeKind::Identity) {
            const std::size_t base = dst.size();
            dst.insert(dst.end(),
                       in.channels[c].begin() + static_cast<std::ptrdiff_t>(lo),
                       in.channels[c].begin() + static_cast<std::ptrdiff_t>(hi));
            if (apply_processing && g.noise_threshold_enabled) {
                for (std::size_t i = base; i < dst.size(); ++i) {
                    dst[i] = rendered_channel_sample(c, lo + (i - base));
                }
            }
            continue;
        }
        if (kind == TransformRuntimeKind::Affine) {
            for (std::size_t r = lo; r < hi; ++r) {
                dst.push_back(rendered_channel_sample(c, r));
            }
            continue;
        }
        ensure_transformed_channel_cache(c);
        const auto& cache = g.transformed_channel_cache[c];
        if(c>=g.transformed_channel_cache_valid.size() || !g.transformed_channel_cache_valid[c]) {
            for(std::size_t r=lo;r<hi;++r)dst.push_back(apply_processing && g.noise_threshold_enabled
                ? rendered_channel_sample(c,r) : transform_channel_value(c,in.channels[c][r]));
            continue;
        }
        const std::size_t base = dst.size();
        dst.insert(dst.end(),
                   cache.begin() + static_cast<std::ptrdiff_t>(lo),
                   cache.begin() + static_cast<std::ptrdiff_t>(hi));
        if (g.noise_threshold_enabled) {
            for (std::size_t i = base; i < dst.size(); ++i) {
                dst[i] = rendered_channel_sample(c, lo + (i - base));
            }
        }
    }
    return true;
}


} // namespace gui
