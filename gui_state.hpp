#pragma once
#include "gui_platform.hpp"
#include "gui_theme.hpp"
#include "frf_analysis.hpp"

namespace gui {

extern std::wstring g_config_path;

struct DocumentHistory;

// Which read-outs to draw next to measurement markers (toggled from the
// "Measurements -> show at points" menu).
struct PointDisplay {
    bool number = true;   // #1, #2, �
    bool x = true;        // x coordinate
    bool y = true;        // y coordinate
    bool dx = true;       // Δx to the previous point
    bool dy = true;       // Δy to the previous point
    bool inv_dt = true;   // 1/Δt (Hz) in time mode only
    bool dist = false;    // Euclidean distance to the previous point
};

enum class AnalysisMode { Time, FFT, FRF };

enum class PointGroupMode : unsigned char {
    Time = 0,
    Frequency = 1,
    FRF = 2,
};

// A reference line the user dropped on the plot. `value` is in data units of
// the axis it pins (x for vertical, y for horizontal); `freq` records whether
// it belongs to the Hz view so it only shows in the matching mode.
struct GuideLine {
    bool vertical = true;
    double value = 0.0;
    AnalysisMode mode = AnalysisMode::Time;
};

struct PointGroup {
    std::wstring name;
    COLORREF color = RGB(0, 120, 215);
    bool visible = true;
    PointGroupMode mode = PointGroupMode::Time;
    PointDisplay display;
    std::vector<std::pair<double, double>> points;
};

struct HotkeyBinding {
    int command = 0;
    BYTE fvirt = FVIRTKEY;
    WORD key = 0;
};

enum class AsyncLoadStage : unsigned char {
    None = 0,
    ScanningRange = 1,
    LoadingFile = 2
};

struct FrfState {
    std::vector<int> inputs, outputs;
    // FRF follows the processed Time/FFT views by default. The user can still
    // explicitly switch to raw channels in the FRF panel.
    bool apply_processing = true;
    lvm::FrfOptions options;
    lvm::FrfBatchResult result;
    bool pending = false, attempted = false;
    std::uint64_t generation = 0;
    bool view_initialized = false, auto_y = true;
    double log_start = 0.0, log_end = 3.0;
    // Keep physical limits alongside their logarithms so the same calculated
    // FRF can be viewed on either a logarithmic or a linear frequency axis.
    double frequency_start = 1.0, frequency_end = 1000.0;
    bool logarithmic_frequency_axis = true;
    double y_min = 0.0, y_max = 1.0;
    bool show_reference_amplitude = true;
    double reference_height_fraction = .25;
    bool reference_auto_y = true;
    double reference_y_max = 1.0;
    // Display-only logarithmic smoothing band; 0 leaves the curve unsmoothed.
    double display_smoothing_octaves = 1.0 / 12.0;
    double source_start = 0.0, source_end = 0.0;
    bool from_selection = false;
    std::wstring input_name;
    std::vector<std::wstring> output_names;
    std::wstring processing_description;
};

// State which belongs to exactly one opened source.  App inherits this type so
// the established g.ds / g.frf access pattern stays intact for the active
// document; inactive documents own the same state in App::inactive_documents.
struct DocumentState {
    lvm::Dataset ds;
    std::vector<char> visible;
    std::vector<COLORREF> channel_colors;
    std::vector<MinMaxIndex> envelopes;
    std::vector<unsigned long long> envelope_serial;
    std::vector<std::wstring> channel_labels;  // user-editable display names
    std::wstring global_formula = L"x";
    std::vector<FormulaToken> global_formula_rpn;
    bool formula_runtime_dirty = true;
    bool formula_ini_deferred = false;
    bool global_formula_identity = true;
    bool global_formula_affine = true;
    double global_formula_mul = 1.0;
    double global_formula_add = 0.0;
    std::vector<std::wstring> channel_formulas;
    std::vector<std::vector<FormulaToken>> channel_formula_rpn;
    std::vector<char> channel_formula_identity;
    std::vector<TransformRuntimeKind> channel_transform_kind;
    std::vector<double> channel_transform_mul;
    std::vector<double> channel_transform_add;
    std::vector<std::vector<double>> transformed_channel_cache;
    std::vector<char> transformed_channel_cache_valid;
    std::vector<std::vector<double>> filtered_channel_cache;
    std::vector<char> filtered_channel_cache_valid;
    bool has_non_identity_formula = false;
    AnalysisMode mode = AnalysisMode::Time;
    FrfState frf;

    double data_t0 = 0.0, data_t1 = 1.0;
    double win_start = 0.0, win_end = 1.0;
    double freq_start = 0.0, freq_end = 1.0;
    double approx_dt = 1.0;

    lvm::Spectrum spec;
    bool spec_valid = false;
    bool spec_attempted = false;
    bool spec_pending = false;
    bool spec_fit_pending = false;
    std::uint64_t spec_generation = 0;
    std::vector<int> spec_channel_indices;
    std::vector<char> spec_visible_state;
    double cached_global_gap_step = 0.0;
    bool cached_global_gap_step_ready = false;

