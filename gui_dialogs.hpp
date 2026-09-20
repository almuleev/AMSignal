#pragma once
#include "gui_platform.hpp"

namespace gui {

struct NumericPromptState {
    HWND wnd = nullptr;
    HWND edit = nullptr;
    bool done = false;
    bool accepted = false;
    bool positive_only = true;
    double value = 1.0;
    std::wstring title;
    std::wstring label;
    std::wstring apply_text;
    std::wstring cancel_text;
    std::wstring invalid_text;
};

extern NumericPromptState g_numeric_prompt;

struct RangePromptState {
    HWND wnd = nullptr;
    HWND start_edit = nullptr;
    HWND end_edit = nullptr;
    bool done = false;
    bool accepted = false;
    double start_value = 0.0;
    double end_value = 0.0;
    double min_value = 0.0;
    double max_value = 0.0;
    std::wstring title;
    std::wstring info_label;
    std::wstring start_label;
    std::wstring end_label;
    std::wstring apply_text;
    std::wstring cancel_text;
    std::wstring invalid_start_text;
    std::wstring invalid_end_text;
};

extern RangePromptState g_range_prompt;

struct InfoPromptState {
    HWND wnd = nullptr;
    HWND ok_button = nullptr;
    bool done = false;
    bool error = false;
    std::wstring title;
    std::wstring message;
    std::wstring ok_text;
};

extern InfoPromptState g_info_prompt;

enum class ExportFileFormat {
    Txt = 0,
    Csv = 1,
    Lvm = 2,
};

// "Save as" deliberately separates data interchange from an AMSignal project.
// A project is an LVM-compatible snapshot with the complete, unmodified source
// data and all application metadata needed to restore the document.
enum class ExportSaveMode {
    OriginalData = 0,
    AppliedSettings = 1,
    Project = 2,
};

struct ExportPromptState {
    HWND wnd = nullptr;
    HWND save_mode_combo = nullptr;
    HWND format_combo = nullptr;
    HWND range_combo = nullptr;
    HWND include_channel_names_check = nullptr;
    HWND include_hidden_channels_check = nullptr;
    HWND include_points_check = nullptr;
    HWND include_markers_check = nullptr;
    HWND include_guides_check = nullptr;
    HWND include_formulas_check = nullptr;
    HWND include_filter_check = nullptr;
    HWND include_graph_settings_check = nullptr;
    bool done = false;
    bool accepted = false;
    ExportSaveMode save_mode = ExportSaveMode::OriginalData;
    ExportFileFormat selected_format = ExportFileFormat::Csv;
    ExportRangeMode selected_range = ExportRangeMode::Visible;
    bool apply_processing_to_data = true;
    bool include_channel_names = true;
    bool include_hidden_channels = false;
    bool include_points = true;
    bool include_markers = true;
    bool include_guides = true;
    bool include_formulas = true;
    bool include_filter_settings = true;
    bool include_graph_settings = true;
    std::wstring title;
    std::wstring intro;
    std::wstring save_mode_label_text;
    std::wstring save_original_text;
    std::wstring save_applied_text;
    std::wstring save_project_text;
    std::wstring format_label_text;
    std::wstring range_label_text;
    std::wstring format_txt_text;
    std::wstring format_csv_text;
    std::wstring format_lvm_text;
    std::wstring range_selected_text;
    std::wstring range_visible_text;
    std::wstring range_whole_text;
    std::wstring channel_names_text;
    std::wstring hidden_channels_text;
    std::wstring points_text;
    std::wstring markers_text;
    std::wstring guides_text;
    std::wstring formulas_text;
    std::wstring filter_text;
    std::wstring graph_settings_text;
    std::wstring continue_text;
    std::wstring cancel_text;
};

struct ExportOptions {
    ExportSaveMode save_mode = ExportSaveMode::OriginalData;
    ExportFileFormat format = ExportFileFormat::Csv;
    ExportRangeMode selected_range = ExportRangeMode::Visible;
    bool apply_processing_to_data = true;
    bool include_channel_names = true;
    bool include_hidden_channels = false;
    bool include_points = true;
    bool include_markers = true;
    bool include_guides = true;
    bool include_formulas = true;
    bool include_filter_settings = true;
    bool include_graph_settings = true;
    // Kept true for callers of the export API; the "Original data" UI mode
    // explicitly switches it off.
    bool include_metadata = true;
};

extern ExportPromptState g_export_prompt;

int prompt_button_width(HDC dc, const wchar_t* text, int min_width);

void draw_prompt_surface(HWND hwnd, HDC dc);

LRESULT CALLBACK InfoPromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

void show_styled_info_prompt(HWND owner, const wchar_t* title, const wchar_t* message, bool error);

// Modal Save / Don't save / Cancel prompt using the application's theme.
int show_styled_save_changes_prompt(HWND owner, const wchar_t* title, const wchar_t* message);

double normalize_prompt_bound(double value);

const wchar_t* speed_prompt_title_text();

const wchar_t* speed_prompt_label_text();

const wchar_t* speed_prompt_apply_text();

const wchar_t* speed_prompt_cancel_text();

const wchar_t* speed_prompt_invalid_text();

const wchar_t* guide_prompt_title_text(bool vertical);

const wchar_t* guide_prompt_label_text(bool vertical);

const wchar_t* guide_prompt_apply_text();

const wchar_t* guide_prompt_cancel_text();

const wchar_t* guide_prompt_invalid_text();

LRESULT CALLBACK NumericPromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

LRESULT CALLBACK RangePromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

ExportFileFormat export_prompt_selected_format();

ExportSaveMode export_prompt_selected_save_mode();

ExportRangeMode export_prompt_selected_range();

void sync_export_prompt_state_from_controls();

void update_export_prompt_controls();

LRESULT CALLBACK ExportPromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

bool prompt_numeric_value(const wchar_t* title, const wchar_t* label,
                          const wchar_t* apply_text, const wchar_t* cancel_text,
                          const wchar_t* invalid_text, double initial_value,
                          bool positive_only, double& out_value);

bool prompt_light_mode_window(double range_start, double range_end, double& out_start, double& out_end);

bool prompt_export_options(ExportOptions& out_options);

bool prompt_exact_guide_value(bool vertical, double& out_value);

bool prompt_custom_play_speed(double& out_speed);

} // namespace gui
