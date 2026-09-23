// Settings: native viewer implementation.
#include "gui_settings.hpp"
#include "gui_hotkeys.hpp"
#include "gui_controls.hpp"
#include "gui_settings_window.hpp"
#include "gui_ids.hpp"
#include "gui_processing.hpp"
#include "gui_side_panel.hpp"
#include "gui_state.hpp"
#include "gui_state_history.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"

namespace gui {

std::wstring app_directory_path() {
    wchar_t path[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full = (len > 0) ? std::wstring(path, path + len) : L"";
    std::size_t slash = full.find_last_of(L"\\/");
    if (slash != std::wstring::npos) full.resize(slash + 1);
    else full.clear();
    return full;
}

std::wstring app_config_path() {
    return app_directory_path() + L"AMS.ini";
}

void load_app_settings() {
    if (g_config_path.empty()) g_config_path = app_config_path();
    wchar_t theme_buf[32]{};
    GetPrivateProfileStringW(L"ui", L"theme", L"light", theme_buf, 32, g_config_path.c_str());
    if (lstrcmpiW(theme_buf, L"dark") == 0) g_theme = &kDarkTheme;
    else g_theme = &kLightTheme;
}

void save_app_settings() {
    if (g_config_path.empty()) g_config_path = app_config_path();
    WritePrivateProfileStringW(
        L"ui",
        L"theme",
        (g_theme == &kDarkTheme) ? L"dark" : L"light",
        g_config_path.c_str());
}

bool g_settings_dirty = false;

std::wstring canonical_recent_file_path(const std::wstring& path) {
    wchar_t resolved[4096]{};
    if (path.empty()) return L"";
    constexpr DWORD resolved_count = static_cast<DWORD>(sizeof(resolved) / sizeof(resolved[0]));
    DWORD len = GetFullPathNameW(path.c_str(), resolved_count, resolved, nullptr);
    if (len > 0 && len < resolved_count) {
        return std::wstring(resolved, resolved + len);
    }
    return path;
}

void load_recent_files_from_ini() {
    if (g_config_path.empty()) g_config_path = app_config_path();
    g.recent_files.clear();
    for (std::size_t i = 0; i < kMaxRecentFiles; ++i) {
        wchar_t key[32]{};
        wchar_t value[4096]{};
        swprintf(key, 32, L"file_%u", static_cast<unsigned int>(i));
        constexpr DWORD value_count = static_cast<DWORD>(sizeof(value) / sizeof(value[0]));
        GetPrivateProfileStringW(L"recent", key, L"", value, value_count, g_config_path.c_str());
        if (value[0]) {
            std::wstring canonical = canonical_recent_file_path(value);
            if (canonical.empty()) continue;
            const bool seen = std::any_of(
                g.recent_files.begin(), g.recent_files.end(),
                [&](const std::wstring& item) {
                    return lstrcmpiW(item.c_str(), canonical.c_str()) == 0;
                });
            if (!seen) {
                g.recent_files.emplace_back(std::move(canonical));
            }
        }
    }
}

void save_recent_files_to_ini() {
    if (g_config_path.empty()) g_config_path = app_config_path();
    for (std::size_t i = 0; i < kMaxRecentFiles; ++i) {
        wchar_t key[32]{};
        swprintf(key, 32, L"file_%u", static_cast<unsigned int>(i));
        if (i < g.recent_files.size()) {
            WritePrivateProfileStringW(L"recent", key, g.recent_files[i].c_str(), g_config_path.c_str());
        } else {
            WritePrivateProfileStringW(L"recent", key, nullptr, g_config_path.c_str());
        }
    }
}

void add_recent_file(const std::wstring& path) {
    std::wstring canonical = canonical_recent_file_path(path);
    if (canonical.empty()) return;

    auto it = std::remove_if(g.recent_files.begin(), g.recent_files.end(),
                             [&](const std::wstring& item) {
                                 return lstrcmpiW(item.c_str(), canonical.c_str()) == 0;
                             });
    g.recent_files.erase(it, g.recent_files.end());
    g.recent_files.insert(g.recent_files.begin(), std::move(canonical));
    if (g.recent_files.size() > kMaxRecentFiles) {
        g.recent_files.resize(kMaxRecentFiles);
    }
    save_runtime_settings();
}

void save_runtime_settings_now() {
    if (g_config_path.empty()) g_config_path = app_config_path();

    WritePrivateProfileStringW(L"ui", L"language", (g_str == &kEn) ? L"en" : L"ru", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"smoothing", g.visual_smooth ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"curve_symbols", g.distinguish_curves ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"vertical_pan", g.vertical_pan ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"snap_to_data", g.snap_to_data ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"light_mode", g.light_mode ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"show_gap_markers", g.show_gap_markers ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"stitch_time_gaps", g.stitch_time_gaps ? L"1" : L"0", g_config_path.c_str());
    normalize_filter_bounds();
    WritePrivateProfileStringW(L"ui", L"filter_enabled", g.noise_threshold_enabled ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"filter_mode", std::to_wstring(g.noise_threshold_mode).c_str(), g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"filter_topology", std::to_wstring(g.noise_threshold_topology).c_str(), g_config_path.c_str());
    write_ini_optional_double(L"ui", L"filter_low_cutoff", g.noise_threshold_min);
    write_ini_optional_double(L"ui", L"filter_high_cutoff", g.noise_threshold_max);
    WritePrivateProfileStringW(L"ui", L"noise_threshold_enabled", nullptr, g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"noise_threshold_min", nullptr, g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"noise_threshold_max", nullptr, g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"side_panel_visible", g.side_panel_visible ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"side_panel_tab", std::to_wstring(std::clamp(g.side_panel_tab, 0, 2)).c_str(), g_config_path.c_str());
    write_ini_double(L"ui", L"play_speed", g.play_speed);
    write_ini_double(L"ui", L"light_mode_open_start", g.light_mode_open_start);
    write_ini_double(L"ui", L"light_mode_open_end", g.light_mode_open_end);
    WritePrivateProfileStringW(L"ui", L"axis_x_label", g.axis_x_label.c_str(), g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"axis_y_label", g.axis_y_label.c_str(), g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"x_label", nullptr, g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"y_label", nullptr, g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"number", g.pdisp.number ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"x", g.pdisp.x ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"y", g.pdisp.y ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"dx", g.pdisp.dx ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"dy", g.pdisp.dy ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"inv_dt", g.pdisp.inv_dt ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"dist", g.pdisp.dist ? L"1" : L"0", g_config_path.c_str());

    wchar_t color_buf[32]{};
    swprintf(color_buf, 32, L"%u", static_cast<unsigned int>(g.marker_color));
    WritePrivateProfileStringW(L"ui", L"marker_color", color_buf, g_config_path.c_str());
    if (!g.formula_ini_deferred) {
        WritePrivateProfileStringW(L"transform", L"global_formula", g.global_formula.c_str(), g_config_path.c_str());
        WritePrivateProfileStringW(L"transform", L"formula_count", nullptr, g_config_path.c_str());
        for (std::size_t i = 0; i < g.channel_formulas.size(); ++i) {
            wchar_t key_name[32]{};
            swprintf(key_name, 32, L"formula_%u", static_cast<unsigned int>(i));
            WritePrivateProfileStringW(L"transform", key_name, g.channel_formulas[i].c_str(), g_config_path.c_str());
        }
    }

    for (const auto& hk : g.hotkeys) {
        wchar_t key_name[32]{};
        wchar_t value[64]{};
        swprintf(key_name, 32, L"cmd_%d", hk.command);
        swprintf(value, 64, L"%u,%u", static_cast<unsigned int>(hk.fvirt), static_cast<unsigned int>(hk.key));
        WritePrivateProfileStringW(L"hotkeys", key_name, value, g_config_path.c_str());
    }
    save_recent_files_to_ini();
    save_app_settings();
}

void save_runtime_settings() {
    g_settings_dirty = true;
    if (g.main && IsWindow(g.main)) SetTimer(g.main, 3, 400, nullptr);
}

void apply_light_mode(bool enabled, bool persist) {
    const bool changed = g.light_mode != enabled;
    g.light_mode = enabled;
    if (!g.light_mode && g.formula_ini_deferred) {
        ensure_channel_formulas_loaded();
    }
    if (persist) save_runtime_settings();
    if (changed) {
        recompute_transforms_from_state();
        load_side_transform_controls();
        if (g.welcome_wnd && IsWindow(g.welcome_wnd)) {
            if (HWND light = GetDlgItem(g.welcome_wnd, IDW_LIGHT_MODE)) {
                set_toggle_checked(light, g.light_mode);
            }
            InvalidateRect(g.welcome_wnd, nullptr, FALSE);
        }
        if (g.settings_wnd) {
            if (HWND light = GetDlgItem(g.settings_wnd, IDW_LIGHT_MODE)) {
                set_toggle_checked(light, g.light_mode);
            }
            refresh_settings_controls();
        }
        if (g.main && IsWindow(g.main)) {
            InvalidateRect(g.main, nullptr, FALSE);
        }
    }
}

int read_ini_int(const wchar_t* section, const wchar_t* key, int def_value) {
    if (g_config_path.empty()) g_config_path = app_config_path();
    return static_cast<int>(GetPrivateProfileIntW(section, key, def_value, g_config_path.c_str()));
}

double read_ini_double(const wchar_t* section, const wchar_t* key, double def_value) {
    if (g_config_path.empty()) g_config_path = app_config_path();
    wchar_t buf[64]{};
    std::wstring def = format_edit_number(def_value);
    GetPrivateProfileStringW(section, key, def.c_str(), buf, 64, g_config_path.c_str());
    double value = def_value;
    if (parse_wide_double_text(buf, value)) return value;
    return def_value;
}

double read_ini_optional_double(const wchar_t* section, const wchar_t* key, double def_value) {
    if (g_config_path.empty()) g_config_path = app_config_path();
    wchar_t buf[64]{};
    GetPrivateProfileStringW(section, key, L"", buf, 64, g_config_path.c_str());
    double value = def_value;
    if (parse_wide_double_text(buf, value)) return value;
    return def_value;
}

std::wstring trim_wide_ascii(const std::wstring& text) {
    const wchar_t* ws = L" \t\r\n\f\v";
    const std::size_t begin = text.find_first_not_of(ws);
    if (begin == std::wstring::npos) return L"";
    const std::size_t end = text.find_last_not_of(ws);
    return text.substr(begin, end - begin + 1);
}

std::wstring read_ini_wstring(const wchar_t* section, const wchar_t* key, const std::wstring& def_value) {
    if (g_config_path.empty()) g_config_path = app_config_path();
    wchar_t buf[128]{};
    GetPrivateProfileStringW(section, key, def_value.c_str(), buf, 128, g_config_path.c_str());
    return trim_wide_ascii(buf);
}

std::wstring normalize_axis_label_text(const std::wstring& text, const wchar_t* fallback) {
    std::wstring out = trim_wide_ascii(text);
    if (out.empty()) out = fallback ? fallback : L"";
    return out;
}

void write_ini_double(const wchar_t* section, const wchar_t* key, double value) {
    if (g_config_path.empty()) g_config_path = app_config_path();
    std::wstring text = format_edit_number(value);
    WritePrivateProfileStringW(section, key, text.c_str(), g_config_path.c_str());
}

void write_ini_optional_double(const wchar_t* section, const wchar_t* key, double value) {
    if (g_config_path.empty()) g_config_path = app_config_path();
    if (!std::isfinite(value)) {
        WritePrivateProfileStringW(section, key, L"", g_config_path.c_str());
        return;
    }
    std::wstring text = format_edit_number(value);
    WritePrivateProfileStringW(section, key, text.c_str(), g_config_path.c_str());
}

void load_channel_formulas_from_ini() {
    ensure_channel_formula_storage();
    wchar_t global_buf[512]{};
    GetPrivateProfileStringW(L"transform", L"global_formula", default_channel_formula_text().c_str(), global_buf, 512, g_config_path.c_str());
    g.global_formula = normalize_formula_text(global_buf);
    g.global_formula_rpn.clear();
    std::wstring global_error;
    if (!compile_formula_rpn(g.global_formula, g.global_formula_rpn, global_error, g_str == &kEn)) {
        g.global_formula = default_channel_formula_text();
        g.global_formula_rpn.clear();
        compile_formula_rpn(g.global_formula, g.global_formula_rpn, global_error, g_str == &kEn);
    }
    for (std::size_t i = 0; i < g.channel_formulas.size(); ++i) {
        wchar_t key_name[32]{};
        swprintf(key_name, 32, L"formula_%u", static_cast<unsigned int>(i));
        wchar_t buf[512]{};
        GetPrivateProfileStringW(L"transform", key_name, default_channel_formula_text().c_str(), buf, 512, g_config_path.c_str());
        g.channel_formulas[i] = normalize_formula_text(buf);
        g.channel_formula_rpn[i].clear();
        std::wstring error;
        if (!compile_formula_rpn(g.channel_formulas[i], g.channel_formula_rpn[i], error, g_str == &kEn)) {
            g.channel_formulas[i] = default_channel_formula_text();
            g.channel_formula_rpn[i].clear();
            compile_formula_rpn(g.channel_formulas[i], g.channel_formula_rpn[i], error, g_str == &kEn);
        }
    }
    invalidate_formula_runtime();
    ensure_channel_formula_vectors();
}

void load_runtime_settings() {
    if (g_config_path.empty()) g_config_path = app_config_path();

    wchar_t lang_buf[16]{};
    GetPrivateProfileStringW(L"ui", L"language", L"ru", lang_buf, 16, g_config_path.c_str());
    g_str = (lstrcmpiW(lang_buf, L"en") == 0) ? &kEn : &kRu;

    g.visual_smooth = read_ini_int(L"ui", L"smoothing", g.visual_smooth ? 1 : 0) != 0;
    g.distinguish_curves = read_ini_int(L"ui", L"curve_symbols", g.distinguish_curves ? 1 : 0) != 0;
    g.vertical_pan = read_ini_int(L"ui", L"vertical_pan", g.vertical_pan ? 1 : 0) != 0;
    g.snap_to_data = read_ini_int(L"ui", L"snap_to_data", g.snap_to_data ? 1 : 0) != 0;
    g.show_gap_markers = read_ini_int(L"ui", L"show_gap_markers", g.show_gap_markers ? 1 : 0) != 0;
    g.stitch_time_gaps = read_ini_int(L"ui", L"stitch_time_gaps", g.stitch_time_gaps ? 1 : 0) != 0;
    g.noise_threshold_enabled = read_ini_int(L"ui", L"filter_enabled", g.noise_threshold_enabled ? 1 : 0) != 0;
    g.noise_threshold_mode = read_ini_int(L"ui", L"filter_mode", g.noise_threshold_mode);
    g.noise_threshold_topology = read_ini_int(L"ui", L"filter_topology", g.noise_threshold_topology);
    g.noise_threshold_min = read_ini_optional_double(L"ui", L"filter_low_cutoff", g.noise_threshold_min);
    g.noise_threshold_max = read_ini_optional_double(L"ui", L"filter_high_cutoff", g.noise_threshold_max);
    g.noise_threshold_mode = std::clamp(g.noise_threshold_mode,
                                        static_cast<int>(FilterModeLowPass),
                                        static_cast<int>(FilterModeBandStop));
    g.noise_threshold_topology = std::clamp(g.noise_threshold_topology,
                                            static_cast<int>(FilterTopologyButterworth),
                                            static_cast<int>(FilterTopologyLinkwitzRiley));
    normalize_filter_bounds();
    g.side_panel_visible = read_ini_int(L"ui", L"side_panel_visible", g.side_panel_visible ? 1 : 0) != 0;
    g.side_panel_tab = read_ini_int(L"ui", L"side_panel_tab", g.side_panel_tab);
    if (g.side_panel_tab < 0 || g.side_panel_tab > 2) g.side_panel_tab = 0;
    g.play_speed = read_ini_double(L"ui", L"play_speed", g.play_speed);
    if (!(g.play_speed > 0.0) || !std::isfinite(g.play_speed)) g.play_speed = 1.0;
    g.light_mode = read_ini_int(L"ui", L"light_mode", g.light_mode ? 1 : 0) != 0;
    g.light_mode_open_start = read_ini_double(L"ui", L"light_mode_open_start", g.light_mode_open_start);
    g.light_mode_open_end = read_ini_double(L"ui", L"light_mode_open_end", g.light_mode_open_end);
    if (!std::isfinite(g.light_mode_open_start) || g.light_mode_open_start < 0.0) g.light_mode_open_start = 0.0;
    if (!std::isfinite(g.light_mode_open_end) || g.light_mode_open_end <= g.light_mode_open_start) {
        g.light_mode_open_end = g.light_mode_open_start + 10.0;
    }

    g.pdisp.number = read_ini_int(L"points", L"number", g.pdisp.number ? 1 : 0) != 0;
    g.pdisp.x = read_ini_int(L"points", L"x", g.pdisp.x ? 1 : 0) != 0;
    g.pdisp.y = read_ini_int(L"points", L"y", g.pdisp.y ? 1 : 0) != 0;
    g.pdisp.dx = read_ini_int(L"points", L"dx", g.pdisp.dx ? 1 : 0) != 0;
    g.pdisp.dy = read_ini_int(L"points", L"dy", g.pdisp.dy ? 1 : 0) != 0;
    g.pdisp.inv_dt = read_ini_int(L"points", L"inv_dt", g.pdisp.inv_dt ? 1 : 0) != 0;
    g.pdisp.dist = read_ini_int(L"points", L"dist", g.pdisp.dist ? 1 : 0) != 0;
    g.axis_x_label = normalize_axis_label_text(
        read_ini_wstring(L"ui", L"axis_x_label", read_ini_wstring(L"points", L"x_label", g.axis_x_label)), L"X");
    g.axis_y_label = normalize_axis_label_text(
        read_ini_wstring(L"ui", L"axis_y_label", read_ini_wstring(L"points", L"y_label", g.axis_y_label)), L"ед.");
    // Prior versions stored the decorative default "Y" here. It is not a
    // physical unit, so replace it with the neutral unit placeholder.
    if (g.axis_y_label == L"Y") g.axis_y_label = L"ед.";

    g.marker_color = static_cast<COLORREF>(read_ini_int(
        L"ui", L"marker_color", static_cast<int>(g_theme->marker_color)));

    g.hotkeys = default_hotkeys();
    for (auto& hk : g.hotkeys) {
        wchar_t key_name[32]{};
        swprintf(key_name, 32, L"cmd_%d", hk.command);
        wchar_t value[64]{};
        GetPrivateProfileStringW(L"hotkeys", key_name, L"", value, 64, g_config_path.c_str());
        if (!value[0]) continue;
        unsigned int fvirt = hk.fvirt;
        unsigned int key = hk.key;
        if (swscanf(value, L"%u,%u", &fvirt, &key) == 2) {
            hk.fvirt = static_cast<BYTE>(fvirt);
            hk.key = static_cast<WORD>(key);
        }
    }
    load_recent_files_from_ini();
}

} // namespace gui
