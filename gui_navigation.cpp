// Navigation: native viewer implementation.
#include "gui_navigation.hpp"
#include "gui_frf.hpp"
#include "gui_frf_render.hpp"
#include "gui_menu.hpp"
#include "gui_render.hpp"
#include "gui_render_data.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_status.hpp"

namespace gui {

void clamp_range(double& lo, double& hi, double minb, double maxb, double minw) {
    double w = hi - lo;
    const double full = maxb - minb;
    if (w < minw) w = minw;
    if (w > full) w = full;
    if (lo < minb) lo = minb;
    hi = lo + w;
    if (hi > maxb) { hi = maxb; lo = hi - w; if (lo < minb) lo = minb; }
}

bool active_axis(double*& lo, double*& hi, double& minb, double& maxb, double& minw) {
    if (g.mode == AnalysisMode::FRF) {
        if (!g.frf.result.ok || g.frf.pending) return false;
        lo = &g.frf.log_start; hi = &g.frf.log_end;
        minb = std::log10(g.frf.result.common().frequencies[1]);
        maxb = std::log10(g.frf.result.common().frequencies.back());
        minw = std::min(1e-6, (maxb-minb)*.01);
        return true;
    }
    if ((g.mode == AnalysisMode::FFT)) {
        if (!ensure_current_spectrum() || g.spec.freqs.size() < 2) return false;
        lo = &g.freq_start; hi = &g.freq_end;
        minb = 0.0; maxb = g.spec.nyquist;
        minw = (g.spec.freqs[1] - g.spec.freqs[0]) * 0.5;
        return true;
    }
    if (!has_data()) return false;
    lo = &g.win_start; hi = &g.win_end;
    minb = g.data_t0; maxb = g.data_t1;
    minw = std::max(g.approx_dt * 0.5, (g.data_t1 - g.data_t0) * 1e-6);
    return true;
}

void zoom_at(double center_frac, double factor) {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return;
    const double w = *hi - *lo;
    const double c = *lo + w * center_frac;
    double nw = w * factor;
    if (nw < minw) nw = minw;
    const double full = maxb - minb;
    if (nw > full) nw = full;
    double nlo = c - nw * center_frac;
    double nhi = nlo + nw;
    if (nlo < minb) { nlo = minb; nhi = nlo + nw; }
    if (nhi > maxb) { nhi = maxb; nlo = nhi - nw; if (nlo < minb) nlo = minb; }
    *lo = nlo;
    *hi = nhi;
    set_status();
    invalidate_plot();
}

void zoom_y_at(double center_frac, double factor) {
    if (g.mode == AnalysisMode::FRF) {
        double lo, hi; frf_y_range(lo, hi);
        (void)center_frac;
        const double width = std::clamp(hi*factor, 1e-6, 1e6);
        g.frf.y_min = 0.0; g.frf.y_max = width;
        g.frf.auto_y = false; set_status(); invalidate_plot(); return;
    }
    if (!has_data()) return;
    double ymin, ymax;
    current_time_yrange(ymin, ymax);
    if (!g.auto_y) { ymin = g.y_lock_min; ymax = g.y_lock_max; }
    const double w = ymax - ymin;
    if (w <= 0) return;
    const double c = ymin + w * center_frac;
    double nw = w * factor;
    const double minw = std::max(1e-12, w * 1e-6);
    if (nw < minw) nw = minw;
    double nlo = c - nw * center_frac;
    double nhi = nlo + nw;
    g.y_lock_min = nlo;
    g.y_lock_max = nhi;
    g.auto_y = false;
    if (g.autoy) { SendMessageW(g.autoy, BM_SETCHECK, BST_UNCHECKED, 0); InvalidateRect(g.autoy, nullptr, FALSE); }
    sync_menu();
    set_status();
    invalidate_plot();
}

void zoom_y_amp_at(double center_frac, double factor) {
    (void)center_frac;
    double ymax = visible_spectrum_ymax();
    if (ymax <= 0) ymax = 1.0;
    double ytop = ymax * 1.08;
    if (!g.auto_y_amp) ytop = g.y_amp_max;
    const double w = ytop;
    if (w <= 0) return;
    double nw = w * factor;
    const double minw = std::max(1e-12, w * 1e-6);
    if (nw < minw) nw = minw;
    g.y_amp_max = nw;
    g.auto_y_amp = false;
    set_status();
    invalidate_plot();
}

void pan_by(double frac) {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return;
    const double w = *hi - *lo;
    *lo += w * frac;
    *hi += w * frac;
    clamp_range(*lo, *hi, minb, maxb, minw);
    set_status();
    invalidate_plot();
}

bool prepare_plot_drag(int mx, int my) {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return false;
    g.drag_x = mx;
    g.drag_y = my;
    g.drag_lo = *lo;
    g.drag_hi = *hi;
    if (g.mode == AnalysisMode::FRF) { frf_y_range(g.drag_y_lo, g.drag_y_hi); return true; }
    if ((g.mode == AnalysisMode::FFT)) {
        if (g.auto_y_amp) {
            double ymax = visible_spectrum_ymax();
            if (ymax <= 0) ymax = 1.0;
            g.drag_y_hi = ymax * 1.08;
        } else {
            g.drag_y_hi = g.y_amp_max;
        }
        g.drag_y_lo = 0.0;
    } else {
        if (g.auto_y) {
            current_time_yrange(g.drag_y_lo, g.drag_y_hi);
        } else {
            g.drag_y_lo = g.y_lock_min;
            g.drag_y_hi = g.y_lock_max;
        }
    }
    return true;
}

void pan_y_by(double frac) {
    if (g.mode == AnalysisMode::FRF) {
        double lo, hi; frf_y_range(lo, hi);
        const double shift = hi*frac;
        g.frf.y_min = 0.0; g.frf.y_max = std::max(1e-6,hi+shift); g.frf.auto_y = false;
        set_status(); invalidate_plot(); return;
    }
    if ((g.mode == AnalysisMode::FFT)) {
        double ymax = visible_spectrum_ymax();
        if (ymax <= 0) ymax = 1.0;
        double ytop = ymax * 1.08;
        if (!g.auto_y_amp) ytop = g.y_amp_max;
        const double w = ytop;
        const double shift = w * frac;
        g.y_amp_max = ytop + shift;
        if (g.y_amp_max < 1e-12) g.y_amp_max = 1e-12;
        g.auto_y_amp = false;
        set_status();
        invalidate_plot();
        return;
    }
    if (!has_data()) return;
    double ymin, ymax;
    current_time_yrange(ymin, ymax);
    if (g.auto_y) {
        g.y_lock_min = ymin;
        g.y_lock_max = ymax;
        g.auto_y = false;
        if (g.autoy) { SendMessageW(g.autoy, BM_SETCHECK, BST_UNCHECKED, 0); InvalidateRect(g.autoy, nullptr, FALSE); }
        sync_menu();
    }
    const double w = g.y_lock_max - g.y_lock_min;
    const double shift = w * frac;
    g.y_lock_min += shift;
    g.y_lock_max += shift;
    set_status();
    invalidate_plot();
}

void goto_start() {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return;
    const double w = *hi - *lo;
    *lo = minb;
    *hi = minb + w;
    if (*hi > maxb) { *hi = maxb; *lo = maxb - w; if (*lo < minb) *lo = minb; }
    set_status();
    invalidate_plot();
}

void goto_end() {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return;
    const double w = *hi - *lo;
    *hi = maxb;
    *lo = maxb - w;
    if (*lo < minb) { *lo = minb; *hi = minb + w; if (*hi > maxb) *hi = maxb; }
    set_status();
    invalidate_plot();
}

void reset_view() {
    if (g.mode == AnalysisMode::FRF) { reset_frf_view(); return; }
    g.win_start = g.data_t0;
    g.win_end = g.data_t1;
    g.freq_start = 0.0;
    g.freq_end = g.spec_valid ? g.spec.nyquist : 1.0;
    set_status();
    invalidate_plot();
}

} // namespace gui
