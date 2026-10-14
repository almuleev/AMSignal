// Render data: native viewer implementation.
#include "gui_render_data.hpp"
#include "gui_processing.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_overview.hpp"
#include "disk_spectrum.hpp"

namespace gui {

void invalidate_plot_analysis_cache() {
    ++g.plot_analysis_serial;
    g.time_yrange_cache.valid = false;
}

ChannelRenderView make_channel_render_view(std::size_t channel_index) {
    ChannelRenderView view;
    if (channel_index >= g.ds.channels.size()) return view;
    view.raw = &g.ds.channels[channel_index];
    view.kind = (channel_index < g.channel_transform_kind.size())
        ? g.channel_transform_kind[channel_index]
        : TransformRuntimeKind::Identity;
    if (g.noise_threshold_enabled) {
        ensure_filtered_channel_cache(channel_index);
        if (channel_index < g.filtered_channel_cache.size()) {
            view.cache = &g.filtered_channel_cache[channel_index];
        }
    } else if (view.kind == TransformRuntimeKind::Affine) {
        view.mul = g.channel_transform_mul[channel_index];
        view.add = g.channel_transform_add[channel_index];
    } else if (view.kind == TransformRuntimeKind::CachedFormula) {
        ensure_transformed_channel_cache(channel_index);
        view.cache = &g.transformed_channel_cache[channel_index];
    }
    return view;
}

const MinMaxIndex& channel_envelope(std::size_t channel, const ChannelRenderView& view) {
    g.envelopes.resize(g.ds.channel_count());
    g.envelope_serial.resize(g.ds.channel_count(), 0);
    if (g.envelope_serial[channel] != g.plot_analysis_serial) {
        g.envelopes[channel].build(g.ds.rows(), [&](std::size_t i) { return channel_render_value(view, i); });
        g.envelope_serial[channel] = g.plot_analysis_serial;
    }
    return g.envelopes[channel];
}

bool any_visible_channel() {
    for (char visible : g.visible) {
        if (visible) return true;
    }
    return false;
}

bool current_time_yrange_window(std::size_t lo, std::size_t hi, double& ymin, double& ymax) {
    if (!has_data() || hi <= lo) return false;
    if (!any_visible_channel()) {
        ymin = -1.0;
        ymax = 1.0;
        return true;
    }
    if (g.time_yrange_cache.valid &&
        g.time_yrange_cache.serial == g.plot_analysis_serial &&
        g.time_yrange_cache.lo == lo &&
        g.time_yrange_cache.hi == hi &&
        g.time_yrange_cache.win_start == g.win_start &&
        g.time_yrange_cache.win_end == g.win_end) {
        ymin = g.time_yrange_cache.ymin;
        ymax = g.time_yrange_cache.ymax;
        return true;
    }
    ensure_channel_formula_vectors();
    ymin = 1e300; ymax = -1e300;
    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        const ChannelRenderView view = make_channel_render_view(c);
        const auto range = channel_envelope(c, view).query(lo, hi, [&](std::size_t i) { return channel_render_value(view, i); });
        ymin = std::min(ymin, range.first);
        ymax = std::max(ymax, range.second);
    }
    if (ymin > ymax) { ymin = -1; ymax = 1; }
    if (ymax - ymin < 1e-12) { ymin -= 1; ymax += 1; }
    const double pad = (ymax - ymin) * 0.05;
    ymin -= pad; ymax += pad;
    g.time_yrange_cache.valid = true;
    g.time_yrange_cache.lo = lo;
    g.time_yrange_cache.hi = hi;
    g.time_yrange_cache.win_start = g.win_start;
    g.time_yrange_cache.win_end = g.win_end;
    g.time_yrange_cache.serial = g.plot_analysis_serial;
    g.time_yrange_cache.ymin = ymin;
    g.time_yrange_cache.ymax = ymax;
    return true;
}

// Auto-fit vertical range over the currently visible time window (with 5% pad).
bool current_time_yrange(double& ymin, double& ymax) {
    enable_background_signal_processing();
    if(g.overview.render_enabled)return overview_time_yrange(ymin,ymax);
    if (!has_data()) return false;
    const std::vector<double>& t = g.ds.time;
    const std::size_t n = t.size();
    std::size_t lo = static_cast<std::size_t>(
        std::lower_bound(t.begin(), t.end(), g.win_start) - t.begin());
    std::size_t hi = static_cast<std::size_t>(
        std::upper_bound(t.begin(), t.end(), g.win_end) - t.begin());
    if (lo > 0) --lo;
    if (hi < n) ++hi;
    return current_time_yrange_window(lo, hi, ymin, ymax);
}

// Auto-fit amplitude range over the currently visible FFT window (with 5% pad).
bool current_freq_yrange(double& ymin, double& ymax) {
    if (g.mode != AnalysisMode::FFT) return false;
    if (!ensure_current_spectrum() || g.spec.bin_count() < 2) return false;
    ymin = 0.0;
    ymax = 0.0;
    if(g.spec.disk) {
        const auto range=g.spec.bin_range(g.freq_start,g.freq_end);
        for(std::size_t j=0;j<g.spec.names.size();++j){const auto ci=g.spec_channel_indices.at(j);if(ci<0 || !g.visible[ci])continue;
            for(const auto& point:g.spec.disk->display_points(j,range.first,range.second-range.first,1024))ymax=std::max(ymax,point.second);}
        if(ymax<=0)ymax=1;
        ymax*=1.05;return true;
    }
    std::size_t klo = static_cast<std::size_t>(
        std::lower_bound(g.spec.freqs.begin(), g.spec.freqs.end(), g.freq_start) - g.spec.freqs.begin());
    std::size_t khi = static_cast<std::size_t>(
        std::upper_bound(g.spec.freqs.begin(), g.spec.freqs.end(), g.freq_end) - g.spec.freqs.begin());
    if (klo > 0) --klo;
    if (khi < g.spec.freqs.size()) ++khi;
    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
        const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
        if (ci < 0 || !g.visible[ci]) continue;
        for (std::size_t k = klo; k < khi; ++k) {
            if (g.spec.amp[j][k] > ymax) ymax = g.spec.amp[j][k];
        }
    }
    if (ymax <= 0.0) ymax = 1.0;
    const double pad = ymax * 0.05;
    ymax += pad;
    return true;
}

double visible_spectrum_ymax() {
    if (!ensure_current_spectrum()) return 0.0;
    double ymax = 0.0;
    if(g.spec.disk){for(std::size_t j=0;j<g.spec.names.size();++j){const auto ci=g.spec_channel_indices.at(j);
        if(ci>=0 && g.visible[ci])ymax=std::max(ymax,g.spec.disk->maxima[j]);}return ymax;}
    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
        const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
        if (ci < 0 || !g.visible[ci]) continue;
        for (double v : g.spec.amp[j]) {
            if (v > ymax) ymax = v;
        }
    }
    return ymax;
}

} // namespace gui
