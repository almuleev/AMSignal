// Processing: native viewer implementation.
#include "gui_processing.hpp"
#include "gui_frf.hpp"
#include "gui_menu.hpp"
#include "gui_render.hpp"
#include "gui_render_data.hpp"
#include "gui_settings.hpp"
#include "gui_side_panel.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_state_history.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"

namespace gui {
namespace {
// Retained processing cache budget per document; input Dataset is separate.
void make_processing_cache_room(std::size_t bytes,std::size_t protected_channel,bool filtered) {
    constexpr std::size_t budget=128ULL*1024*1024;
    std::size_t used=bytes;
    for(std::size_t c=0;c<g.ds.channel_count();++c) {
        if(c<g.transformed_channel_cache.size() && (filtered || c!=protected_channel))
            used+=g.transformed_channel_cache[c].capacity()*8;
        if(c<g.filtered_channel_cache.size() && (filtered ? c!=protected_channel : true))
            used+=g.filtered_channel_cache[c].capacity()*8;
    }
    if(used<=budget)return;
    if(filtered && bytes+g.transformed_channel_cache[protected_channel].capacity()*8>budget) {
        std::vector<double>().swap(g.transformed_channel_cache[protected_channel]);
        g.transformed_channel_cache_valid[protected_channel]=0;
    }
    for(std::size_t c=0;c<g.ds.channel_count();++c) {
        if(c==protected_channel)continue;
        if(c<g.transformed_channel_cache.size()) {
            std::vector<double>().swap(g.transformed_channel_cache[c]);g.transformed_channel_cache_valid[c]=0;
        }
        if(c<g.filtered_channel_cache.size()) {
            std::vector<double>().swap(g.filtered_channel_cache[c]);g.filtered_channel_cache_valid[c]=0;
        }
    }
}
}

std::wstring channel_display_label(std::size_t ci) {
    if (ci < g.channel_labels.size() && !g.channel_labels[ci].empty()) return g.channel_labels[ci];
    if (ci < g.ds.names.size() && !g.ds.names[ci].empty()) return to_w(g.ds.names[ci]);
    return std::wstring(L"Channel_") + std::to_wstring(ci + 1);
}

std::wstring channel_coefficient_text(std::size_t ci) {
    ensure_channel_formulas_loaded();
    ensure_channel_formula_storage();
    if (ci >= g.channel_formulas.size() || ci >= g.channel_formula_rpn.size()) return L"";
    if (g.channel_formula_rpn[ci].empty()) {
        std::wstring error;
        compile_formula_rpn(g.channel_formulas[ci], g.channel_formula_rpn[ci], error, g_str == &kEn);
    }
    const AffineFormulaInfo info = analyze_formula_rpn_affine(g.channel_formula_rpn[ci]);
    return info.valid && info.add == 0.0 ? format_edit_number(info.mul) : L"";
}

void invalidate_formula_runtime() {
    ++g.numerical_revision;
    g.formula_runtime_dirty = true;
    invalidate_transformed_channel_cache();
    invalidate_filtered_channel_cache();
    invalidate_plot_analysis_cache();
}

void invalidate_formula_runtime_channel(std::size_t channel_index) {
    ++g.numerical_revision;
    g.formula_runtime_dirty = true;
    if (channel_index < g.transformed_channel_cache_valid.size()) {
        g.transformed_channel_cache_valid[channel_index] = 0;
    } else {
        invalidate_transformed_channel_cache();
    }
    if (channel_index < g.filtered_channel_cache_valid.size()) {
        g.filtered_channel_cache_valid[channel_index] = 0;
    } else {
        invalidate_filtered_channel_cache();
    }
    invalidate_plot_analysis_cache();
}

void invalidate_transformed_channel_cache() {
    const std::size_t n = g.ds.channel_count();
    g.transformed_channel_cache.resize(n);
    g.transformed_channel_cache_valid.assign(n, 0);
}

void invalidate_filtered_channel_cache() {
    const std::size_t n = g.ds.channel_count();
    if (!g.noise_threshold_enabled) g.filtered_channel_cache.clear();
    g.filtered_channel_cache.resize(n);
    g.filtered_channel_cache_valid.assign(n, 0);
}

void ensure_channel_formula_storage() {
    const std::size_t n = g.ds.channel_count();
    if (g.channel_formulas.size() != n) g.channel_formulas.assign(n, default_channel_formula_text());
    if (g.channel_formula_rpn.size() != n) g.channel_formula_rpn.assign(n, {});
    if (g.channel_formula_identity.size() != n) g.channel_formula_identity.assign(n, 1);
    if (g.channel_transform_kind.size() != n) g.channel_transform_kind.assign(n, TransformRuntimeKind::Identity);
    if (g.channel_transform_mul.size() != n) g.channel_transform_mul.assign(n, 1.0);
    if (g.channel_transform_add.size() != n) g.channel_transform_add.assign(n, 0.0);
    if (g.transformed_channel_cache.size() != n || g.transformed_channel_cache_valid.size() != n) {
        invalidate_transformed_channel_cache();
    }
}

void ensure_channel_formulas_loaded() {
    if (!g.formula_ini_deferred) return;
    g.formula_ini_deferred = false;
    load_channel_formulas_from_ini();
}

void ensure_channel_formula_vectors() {
    const std::size_t n = g.ds.channel_count();
    ensure_channel_formula_storage();
    const bool vectors_ready =
        g.channel_formulas.size() == n &&
        g.channel_formula_rpn.size() == n &&
        g.channel_formula_identity.size() == n &&
        g.channel_transform_kind.size() == n &&
        g.channel_transform_mul.size() == n &&
        g.channel_transform_add.size() == n &&
        g.transformed_channel_cache.size() == n &&
        g.transformed_channel_cache_valid.size() == n;
    if (!g.formula_runtime_dirty && vectors_ready) return;

    ensure_channel_formulas_loaded();
    if (g.global_formula.empty()) g.global_formula = default_channel_formula_text();
    if (g.global_formula_rpn.empty()) {
        std::wstring error;
        compile_formula_rpn(g.global_formula, g.global_formula_rpn, error, g_str == &kEn);
        if (g.global_formula_rpn.empty()) {
            g.global_formula = default_channel_formula_text();
            compile_formula_rpn(g.global_formula, g.global_formula_rpn, error, g_str == &kEn);
        }
    }
    g.global_formula_identity = formula_rpn_is_identity(g.global_formula_rpn);
    const AffineFormulaInfo global_affine = analyze_formula_rpn_affine(g.global_formula_rpn);
    g.global_formula_affine = global_affine.valid;
    g.global_formula_mul = global_affine.valid ? global_affine.mul : 1.0;
    g.global_formula_add = global_affine.valid ? global_affine.add : 0.0;
    bool has_non_identity = !g.global_formula_identity;
    for (std::size_t i = 0; i < n; ++i) {
        if (g.channel_formulas[i].empty()) g.channel_formulas[i] = default_channel_formula_text();
        if (g.channel_formula_rpn[i].empty()) {
            std::wstring error;
            compile_formula_rpn(g.channel_formulas[i], g.channel_formula_rpn[i], error, g_str == &kEn);
        }
        g.channel_formula_identity[i] = formula_rpn_is_identity(g.channel_formula_rpn[i]) ? 1 : 0;
        const AffineFormulaInfo local_affine = analyze_formula_rpn_affine(g.channel_formula_rpn[i]);
        TransformRuntimeKind kind = TransformRuntimeKind::CachedFormula;
        double mul = 1.0;
        double add = 0.0;
        if (g.global_formula_affine && local_affine.valid) {
            mul = local_affine.mul * g.global_formula_mul;
            add = local_affine.mul * g.global_formula_add + local_affine.add;
            kind = (mul == 1.0 && add == 0.0) ? TransformRuntimeKind::Identity : TransformRuntimeKind::Affine;
        } else if (g.global_formula_identity && g.channel_formula_identity[i]) {
            kind = TransformRuntimeKind::Identity;
        }
        g.channel_transform_kind[i] = kind;
        if (kind != TransformRuntimeKind::CachedFormula) std::vector<double>().swap(g.transformed_channel_cache[i]);
        g.channel_transform_mul[i] = mul;
        g.channel_transform_add[i] = add;
        if (kind != TransformRuntimeKind::Identity) has_non_identity = true;
    }
    g.has_non_identity_formula = has_non_identity;
    g.formula_runtime_dirty = false;
}

void ensure_transformed_channel_cache(std::size_t channel_index) {
    ensure_channel_formula_vectors();
    if (channel_index >= g.ds.channel_count()) return;
    if (channel_index >= g.channel_transform_kind.size() ||
        g.channel_transform_kind[channel_index] != TransformRuntimeKind::CachedFormula) {
        return;
    }
    if (channel_index >= g.transformed_channel_cache_valid.size()) invalidate_transformed_channel_cache();
    if (g.transformed_channel_cache_valid[channel_index]) return;

    const auto& src = g.ds.channels[channel_index];
    if(src.size()*8>64ULL*1024*1024)return; // Formula evaluation has a per-sample fallback.
    make_processing_cache_room(src.size()*8,channel_index,false);
    auto& dst = g.transformed_channel_cache[channel_index];
    dst.resize(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst[i] = transform_channel_value(channel_index, src[i]);
    }
    g.transformed_channel_cache_valid[channel_index] = 1;
}

double transform_channel_value(std::size_t ci, double raw) {
    if (!std::isfinite(raw)) return std::numeric_limits<double>::quiet_NaN();
    if (!g.has_non_identity_formula) return raw;
    if (ci >= g.channel_transform_kind.size()) return raw;
    const TransformRuntimeKind kind = g.channel_transform_kind[ci];
    if (kind == TransformRuntimeKind::Identity) return raw;
    if (kind == TransformRuntimeKind::Affine) {
        return raw * g.channel_transform_mul[ci] + g.channel_transform_add[ci];
    }
    const double global_value = g.global_formula_identity
        ? raw
        : (g.global_formula_affine
            ? (raw * g.global_formula_mul + g.global_formula_add)
            : eval_formula_rpn(g.global_formula_rpn, raw));
    if (!std::isfinite(global_value)) return std::numeric_limits<double>::quiet_NaN();
    const double base = global_value;
    if (ci < g.channel_formula_identity.size() && g.channel_formula_identity[ci]) return base;
    const double value = eval_formula_rpn(g.channel_formula_rpn[ci], base);
    return std::isfinite(value) ? value : std::numeric_limits<double>::quiet_NaN();
}

std::wstring format_edit_number(double value) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%.12g", value);
    return buf;
}

