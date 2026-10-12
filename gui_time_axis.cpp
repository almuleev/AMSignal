// Time axis: native viewer implementation.
#include "gui_time_axis.hpp"
#include "gui_state.hpp"

namespace gui {

double precompute_global_gap_step(const std::vector<double>& time) {
    if (time.size() <= 1) return 0.0;
    std::vector<double> diffs;
    diffs.reserve(std::min<std::size_t>(4096, time.size() - 1));
    const std::size_t stride = std::max<std::size_t>(1, (time.size() - 1) / 4096);
    for (std::size_t i = 1; i < time.size() && diffs.size() < 4096; i += stride) {
        const double diff = time[i] - time[i - 1];
        if (std::isfinite(diff) && diff > 0.0) diffs.push_back(diff);
    }
    if (diffs.empty()) return 0.0;
    std::sort(diffs.begin(), diffs.end());
    const std::size_t mid = diffs.size() / 2;
    return (diffs.size() % 2 == 0)
        ? 0.5 * (diffs[mid - 1] + diffs[mid])
        : diffs[mid];
}

void ensure_global_gap_step_ready() {
    if (g.cached_global_gap_step_ready) return;
    if(g.overview.render_enabled && g.recording_rows)return; // Computed from original full source by the I/O worker.
    g.cached_global_gap_step = precompute_global_gap_step(g.ds.time);
    g.cached_global_gap_step_ready = true;
}

void invalidate_stitched_time_cache() {
    g.stitched_time_ready = false;
    g.stitched_gap_right_indices.clear();
    g.stitched_gap_display_right.clear();
}

void ensure_stitched_time_cache() {
    if (g.stitched_time_ready) return;
    if (g.ds.time.size() < 2) return;
    std::vector<double> intervals;
    intervals.reserve(g.ds.time.size() - 1);
    for (std::size_t i = 1; i < g.ds.time.size(); ++i) {
        const double duration = g.ds.time[i] - g.ds.time[i - 1];
        if (std::isfinite(duration) && duration > 0) intervals.push_back(duration);
    }
    const double step = lvm::typical_sample_spacing(intervals);
    if (!(std::isfinite(step) && step > 0.0)) return;
    const double threshold = step * lvm::sampling_gap_factor;
    std::vector<std::size_t> right_indices;
    std::vector<double> display_right;
    double previous_raw = g.ds.time.front(), previous_display = previous_raw;
    for (std::size_t i = 1; i < g.ds.time.size(); ++i) {
        const double duration = g.ds.time[i] - g.ds.time[i - 1];
        if (!std::isfinite(duration) || duration <= threshold) continue;
        // Accumulate continuous segment lengths, never subtract a huge total
        // outage from a huge timestamp: that loses the whole visible interval.
        previous_display += (g.ds.time[i - 1] - previous_raw) + step;
        previous_raw = g.ds.time[i];
        right_indices.push_back(i);
        display_right.push_back(previous_display);
    }
    g.stitched_gap_right_indices = std::move(right_indices);
    g.stitched_gap_display_right = std::move(display_right);
    g.stitched_time_ready = true;
}

double stitched_time_at_index(std::size_t index) {
    if (!g.stitch_time_gaps || index >= g.ds.time.size()) return index < g.ds.time.size() ? g.ds.time[index] : 0.0;
    ensure_stitched_time_cache();
    const auto position = std::upper_bound(g.stitched_gap_right_indices.begin(), g.stitched_gap_right_indices.end(), index);
    const std::size_t count = static_cast<std::size_t>(position - g.stitched_gap_right_indices.begin());
    if (!count) return g.ds.time[index];
    return g.stitched_gap_display_right[count - 1] +
        (g.ds.time[index] - g.ds.time[g.stitched_gap_right_indices[count - 1]]);
}

double stitched_time_from_raw(double time) {
    if (!g.stitch_time_gaps || g.ds.time.empty()) return time;
    ensure_stitched_time_cache();
    const auto& source = g.ds.time;
    if (time <= source.front()) return time;
    if (time >= source.back()) return stitched_time_at_index(source.size() - 1);
    const std::size_t right = static_cast<std::size_t>(std::lower_bound(source.begin(), source.end(), time) - source.begin());
    if (time == source[right]) return stitched_time_at_index(right);
    const double left_display = stitched_time_at_index(right - 1);
    const double fraction = (time - source[right - 1]) / (source[right] - source[right - 1]);
    return left_display + fraction * (stitched_time_at_index(right) - left_display);
}

double raw_time_from_stitched(double time) {
    if (!g.stitch_time_gaps || g.ds.time.empty()) return time;
    const double first = stitched_time_at_index(0);
    const double last = stitched_time_at_index(g.ds.time.size() - 1);
    if (time <= first) return g.ds.time.front();
    if (time >= last) return g.ds.time.back();
    // Search sample indices, not physical time. The number of iterations is
    // bounded by sample count even for astronomically long outages.
    std::size_t lo = 0, hi = g.ds.time.size() - 1;
    while (hi - lo > 1) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (stitched_time_at_index(mid) < time) lo = mid;
        else hi = mid;
    }
    const double left = stitched_time_at_index(lo), right = stitched_time_at_index(hi);
    if (time == right) return g.ds.time[hi];
    const double fraction = (time - left) / (right - left);
    return g.ds.time[lo] + fraction * (g.ds.time[hi] - g.ds.time[lo]);
}

} // namespace gui