    bool visual_smooth = false;  // Catmull-Rom spline rendering (data unchanged)
    // Add per-curve line patterns and symbols so plots remain identifiable in
    // grayscale screenshots and printed exports.
    bool distinguish_curves = false;

    bool auto_y = true;            // auto-fit vertical scale (true=auto, false=fixed)
    double y_lock_min = -1.0, y_lock_max = 1.0;

    bool auto_y_amp = true;        // auto-fit amplitude in Hz mode
    double y_amp_max = 1.0;        // locked amplitude max in Hz mode

    bool measure_mode = false;
    bool snap_to_data = true;       // snap markers to the nearest real sample
    PointDisplay pdisp;             // which read-outs to draw at markers
    std::wstring axis_x_label = L"X"; // graph label shown on the X axis corner
    std::wstring axis_y_label = L"Y"; // graph label shown on the Y axis corner
    COLORREF marker_color = g_theme->marker_color;
    std::vector<PointGroup> point_groups;
    int active_point_group = -1;
    int time_active_point_group = -1;
    int freq_active_point_group = -1;
    int frf_active_point_group = -1;

    std::vector<GuideLine> guides;  // vertical / horizontal reference lines
    int pending_line = 0;           // 0 none, 1 active vertical tool, 2 active horizontal tool
    std::vector<HotkeyBinding> hotkeys;

    bool playing = false;
    bool playhead_active = false;
    double playhead = 0.0;
    double play_anchor_data = 0.0;        // signal time when playback (re)started
    LARGE_INTEGER play_anchor_qpc = {};   // performance counter at that moment
    double play_speed = 1.0;

    // Mapping cache from the last paint (data <-> pixels) for hit-testing.
    double vx0 = 0, vx1 = 1, vy0 = 0, vy1 = 1;
    RECT vrect = {0, 0, 1, 1};
    bool vvalid = false;

    struct Marker {
        double x = 0.0;
        double y = 0.0;
        std::wstring label;
        bool freq = false;
        AnalysisMode mode = AnalysisMode::Time;
        bool snapped = false;
        int channel = -1;
    };
    std::vector<Marker> markers;
    struct GapMarkerVisual {
        RECT rect = {0, 0, 0, 0};
        double duration = 0.0;
        long long estimated_missing_samples = 0;
    };
    struct TimeYRangeCache {
        bool valid = false;
        std::size_t lo = 0;
        std::size_t hi = 0;
        double win_start = 0.0;
        double win_end = 0.0;
        unsigned long long serial = 0;
        double ymin = -1.0;
        double ymax = 1.0;
    };
    std::vector<GapMarkerVisual> visible_gap_markers;
    TimeYRangeCache time_yrange_cache;
    unsigned long long plot_analysis_serial = 1;
    bool pending_marker = false;    // active marker tool; stays selected after placement
    int active_marker = -1;
    bool show_gap_markers = true;
    bool stitch_time_gaps = false;
    bool stitched_time_ready = false;
    std::vector<std::size_t> stitched_gap_right_indices;
    std::vector<double> stitched_gap_display_right;
    bool noise_threshold_enabled = false;
    double noise_threshold_min = -std::numeric_limits<double>::infinity();
    double noise_threshold_max = std::numeric_limits<double>::infinity();
    int noise_threshold_mode = FilterModeBandPass;
    int noise_threshold_topology = FilterTopologyButterworth;
    bool gap_details_visible = false;
    double gap_details_duration = 0.0;
    long long gap_details_missing_samples = 0;
    double gap_details_reference_step = 0.0;
    bool current_file_partial = false;
    std::wstring source_path;
    std::wstring file_name;
    // Empty until the document has been saved as, or opened from, an AMSignal project.
    std::wstring project_path;
    // Each source retains its history and saved revision while inactive.
    std::shared_ptr<DocumentHistory> history;
    std::uint64_t project_revision = 0;
    std::uint64_t next_project_revision = 0;
    std::uint64_t saved_project_revision = 0;
    bool project_dirty = false;
};

struct App : DocumentState {
    // Only one loader runs at a time.  Multi-select and multi-file drops are
    // queued here; each result is moved into a separate DocumentState.
    std::vector<DocumentState> inactive_documents;
    std::vector<std::wstring> pending_open_paths;
    bool light_mode = false;
    double light_mode_open_start = 0.0;
    double light_mode_open_end = 10.0;
    AsyncLoadStage async_load_stage = AsyncLoadStage::None;
    unsigned long long async_load_token = 0;
    std::shared_ptr<std::atomic<bool>> async_load_cancel_flag;
    std::wstring cached_scan_path;
    double cached_scan_start = 0.0;
    double cached_scan_end = 0.0;
    bool cached_scan_valid = false;
    std::shared_ptr<const lvm::ScanIndex> cached_scan_index;
    std::vector<std::wstring> recent_files;
    std::string last_error;

