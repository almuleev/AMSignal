#pragma once
#include "gui_platform.hpp"
#include "gui_state.hpp"

namespace gui {

// ---- undo / redo system ------------------------------------------------
struct SettingsSnapshot {
    std::vector<char> visible;
    std::vector<std::wstring> channel_labels;
    std::vector<COLORREF> channel_colors;
    std::wstring global_formula;
    std::vector<std::wstring> channel_formulas;
    bool snap_to_data = true;
    COLORREF marker_color = RGB(0, 120, 215);
    std::vector<PointGroup> point_groups;
    int active_point_group = -1;
    int time_active_point_group = -1;
    int freq_active_point_group = -1;
    int frf_active_point_group = -1;
    std::vector<GuideLine> guides;
    std::vector<App::Marker> markers;
    int active_marker = -1;
    int pending_line = 0;
    bool pending_marker = false;
    bool measure_mode = false;
    bool auto_y = true;
    double y_lock_min = -1.0;
    double y_lock_max = 1.0;
    bool auto_y_amp = true;
    double y_amp_max = 1.0;
    bool distinguish_curves = false;
    bool noise_threshold_enabled = false;
    double noise_threshold_min = -std::numeric_limits<double>::infinity();
    double noise_threshold_max = std::numeric_limits<double>::infinity();
    int noise_threshold_mode = FilterModeBandPass;
    int noise_threshold_topology = FilterTopologyButterworth;
};

struct UndoAction {
    enum Type { NONE, ADD_POINT, ADD_LINE, ADD_MARKER, CLEAR_POINTS, CLEAR_LINES, CLEAR_MARKERS, SETTINGS_CHANGE } type = NONE;
    std::pair<double, double> point;
    int point_group_index = -1;
    bool point_group_created = false;
    PointGroupMode cleared_mode = PointGroupMode::Time;
    PointGroup point_group_state;
    GuideLine line;
    App::Marker marker;
    std::vector<PointGroup> saved_point_groups;
    int saved_active_point_group = -1;
    int saved_time_active_point_group = -1;
    int saved_freq_active_point_group = -1;
    int saved_frf_active_point_group = -1;
    std::vector<GuideLine> saved_lines;
    std::vector<App::Marker> saved_markers;
    SettingsSnapshot before_settings;
    SettingsSnapshot after_settings;
    std::uint64_t before_project_revision = 0;
    std::uint64_t after_project_revision = 0;
};

struct DocumentHistory {
    std::vector<UndoAction> undo;
    std::vector<UndoAction> redo;
    std::optional<SettingsSnapshot> filter_slider_before;
};

extern std::vector<UndoAction> g_undo;

extern std::vector<UndoAction> g_redo;

extern std::optional<SettingsSnapshot> g_filter_slider_before;

inline constexpr std::size_t kUndoActionLimit = 128;

inline constexpr std::size_t kUndoByteLimit = 64 * 1024 * 1024;

PointGroupMode current_point_group_mode();

int& active_point_group_index_for_mode(PointGroupMode mode);

bool point_group_matches_mode(const PointGroup& group, PointGroupMode mode);

std::size_t point_group_count_for_mode(PointGroupMode mode);

std::size_t point_group_mode_index(std::size_t index, PointGroupMode mode);

void shift_point_group_active_indices_after_insert(std::size_t index);

void shift_point_group_active_indices_after_erase(std::size_t index);

void normalize_active_point_group();

PointGroup* active_point_group();

const PointGroup* active_point_group_readonly();

std::size_t total_measure_point_count();

bool has_measure_points();

void clear_measure_point_groups_in_mode(PointGroupMode mode);

void clear_measure_point_groups();

void clear_all_measure_point_groups();

PointDisplay* active_point_display();

const PointDisplay* active_point_display_readonly();

void sync_point_display_from_active_group();

void erase_point_group(std::size_t index);

std::size_t insert_point_group(std::size_t index, const PointGroup& group);

int create_point_group(COLORREF color);

int ensure_point_group_for_measurement(bool force_new_group, bool* created_group = nullptr);

std::wstring point_group_list_label(std::size_t index, const PointGroup& group);

std::wstring measure_points_status_text();

std::size_t history_dynamic_bytes(const std::wstring& value);

std::size_t history_dynamic_bytes(const PointGroup& group);

std::size_t history_dynamic_bytes(const App::Marker& marker);

std::size_t history_dynamic_bytes(const SettingsSnapshot& snapshot);

std::size_t history_action_bytes(const UndoAction& action);

std::size_t history_stack_bytes(const std::vector<UndoAction>& stack);

void push_undo(UndoAction a);

void save_active_document_history();

void restore_active_document_history();

void clear_active_document_history();

SettingsSnapshot capture_settings_snapshot();

bool settings_snapshot_differs(const SettingsSnapshot& a, const SettingsSnapshot& b);

void sync_channel_controls_from_state();

void rebuild_formula_cache_from_state();

void recompute_transforms_from_state();

void apply_settings_snapshot(const SettingsSnapshot& snapshot);

bool record_settings_change(const SettingsSnapshot& before);

void pop_undo();

void pop_redo();

} // namespace gui