std::wstring format_optional_edit_number(double value) {
    if (!std::isfinite(value)) return L"";
    return format_edit_number(value);
}

bool parse_wide_double_text(const wchar_t* text, double& out) {
    if (!text) return false;
    std::wstring s = text;
    for (wchar_t& ch : s) if (ch == L',') ch = L'.';
    const wchar_t* begin = s.c_str();
    while (*begin == L' ' || *begin == L'\t' || *begin == L'\r' || *begin == L'\n') ++begin;
    if (*begin == 0) return false;
    wchar_t* end = nullptr;
    double value = wcstod(begin, &end);
    if (begin == end) return false;
    while (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n') ++end;
    if (*end != 0 || !std::isfinite(value)) return false;
    out = value;
    return true;
}

double current_filter_sample_step() {
    if (!has_data() || g.ds.time.size() < 2) return 0.0;
    if (g.cached_global_gap_step_ready && g.cached_global_gap_step > 0.0 && std::isfinite(g.cached_global_gap_step)) {
        return g.cached_global_gap_step;
    }
    const double step = effective_time_gap_step(g.ds.time, 0, g.ds.time.size());
    if (step > 0.0 && std::isfinite(step)) {
        g.cached_global_gap_step = step;
        g.cached_global_gap_step_ready = true;
        return step;
    }
    return 0.0;
}

double current_filter_nyquist() {
    if (g.ds.frequency_axis) return 0.0;
    const double step = current_filter_sample_step();
    return (step > 0.0) ? (0.5 / step) : 0.0;
}

double clamp_filter_cutoff(double hz, double nyquist) {
    if (!std::isfinite(hz) || hz < 0.0) hz = 0.0;
    if (nyquist > 0.0 && hz > nyquist) hz = nyquist;
    return hz;
}

void normalize_filter_bounds() {
    g.noise_threshold_min = std::isfinite(g.noise_threshold_min) ? std::max(0.0, g.noise_threshold_min) : 0.0;
    g.noise_threshold_max = std::isfinite(g.noise_threshold_max) ? std::max(0.0, g.noise_threshold_max) : 0.0;
    if (g.noise_threshold_min > g.noise_threshold_max) {
        std::swap(g.noise_threshold_min, g.noise_threshold_max);
    }
}

double filter_slider_to_frequency(int pos, double nyquist) {
    if (!(nyquist > 0.0)) return 0.0;
    pos = std::clamp(pos, 0, static_cast<int>(kFilterSliderRange));
    if (pos <= 0) return 0.0;
    const double t = static_cast<double>(pos) / kFilterSliderRange;
    return nyquist * std::pow(t, kFilterSliderGamma);
}

int frequency_to_filter_slider(double hz, double nyquist) {
    if (!(nyquist > 0.0) || !(hz > 0.0)) return 0;
    hz = clamp_filter_cutoff(hz, nyquist);
    if (!(hz > 0.0)) return 0;
    const double t = std::pow(hz / nyquist, 1.0 / kFilterSliderGamma);
    return std::clamp(static_cast<int>(std::lround(t * kFilterSliderRange)), 0, static_cast<int>(kFilterSliderRange));
}

std::wstring filter_frequency_text(double hz) {
    if (!(hz > 0.0) || !std::isfinite(hz)) {
        return g_str == &kEn ? L"off" : L"выкл";
    }
    wchar_t buf[64];
    if (hz >= 1000.0) {
        swprintf(buf, 64, g_str == &kEn ? L"%.6g kHz" : L"%.6g кГц", hz / 1000.0);
    } else {
        swprintf(buf, 64, g_str == &kEn ? L"%.6g Hz" : L"%.6g Гц", hz);
    }
    return buf;
}

double transformed_channel_sample(std::size_t channel_index, std::size_t row_index) {
    if (channel_index >= g.ds.channel_count()) return std::numeric_limits<double>::quiet_NaN();
    const auto& column = g.ds.channels[channel_index];
    if (row_index >= column.size()) return std::numeric_limits<double>::quiet_NaN();

    const double raw = column[row_index];
    if (std::isnan(raw)) return raw;
    if (!g.has_non_identity_formula || channel_index >= g.channel_transform_kind.size()) {
        return raw;
    }
    const TransformRuntimeKind kind = g.channel_transform_kind[channel_index];
    if (kind == TransformRuntimeKind::Identity) return raw;
    if (kind == TransformRuntimeKind::Affine) {
        return raw * g.channel_transform_mul[channel_index] + g.channel_transform_add[channel_index];
    }
    ensure_transformed_channel_cache(channel_index);
    if (channel_index < g.transformed_channel_cache.size() &&
        g.transformed_channel_cache_valid[channel_index] &&
        row_index < g.transformed_channel_cache[channel_index].size()) {
        return g.transformed_channel_cache[channel_index][row_index];
    }
    return transform_channel_value(channel_index, raw);
}

double rendered_channel_sample(std::size_t channel_index, std::size_t row_index) {
    if (g.noise_threshold_enabled) {
        ensure_filtered_channel_cache(channel_index);
        if (channel_index < g.filtered_channel_cache.size() &&
            row_index < g.filtered_channel_cache[channel_index].size()) {
            return g.filtered_channel_cache[channel_index][row_index];
        }
    }
    return transformed_channel_sample(channel_index, row_index);
}

void ensure_filtered_channel_cache(std::size_t channel_index) {
    ensure_channel_formula_vectors();
    if (!g.noise_threshold_enabled || channel_index >= g.ds.channel_count()) return;
    if (channel_index >= g.filtered_channel_cache_valid.size()) invalidate_filtered_channel_cache();
    if (g.filtered_channel_cache_valid[channel_index]) return;
    make_processing_cache_room(g.ds.rows()*8,channel_index,true);
    const std::vector<double>* samples = &g.ds.channels[channel_index];
    std::vector<double> transformed;
    const auto kind = g.channel_transform_kind[channel_index];
    if (kind == TransformRuntimeKind::CachedFormula) {
        ensure_transformed_channel_cache(channel_index);
        if(g.transformed_channel_cache_valid[channel_index])samples=&g.transformed_channel_cache[channel_index];
        else {
            transformed.resize(g.ds.rows());
            for(std::size_t i=0;i<transformed.size();++i)transformed[i]=transform_channel_value(channel_index,(*samples)[i]);
            samples=&transformed;
        }
    } else if (kind == TransformRuntimeKind::Affine) {
        transformed.resize(g.ds.rows());
        for (std::size_t i = 0; i < transformed.size(); ++i) transformed[i] = transformed_channel_sample(channel_index, i);
        samples = &transformed;
    }
    FilterSettings settings;
    settings.mode = g.noise_threshold_mode;
    settings.topology = g.noise_threshold_topology;
    settings.low_cutoff = g.noise_threshold_min;
    settings.high_cutoff = g.noise_threshold_max;
    settings.sample_step = current_filter_sample_step();
    g.filtered_channel_cache[channel_index] = filter_signal(g.ds.time, *samples, settings);
    g.filtered_channel_cache_valid[channel_index] = 1;
    make_processing_cache_room(g.filtered_channel_cache[channel_index].capacity()*8,channel_index,true);
    if(g.filtered_channel_cache[channel_index].capacity()*8+
       g.transformed_channel_cache[channel_index].capacity()*8>128ULL*1024*1024) {
        std::vector<double>().swap(g.transformed_channel_cache[channel_index]);
        g.transformed_channel_cache_valid[channel_index]=0;
    }
}

void reset_channel_transform(std::size_t ci) {
    ensure_channel_formula_vectors();
    if (ci < g.channel_formulas.size()) g.channel_formulas[ci] = default_channel_formula_text();
    if (ci < g.channel_formula_rpn.size()) {
        g.channel_formula_rpn[ci].clear();
        std::wstring error;
        compile_formula_rpn(g.channel_formulas[ci], g.channel_formula_rpn[ci], error, g_str == &kEn);
    }
    invalidate_formula_runtime_channel(ci);
    ensure_channel_formula_vectors();
}

void reset_all_channel_transforms() {
    ensure_channel_formula_vectors();
    for (std::size_t i = 0; i < g.channel_formulas.size(); ++i) {
        reset_channel_transform(i);
    }
}

void clear_transform_sensitive_overlays(bool clear_history) {
    clear_all_measure_point_groups();
    g.markers.clear();
    g.active_marker = -1;
    g.guides.erase(
        std::remove_if(g.guides.begin(), g.guides.end(), [](const GuideLine& gl) { return !gl.vertical; }),
        g.guides.end());
    g.pending_line = 0;
    g.pending_marker = false;
    g.measure_mode = false;
    if (clear_history) {
        g_undo.clear();
        g_redo.clear();
    }
    if (g.measure) SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
}

void on_signal_transform_changed(bool preserve_history) {
    ++g.numerical_revision;
    if (!has_data()) return;
    clear_transform_sensitive_overlays(!preserve_history);
    g.auto_y = true;
    g.auto_y_amp = true;
    if (g.autoy) SendMessageW(g.autoy, BM_SETCHECK, BST_CHECKED, 0);
    invalidate_plot_analysis_cache();
    clear_spectrum_cache_state();
    if ((g.mode == AnalysisMode::FFT)) compute_spectrum();
    on_frf_processing_changed();
    sync_menu();
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
    save_runtime_settings();
}

void commit_filter_settings_change(const SettingsSnapshot& before) {
    if (!settings_snapshot_differs(before, capture_settings_snapshot())) return;
    if (before.noise_threshold_enabled || g.noise_threshold_enabled) clear_transform_sensitive_overlays(false);
    recompute_transforms_from_state();
    record_settings_change(before);
    save_runtime_settings();
    refresh_side_panel_controls();
    set_status();
    if (g.main) InvalidateRect(g.main, nullptr, TRUE);
}

void apply_filter_slider_change(bool low_cutoff, int position, bool preview) {
    const double nyquist = current_filter_nyquist();
    if (!(nyquist > 0)) return;
    const double value = filter_slider_to_frequency(position, nyquist);
    if (preview) {
        if (!g_filter_slider_before) g_filter_slider_before = capture_settings_snapshot();
        HWND label = low_cutoff ? g.side_filter_low_value : g.side_filter_high_value;
        if (label) SetWindowTextW(label, filter_frequency_text(value).c_str());
        return;
    }
    SettingsSnapshot before = g_filter_slider_before ? std::move(*g_filter_slider_before) : capture_settings_snapshot();
    g_filter_slider_before.reset();
    if (low_cutoff) g.noise_threshold_min = value;
    else g.noise_threshold_max = value;
    normalize_filter_bounds();
    commit_filter_settings_change(before);
}

bool read_formula_edit(HWND edit, std::wstring& formula, std::vector<FormulaToken>& compiled, std::wstring& error) {
    if (!edit) {
        error = (g_str == &kEn) ? L"Coefficient field is unavailable." : L"Поле коэффициента недоступно.";
        return false;
    }
    wchar_t buf[512]{};
    GetWindowTextW(edit, buf, 512);
    formula = normalize_formula_text(buf);
    if (formula.empty()) formula = default_channel_formula_text();
    return compile_formula_rpn(formula, compiled, error, g_str == &kEn);
}

void assign_formula_to_channel(std::size_t channel_index, const std::wstring& formula, const std::vector<FormulaToken>& compiled) {
    ensure_channel_formula_vectors();
    if (channel_index >= g.channel_formulas.size() || channel_index >= g.channel_formula_rpn.size()) return;
    g.channel_formulas[channel_index] = formula;
    g.channel_formula_rpn[channel_index] = compiled;
    invalidate_formula_runtime_channel(channel_index);
    ensure_channel_formula_vectors();
}

void assign_global_formula(const std::wstring& formula, const std::vector<FormulaToken>& compiled) {
    g.global_formula = formula;
    g.global_formula_rpn = compiled;
    invalidate_formula_runtime();
    ensure_channel_formula_vectors();
}

} // namespace gui