    HWND main = nullptr;
    HWND open = nullptr, savepng = nullptr, savecsv = nullptr;
    HWND document_selector = nullptr, document_close = nullptr;
    HWND mode_time = nullptr, mode_freq = nullptr, mode_frf = nullptr;
    HWND frf_panel = nullptr;
    HWND play = nullptr, measure = nullptr, marker_btn = nullptr;
    HWND vline_btn = nullptr, hline_btn = nullptr;
    HWND cursor_btn = nullptr, line_menu_btn = nullptr;
    HWND reset = nullptr, autoy = nullptr, sidepanel_btn = nullptr;
    HWND show_all_btn = nullptr, hide_all_btn = nullptr;
    HWND status = nullptr;
    std::vector<HWND> checks;
    std::vector<HWND> check_labels;
    HWND channel_edit = nullptr;
    int editing_channel = -1;
    std::vector<HWND> buttons;   // owner-drawn toolbar buttons
    HWND hovered_btn = nullptr;
    std::wstring status_text;
    std::wstring status_detail_text;
    COLORREF status_detail_color = RGB(0, 0, 0);
    std::wstring hover_status_text;  // shown in status bar when hovering toolbar buttons
    std::vector<int> toolbar_seps;

    bool side_panel_visible = true;
    int side_panel_tab = 0; // 0 = channels, 1 = points, 2 = filter
    bool frf_point_settings_open = false;
    int side_selected_channel = -1;
    int side_scroll_y = 0;
    int side_scroll_max = 0;
    int side_content_height_channels = 0;
    int side_content_height_points = 0;
    int side_content_height_filter = 0;
    HWND side_tab_channels = nullptr;
    HWND side_tab_points = nullptr;
    HWND side_tab_filter = nullptr;
    HWND side_channel_hint = nullptr;
    HWND side_filter_enable = nullptr;
    HWND side_filter_mode_label = nullptr;
    HWND side_filter_mode = nullptr;
    HWND side_filter_topology_label = nullptr;
    HWND side_filter_topology = nullptr;
    HWND side_filter_low_label = nullptr;
    HWND side_filter_low_value = nullptr;
    HWND side_filter_low_track = nullptr;
    HWND side_filter_high_label = nullptr;
    HWND side_filter_high_value = nullptr;
    HWND side_filter_high_track = nullptr;
    HWND side_global_formula_label = nullptr;
    HWND side_global_formula_edit = nullptr;
    HWND side_global_formula_apply = nullptr;
    HWND side_channel_separator = nullptr;
    HWND side_channel_formula_label = nullptr;
    HWND side_formula_edit = nullptr;
    HWND side_channel_color = nullptr;
    HWND side_formula_apply_selected = nullptr;
    HWND side_formula_apply_visible = nullptr;
    HWND side_formula_reset_selected = nullptr;
    HWND side_formula_reset_all = nullptr;
    std::vector<HWND> channel_coefficient_edits;
    HWND side_point_group_list = nullptr;
    HWND side_point_group_visible = nullptr;
    HWND side_point_group_new = nullptr;
    HWND side_point_group_delete = nullptr;
    HWND side_point_group_name = nullptr;
    HWND side_point_group_rename = nullptr;
    HWND side_point_color_current = nullptr;
    HWND side_point_group_color = nullptr;
    HWND side_point_label_groups = nullptr;
    bool updating_axis_label_edits = false;
    bool updating_noise_threshold_edits = false;
    std::vector<HWND> side_channel_controls;
    std::vector<HWND> side_filter_controls;
    std::vector<HWND> side_point_controls;

    HWND settings_wnd = nullptr; // measurement-point settings panel (modeless)
    HWND welcome_wnd = nullptr;  // start screen

    HMENU menu = nullptr;        // main menu bar
    HACCEL accel = nullptr;      // current accelerator table (rebuilt from hotkeys)
    HFONT ui_font = nullptr;     // Segoe UI for controls / labels
    HFONT menu_font = nullptr;   // menu bar font sized for owner-drawn top menu
    HFONT bold_font = nullptr;   // semibold for headings
    HFONT title_font = nullptr;  // large font for the welcome title
    HFONT axis_font = nullptr;   // 11px for axis tick labels
    // icon_font removed � toolbar now uses text labels with ui_font

    bool dragging = false;
    int drag_x = 0, drag_y = 0;
    double drag_lo = 0.0, drag_hi = 0.0;
    double drag_y_lo = 0.0, drag_y_hi = 0.0;
    bool gap_click_pending = false;
    int gap_click_index = -1;

    bool fft_window_active = false;
    double fft_window_start = 0.0, fft_window_end = 0.0;
    bool fft_selecting = false;
    int fft_select_anchor_x = 0, fft_select_current_x = 0;
    double fft_select_anchor_t = 0.0, fft_select_current_t = 0.0;

    double spec_source_start = 0.0, spec_source_end = 0.0;
    bool spec_source_from_selection = false;
    bool spec_source_valid = false;

    bool vertical_pan = true;  // enable vertical panning with left-drag

    HDC backbuffer_dc = nullptr;
    HBITMAP backbuffer_bmp = nullptr;
    HBITMAP backbuffer_prev_bmp = nullptr;
    int backbuffer_w = 0;
    int backbuffer_h = 0;
};

extern App g;

extern ULONG_PTR g_gdiplus_token;

bool has_data();

} // namespace gui
