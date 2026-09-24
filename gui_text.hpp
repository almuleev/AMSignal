#pragma once
#include "gui_platform.hpp"

namespace gui {

// ---- string table --------------------------------------------------------
struct Strings {
    const wchar_t* app_title;
    const wchar_t* btn_open; const wchar_t* btn_png; const wchar_t* btn_csv; const wchar_t* btn_play; const wchar_t* btn_pause;
    const wchar_t* btn_measure; const wchar_t* btn_reset; const wchar_t* btn_autoy;
    const wchar_t* st_time; const wchar_t* st_hz; const wchar_t* st_yauto; const wchar_t* st_yfix; const wchar_t* st_lines; const wchar_t* st_markers; const wchar_t* st_speed;
    const wchar_t* plot_xlabel_time; const wchar_t* plot_xlabel_freq;
    const wchar_t* fmt_pt_dx; const wchar_t* fmt_pt_dy; const wchar_t* fmt_pt_invdt; const wchar_t* fmt_pt_dist;
    const wchar_t* dlg_hotkeys_title;
    const wchar_t* msg_nodata; const wchar_t* msg_openfirst; const wchar_t* msg_savepng_err; const wchar_t* msg_read_err;
    const wchar_t* welcome_title;
    const wchar_t* welcome_btn_recent; const wchar_t* welcome_btn_hotkeys; const wchar_t* welcome_btn_start;
    const wchar_t* hover_open; const wchar_t* hover_png; const wchar_t* hover_play; const wchar_t* hover_pause; const wchar_t* hover_measure; const wchar_t* hover_reset; const wchar_t* hover_autoy;
    const wchar_t* lang_ru; const wchar_t* lang_en;
    const wchar_t* m_lang;
    const wchar_t* light_mode;
    const wchar_t* light_mode_status;
    const wchar_t* light_mode_range_title;
    const wchar_t* light_mode_range_start;
    const wchar_t* light_mode_range_end;
    const wchar_t* light_mode_range_apply;
    const wchar_t* light_mode_range_invalid_end;
    const wchar_t* msg_loading;
    const wchar_t* msg_loading_light;
    const wchar_t* msg_scanning_range;
    const wchar_t* msg_delta_f;
    const wchar_t* msg_delta_t;
    const wchar_t* st_spline;
    const wchar_t* fmt_hz;
    const wchar_t* fmt_sec;
    const wchar_t* fmt_y;
    const wchar_t* unit_hz;
    const wchar_t* unit_sec;
    const wchar_t* theme_light;
    const wchar_t* theme_dark;
    const wchar_t* msg_error_title;
    const wchar_t* msg_saved_png;
    const wchar_t* filter_open;
    const wchar_t* filter_png;
    const wchar_t* csv_time;
    const wchar_t* csv_freq;
    const wchar_t* status_vline;
    const wchar_t* status_hline;
    const wchar_t* status_marker;
};

extern const Strings kRu;

extern const Strings kEn;

extern const Strings* g_str;

const wchar_t* gap_markers_toggle_text();

const wchar_t* filter_toggle_text();

const wchar_t* filter_low_cutoff_text();

const wchar_t* filter_high_cutoff_text();

const wchar_t* filter_section_title_text();

const wchar_t* filter_mode_label_text();

const wchar_t* filter_topology_label_text();

const wchar_t* filter_mode_lowpass_text();

const wchar_t* filter_mode_highpass_text();

const wchar_t* filter_mode_bandpass_text();

const wchar_t* filter_mode_bandstop_text();

const wchar_t* filter_topology_butterworth_text();

const wchar_t* filter_topology_bessel_text();

const wchar_t* filter_topology_chebyshev_text();

const wchar_t* filter_topology_linkwitz_text();

const wchar_t* side_global_formula_label_text();

const wchar_t* side_global_formula_apply_text();

const wchar_t* side_channel_formula_label_text();

const wchar_t* point_group_list_title();

// Compact labels for the three top-toolbar modes. Keep these separate from
// the parameterized status-bar templates in Strings.
const wchar_t* mode_time_text();

const wchar_t* mode_spectrum_text();

const wchar_t* mode_frf_text();

const wchar_t* point_current_color_button_text();

const wchar_t* point_selected_group_color_button_text();

const wchar_t* point_group_visible_text();

const wchar_t* point_group_new_button_text();

const wchar_t* point_group_empty_text();

const wchar_t* side_panel_button_text();

const wchar_t* side_tab_channels_text();

const wchar_t* side_tab_points_text();

const wchar_t* side_tab_filter_text();

const wchar_t* side_channel_color_button_text();

const wchar_t* side_channel_hint_text();

const wchar_t* side_formula_apply_selected_text();

const wchar_t* side_formula_apply_visible_text();

const wchar_t* side_formula_reset_selected_text();

const wchar_t* side_formula_reset_all_text();

const wchar_t* side_point_group_delete_text();

const wchar_t* side_point_group_rename_text();

const wchar_t* side_pt_num_text();

const wchar_t* side_pt_x_text();

const wchar_t* stitch_gaps_toggle_text();

const wchar_t* side_pt_y_text();

const wchar_t* side_pt_dx_text();

const wchar_t* side_pt_dy_text();

const wchar_t* side_pt_invdt_text();

const wchar_t* side_pt_dist_text();

const wchar_t* side_pt_snap_text();

std::wstring to_w(const std::string& s);

std::wstring to_w_acp(const std::string& s);

std::string to_utf8(const std::wstring& w);

std::string numfmt(double v);

} // namespace gui
