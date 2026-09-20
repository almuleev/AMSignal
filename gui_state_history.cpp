// State history: native viewer implementation.
#include "gui_state_history.hpp"
#include "gui_frf.hpp"
#include "gui_controls.hpp"
#include "gui_documents.hpp"
#include "gui_menu.hpp"
#include "gui_settings_window.hpp"
#include "gui_processing.hpp"
#include "gui_render_data.hpp"
#include "gui_settings.hpp"
#include "gui_side_panel.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"

namespace gui {

std::vector<UndoAction> g_undo;

std::vector<UndoAction> g_redo;

std::optional<SettingsSnapshot> g_filter_slider_before;

PointGroupMode current_point_group_mode() {
    if (g.mode == AnalysisMode::FFT) return PointGroupMode::Frequency;
    if (g.mode == AnalysisMode::FRF) return PointGroupMode::FRF;
    return PointGroupMode::Time;
}

int& active_point_group_index_for_mode(PointGroupMode mode) {
    if (mode == PointGroupMode::Frequency) return g.freq_active_point_group;
    if (mode == PointGroupMode::FRF) return g.frf_active_point_group;
    return g.time_active_point_group;
}

bool point_group_matches_mode(const PointGroup& group, PointGroupMode mode) {
    return group.mode == mode;
}

std::size_t point_group_count_for_mode(PointGroupMode mode) {
    std::size_t count = 0;
    for (const auto& group : g.point_groups) {
        if (point_group_matches_mode(group, mode)) ++count;
    }
    return count;
}

std::size_t point_group_mode_index(std::size_t index, PointGroupMode mode) {
    std::size_t count = 0;
    for (std::size_t i = 0; i <= index && i < g.point_groups.size(); ++i) {
        if (point_group_matches_mode(g.point_groups[i], mode)) ++count;
    }
    return count;
}

void shift_point_group_active_indices_after_insert(std::size_t index) {
    auto bump = [&](int& active) {
        if (active >= static_cast<int>(index)) ++active;
    };
    bump(g.active_point_group);
    bump(g.time_active_point_group);
    bump(g.freq_active_point_group);
    bump(g.frf_active_point_group);
}

void shift_point_group_active_indices_after_erase(std::size_t index) {
    auto fix = [&](int& active) {
        if (active > static_cast<int>(index)) {
            --active;
        } else if (active == static_cast<int>(index)) {
            active = -1;
        }
    };
    fix(g.active_point_group);
    fix(g.time_active_point_group);
    fix(g.freq_active_point_group);
    fix(g.frf_active_point_group);
}

void normalize_active_point_group() {
    const PointGroupMode mode = current_point_group_mode();
    int& stored_active = active_point_group_index_for_mode(mode);
    if (stored_active >= 0 &&
        stored_active < static_cast<int>(g.point_groups.size()) &&
        point_group_matches_mode(g.point_groups[static_cast<std::size_t>(stored_active)], mode)) {
        g.active_point_group = stored_active;
        return;
    }

    int fallback = -1;
    for (int i = static_cast<int>(g.point_groups.size()) - 1; i >= 0; --i) {
        if (point_group_matches_mode(g.point_groups[static_cast<std::size_t>(i)], mode)) {
            fallback = i;
            break;
        }
    }
    stored_active = fallback;
    g.active_point_group = fallback;
}

PointGroup* active_point_group() {
    normalize_active_point_group();
    if (g.active_point_group < 0) return nullptr;
    return &g.point_groups[static_cast<std::size_t>(g.active_point_group)];
}

const PointGroup* active_point_group_readonly() {
    normalize_active_point_group();
    if (g.active_point_group < 0) return nullptr;
    return &g.point_groups[static_cast<std::size_t>(g.active_point_group)];
}

std::size_t total_measure_point_count() {
    const PointGroupMode mode = current_point_group_mode();
    std::size_t total = 0;
    for (const auto& group : g.point_groups) {
        if (point_group_matches_mode(group, mode)) total += group.points.size();
    }
    return total;
}

bool has_measure_points() {
    return total_measure_point_count() != 0;
}

void clear_measure_point_groups_in_mode(PointGroupMode mode) {
    const bool clearing_current_mode = (mode == current_point_group_mode());
    for (std::size_t i = g.point_groups.size(); i-- > 0; ) {
        if (point_group_matches_mode(g.point_groups[i], mode)) {
            g.point_groups.erase(g.point_groups.begin() + static_cast<std::ptrdiff_t>(i));
            shift_point_group_active_indices_after_erase(i);
        }
    }
    active_point_group_index_for_mode(mode) = -1;
    if (clearing_current_mode) g.active_point_group = -1;
}

void clear_measure_point_groups() {
    clear_measure_point_groups_in_mode(current_point_group_mode());
}

void clear_all_measure_point_groups() {
    g.point_groups.clear();
    g.active_point_group = -1;
    g.time_active_point_group = -1;
    g.freq_active_point_group = -1;
    g.frf_active_point_group = -1;
}

PointDisplay* active_point_display() {
    if (PointGroup* group = active_point_group()) return &group->display;
    return nullptr;
}

const PointDisplay* active_point_display_readonly() {
    if (const PointGroup* group = active_point_group_readonly()) return &group->display;
    return nullptr;
}

void sync_point_display_from_active_group() {
    if (const PointDisplay* display = active_point_display_readonly()) {
        g.pdisp = *display;
    }
}

void erase_point_group(std::size_t index) {
    if (index >= g.point_groups.size()) return;
    g.point_groups.erase(g.point_groups.begin() + static_cast<std::ptrdiff_t>(index));
    shift_point_group_active_indices_after_erase(index);
    if (!g.point_groups.empty()) {
        normalize_active_point_group();
        sync_point_display_from_active_group();
    }
}

std::size_t insert_point_group(std::size_t index, const PointGroup& group) {
    const std::size_t insert_at = std::min(index, g.point_groups.size());
    g.point_groups.insert(g.point_groups.begin() + static_cast<std::ptrdiff_t>(insert_at), group);
    shift_point_group_active_indices_after_insert(insert_at);
    return insert_at;
}

int create_point_group(COLORREF color) {
    PointGroup group;
    const PointGroupMode mode = current_point_group_mode();
    group.mode = mode;
    const std::size_t index = point_group_count_for_mode(mode);
    if (g_str == &kEn) group.name = L"Group " + std::to_wstring(index + 1);
    else group.name = L"Группа " + std::to_wstring(index + 1);
    group.color = color;
    group.visible = true;
    if (const PointDisplay* active_display = active_point_display_readonly()) {
        group.display = *active_display;
    } else {
        group.display = g.pdisp;
    }
    g.point_groups.push_back(group);
    g.active_point_group = static_cast<int>(g.point_groups.size()) - 1;
    active_point_group_index_for_mode(mode) = g.active_point_group;
    sync_point_display_from_active_group();
    return g.active_point_group;
}

int ensure_point_group_for_measurement(bool force_new_group, bool* created_group) {
    bool created = false;
    normalize_active_point_group();
    PointGroup* group = active_point_group();
    if (!group) {
        create_point_group(g.marker_color);
        created = true;
        group = active_point_group();
    } else {
        const bool active_has_points = !group->points.empty();
        if (force_new_group || (active_has_points && group->color != g.marker_color)) {
            create_point_group(g.marker_color);
            created = true;
            group = active_point_group();
        } else if (!active_has_points) {
            group->color = g.marker_color;
        }
    }
    if (group) group->visible = true;
    if (created_group) *created_group = created;
    return g.active_point_group;
}

std::wstring point_group_list_label(std::size_t index, const PointGroup& group) {
    wchar_t buf[160];
    const std::wstring base_name = group.name.empty()
        ? ((g_str == &kEn)
            ? (L"Group " + std::to_wstring(point_group_mode_index(index, group.mode)))
            : (L"Группа " + std::to_wstring(point_group_mode_index(index, group.mode))))
        : group.name;
    const wchar_t* status = L"";
    if (!group.visible) {
        status = (g_str == &kEn) ? L" [hidden]" : L" [скрыта]";
    } else if (static_cast<int>(index) == g.active_point_group) {
        status = (g_str == &kEn) ? L" [active]" : L" [активна]";
    }
    if (g_str == &kEn) {
        swprintf(buf, 160, L"%ls — %zu pts%s", base_name.c_str(), group.points.size(), status);
    } else {
        swprintf(buf, 160, L"%ls — %zu тчк%s", base_name.c_str(), group.points.size(), status);
    }
    return buf;
}

std::wstring measure_points_status_text() {
    const std::size_t total_points = total_measure_point_count();
    if (!total_points) return L"";
    std::size_t visible_groups = 0;
    for (const auto& group : g.point_groups) {
        if (point_group_matches_mode(group, current_point_group_mode()) &&
            group.visible && !group.points.empty()) {
            ++visible_groups;
        }
    }
    const std::size_t total_groups = point_group_count_for_mode(current_point_group_mode());
    wchar_t buf[160];
    if (g_str == &kEn) {
        swprintf(buf, 160, L"   |   Points: %zu in %zu groups (%zu visible)",
            total_points, total_groups, visible_groups);
    } else {
        swprintf(buf, 160, L"   |   Точки: %zu в %zu группах (%zu видимых)",
            total_points, total_groups, visible_groups);
    }
    return buf;
}

std::size_t history_dynamic_bytes(const std::wstring& value) { return value.capacity() * sizeof(wchar_t); }

template<class T> std::size_t history_dynamic_bytes(const T&) { return 0; }

template<class T> std::size_t history_dynamic_bytes(const std::vector<T>& values) {
    std::size_t bytes = values.capacity() * sizeof(T);
    for (const auto& value : values) bytes += history_dynamic_bytes(value);
    return bytes;
}

std::size_t history_dynamic_bytes(const PointGroup& group) {
    return history_dynamic_bytes(group.name) + history_dynamic_bytes(group.points);
}

std::size_t history_dynamic_bytes(const App::Marker& marker) { return history_dynamic_bytes(marker.label); }

std::size_t history_dynamic_bytes(const SettingsSnapshot& snapshot) {
    return history_dynamic_bytes(snapshot.visible) + history_dynamic_bytes(snapshot.channel_labels) +
        history_dynamic_bytes(snapshot.channel_colors) + history_dynamic_bytes(snapshot.global_formula) +
        history_dynamic_bytes(snapshot.channel_formulas) + history_dynamic_bytes(snapshot.point_groups) +
        history_dynamic_bytes(snapshot.guides) + history_dynamic_bytes(snapshot.markers);
}

std::size_t history_action_bytes(const UndoAction& action) {
    return sizeof(action) + history_dynamic_bytes(action.point_group_state) + history_dynamic_bytes(action.marker) +
        history_dynamic_bytes(action.saved_point_groups) + history_dynamic_bytes(action.saved_lines) +
        history_dynamic_bytes(action.saved_markers) + history_dynamic_bytes(action.before_settings) +
        history_dynamic_bytes(action.after_settings);
}

std::size_t history_stack_bytes(const std::vector<UndoAction>& stack) {
    std::size_t bytes = 0;
    for (const auto& action : stack) bytes += history_action_bytes(action);
    return bytes;
}

void push_undo(UndoAction a) {
    if (a.type == UndoAction::NONE) return;
    g_redo.clear(); // new action clears redo stack
    a.before_project_revision = g.project_revision;
    if (g.next_project_revision < g.project_revision) g.next_project_revision = g.project_revision;
    a.after_project_revision = ++g.next_project_revision;
    g.project_revision = a.after_project_revision;
    g.project_dirty = g.project_revision != g.saved_project_revision;
    refresh_active_document_title();
    const std::size_t incoming = history_action_bytes(a);
    // An oversized action is a history boundary: never allow an older snapshot
    // to undo across an operation whose inverse could not be retained.
    if (incoming > kUndoByteLimit) { g_undo.clear(); return; }
    std::size_t bytes = history_stack_bytes(g_undo);
    while (!g_undo.empty() && (g_undo.size() >= kUndoActionLimit || bytes + incoming > kUndoByteLimit)) {
        bytes -= history_action_bytes(g_undo.front());
        g_undo.erase(g_undo.begin());
    }
    g_undo.push_back(std::move(a));
}

void save_active_document_history() {
    if (g_undo.empty() && g_redo.empty() && !g_filter_slider_before) return;
    if (!g.history) g.history = std::make_shared<DocumentHistory>();
    g.history->undo = std::move(g_undo);
    g.history->redo = std::move(g_redo);
    g.history->filter_slider_before = std::move(g_filter_slider_before);
}

void restore_active_document_history() {
    g_undo.clear();
    g_redo.clear();
    g_filter_slider_before.reset();
    if (!g.history) return;
    g_undo = std::move(g.history->undo);
    g_redo = std::move(g.history->redo);
    g_filter_slider_before = std::move(g.history->filter_slider_before);
}

void clear_active_document_history() {
    g_undo.clear();
    g_redo.clear();
    g_filter_slider_before.reset();
    g.history.reset();
}

SettingsSnapshot capture_settings_snapshot() {
    ensure_channel_formula_vectors();
    SettingsSnapshot snapshot;
    snapshot.visible = g.visible;
    snapshot.channel_labels = g.channel_labels;
    snapshot.channel_colors = g.channel_colors;
    snapshot.global_formula = g.global_formula;
    snapshot.channel_formulas = g.channel_formulas;
    snapshot.snap_to_data = g.snap_to_data;
    snapshot.noise_threshold_enabled = g.noise_threshold_enabled;
    snapshot.noise_threshold_min = g.noise_threshold_min;
    snapshot.noise_threshold_max = g.noise_threshold_max;
    snapshot.noise_threshold_mode = g.noise_threshold_mode;
    snapshot.noise_threshold_topology = g.noise_threshold_topology;
    snapshot.marker_color = g.marker_color;
    snapshot.point_groups = g.point_groups;
    snapshot.active_point_group = g.active_point_group;
    snapshot.time_active_point_group = g.time_active_point_group;
    snapshot.freq_active_point_group = g.freq_active_point_group;
    snapshot.frf_active_point_group = g.frf_active_point_group;
    snapshot.guides = g.guides;
    snapshot.markers = g.markers;
    snapshot.active_marker = g.active_marker;
    snapshot.pending_line = g.pending_line;
    snapshot.pending_marker = g.pending_marker;
    snapshot.measure_mode = g.measure_mode;
    snapshot.auto_y = g.auto_y;
    snapshot.y_lock_min = g.y_lock_min;
    snapshot.y_lock_max = g.y_lock_max;
    snapshot.auto_y_amp = g.auto_y_amp;
    snapshot.y_amp_max = g.y_amp_max;
    snapshot.distinguish_curves = g.distinguish_curves;
    return snapshot;
}

bool settings_snapshot_differs(const SettingsSnapshot& a, const SettingsSnapshot& b) {
    auto same_point_groups = [&](const std::vector<PointGroup>& lhs, const std::vector<PointGroup>& rhs) {
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i].name != rhs[i].name ||
                lhs[i].color != rhs[i].color ||
                lhs[i].visible != rhs[i].visible ||
                lhs[i].mode != rhs[i].mode ||
                lhs[i].display.number != rhs[i].display.number ||
                lhs[i].display.x != rhs[i].display.x ||
                lhs[i].display.y != rhs[i].display.y ||
                lhs[i].display.dx != rhs[i].display.dx ||
                lhs[i].display.dy != rhs[i].display.dy ||
                lhs[i].display.inv_dt != rhs[i].display.inv_dt ||
                lhs[i].display.dist != rhs[i].display.dist ||
                lhs[i].points != rhs[i].points) {
                return false;
            }
        }
        return true;
    };
    auto same_guides = [&](const std::vector<GuideLine>& lhs, const std::vector<GuideLine>& rhs) {
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i].vertical != rhs[i].vertical ||
                lhs[i].value != rhs[i].value ||
                lhs[i].mode != rhs[i].mode) {
                return false;
            }
        }
        return true;
    };
    auto same_markers = [&](const std::vector<App::Marker>& lhs, const std::vector<App::Marker>& rhs) {
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i].x != rhs[i].x ||
                lhs[i].y != rhs[i].y ||
                lhs[i].label != rhs[i].label ||
                lhs[i].freq != rhs[i].freq ||
                lhs[i].mode != rhs[i].mode ||
                lhs[i].snapped != rhs[i].snapped ||
                lhs[i].channel != rhs[i].channel) {
                return false;
            }
        }
        return true;
    };
    return a.visible != b.visible ||
           a.channel_labels != b.channel_labels ||
           a.channel_colors != b.channel_colors ||
           a.global_formula != b.global_formula ||
           a.channel_formulas != b.channel_formulas ||
           a.snap_to_data != b.snap_to_data ||
           a.noise_threshold_enabled != b.noise_threshold_enabled ||
           a.noise_threshold_min != b.noise_threshold_min ||
           a.noise_threshold_max != b.noise_threshold_max ||
           a.noise_threshold_mode != b.noise_threshold_mode ||
           a.noise_threshold_topology != b.noise_threshold_topology ||
           a.marker_color != b.marker_color ||
           !same_point_groups(a.point_groups, b.point_groups) ||
           a.active_point_group != b.active_point_group ||
           a.time_active_point_group != b.time_active_point_group ||
           a.freq_active_point_group != b.freq_active_point_group ||
           a.frf_active_point_group != b.frf_active_point_group ||
           !same_guides(a.guides, b.guides) ||
           !same_markers(a.markers, b.markers) ||
           a.active_marker != b.active_marker ||
           a.pending_line != b.pending_line ||
           a.pending_marker != b.pending_marker ||
           a.measure_mode != b.measure_mode ||
           a.auto_y != b.auto_y ||
           a.y_lock_min != b.y_lock_min ||
           a.y_lock_max != b.y_lock_max ||
           a.auto_y_amp != b.auto_y_amp ||
           a.y_amp_max != b.y_amp_max ||
           a.distinguish_curves != b.distinguish_curves;
}

void sync_channel_controls_from_state() {
    for (std::size_t i = 0; i < g.checks.size() && i < g.visible.size(); ++i) {
        if (g.checks[i]) {
            set_toggle_checked(g.checks[i], g.visible[i] != 0);
        }
    }
    for (std::size_t i = 0; i < g.check_labels.size() && i < g.ds.channel_count(); ++i) {
        if (g.check_labels[i]) {
            const std::wstring label = channel_display_label(i);
            SetWindowTextW(g.check_labels[i], label.c_str());
        }
    }
    if (g.measure) {
        SendMessageW(g.measure, BM_SETCHECK, g.measure_mode ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g.autoy) {
        SendMessageW(g.autoy, BM_SETCHECK, g.auto_y ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void rebuild_formula_cache_from_state() {
    g.global_formula_rpn.clear();
    g.channel_formula_rpn.assign(g.channel_formulas.size(), {});
    invalidate_formula_runtime();
    ensure_channel_formula_vectors();
}

void recompute_transforms_from_state() {
    invalidate_plot_analysis_cache();
    invalidate_filtered_channel_cache();
    clear_spectrum_cache_state();
    if ((g.mode == AnalysisMode::FFT) && !g.formula_ini_deferred) compute_spectrum();
    on_frf_processing_changed();
    sync_menu();
}

void apply_settings_snapshot(const SettingsSnapshot& snapshot) {
    g_filter_slider_before.reset();
    if (g.channel_edit) finish_channel_rename(false);
    g.visible = snapshot.visible;
    g.channel_labels = snapshot.channel_labels;
    g.channel_colors = snapshot.channel_colors;
    g.global_formula = snapshot.global_formula;
    g.channel_formulas = snapshot.channel_formulas;
    rebuild_formula_cache_from_state();
    g.snap_to_data = snapshot.snap_to_data;
    g.noise_threshold_enabled = snapshot.noise_threshold_enabled;
    g.noise_threshold_min = snapshot.noise_threshold_min;
    g.noise_threshold_max = snapshot.noise_threshold_max;
    g.noise_threshold_mode = snapshot.noise_threshold_mode;
    g.noise_threshold_topology = snapshot.noise_threshold_topology;
    normalize_filter_bounds();
    g.marker_color = snapshot.marker_color;
    g.point_groups = snapshot.point_groups;
    g.active_point_group = snapshot.active_point_group;
    g.time_active_point_group = snapshot.time_active_point_group;
    g.freq_active_point_group = snapshot.freq_active_point_group;
    g.frf_active_point_group = snapshot.frf_active_point_group;
    normalize_active_point_group();
    sync_point_display_from_active_group();
    g.guides = snapshot.guides;
    g.markers = snapshot.markers;
    g.active_marker = snapshot.active_marker;
    g.pending_line = snapshot.pending_line;
    g.pending_marker = snapshot.pending_marker;
    g.measure_mode = snapshot.measure_mode;
    g.auto_y = snapshot.auto_y;
    g.y_lock_min = snapshot.y_lock_min;
    g.y_lock_max = snapshot.y_lock_max;
    g.auto_y_amp = snapshot.auto_y_amp;
    g.y_amp_max = snapshot.y_amp_max;
    g.distinguish_curves = snapshot.distinguish_curves;
    sync_channel_controls_from_state();
    recompute_transforms_from_state();
    refresh_settings_controls();
    refresh_side_panel_controls();
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
    save_runtime_settings();
}

bool record_settings_change(const SettingsSnapshot& before) {
    SettingsSnapshot after = capture_settings_snapshot();
    if (!settings_snapshot_differs(before, after)) return false;
    UndoAction action;
    action.type = UndoAction::SETTINGS_CHANGE;
    action.before_settings = before;
    action.after_settings = std::move(after);
    push_undo(std::move(action));
    return true;
}

void pop_undo() {
    if (g_undo.empty()) return;
    UndoAction a = std::move(g_undo.back());
    g_undo.pop_back();
    bool changed = false;
    switch (a.type) {
        case UndoAction::ADD_POINT:
            if (a.point_group_index >= 0 &&
                a.point_group_index < static_cast<int>(g.point_groups.size())) {
                auto& pts = g.point_groups[static_cast<std::size_t>(a.point_group_index)].points;
                if (!pts.empty()) {
                    g_redo.push_back(a);
                    pts.pop_back();
                    changed = true;
                    if (a.point_group_created && pts.empty()) {
                        erase_point_group(static_cast<std::size_t>(a.point_group_index));
                    } else {
                        g.active_point_group = a.point_group_index;
                        active_point_group_index_for_mode(g.point_groups[static_cast<std::size_t>(a.point_group_index)].mode) = g.active_point_group;
                    }
                    if (PointGroup* group = active_point_group()) g.marker_color = group->color;
                }
            }
            break;
        case UndoAction::ADD_LINE: {
            auto it = std::find_if(g.guides.begin(), g.guides.end(), [&](const GuideLine& gl) {
                return gl.vertical == a.line.vertical && gl.value == a.line.value && gl.mode == a.line.mode;
            });
            if (it != g.guides.end()) {
                g_redo.push_back(a);
                g.guides.erase(it);
                changed = true;
            }
            break;
        }
        case UndoAction::ADD_MARKER: {
            auto it = std::find_if(g.markers.begin(), g.markers.end(), [&](const App::Marker& m) {
                return m.x == a.marker.x && m.mode == a.marker.mode && m.label == a.marker.label;
            });
            if (it != g.markers.end()) {
                g_redo.push_back(a);
                g.markers.erase(it);
                changed = true;
            }
            break;
        }
        case UndoAction::CLEAR_POINTS:
            g_redo.push_back(a);
            g.point_groups = a.saved_point_groups;
            g.active_point_group = a.saved_active_point_group;
            g.time_active_point_group = a.saved_time_active_point_group;
            g.freq_active_point_group = a.saved_freq_active_point_group;
            g.frf_active_point_group = a.saved_frf_active_point_group;
            normalize_active_point_group();
            if (PointGroup* group = active_point_group()) g.marker_color = group->color;
            changed = true;
            break;
        case UndoAction::CLEAR_LINES:
            g_redo.push_back(a);
            g.guides = a.saved_lines;
            changed = true;
            break;
        case UndoAction::CLEAR_MARKERS:
            g_redo.push_back(a);
            g.markers = a.saved_markers;
            changed = true;
            break;
        case UndoAction::SETTINGS_CHANGE:
            g_redo.push_back(a);
            apply_settings_snapshot(a.before_settings);
            changed = true;
            break;
        default: break;
    }
    if (changed) {
        g.project_revision = a.before_project_revision;
        g.project_dirty = g.project_revision != g.saved_project_revision;
        refresh_active_document_title();
    }
    sync_point_display_from_active_group();
    refresh_side_panel_controls();
}

void pop_redo() {
    if (g_redo.empty()) return;
    UndoAction a = std::move(g_redo.back());
    g_redo.pop_back();
    bool changed = false;
    switch (a.type) {
        case UndoAction::ADD_POINT:
            if (a.point_group_created) {
                const std::size_t insert_at = (a.point_group_index >= 0 &&
                                               a.point_group_index <= static_cast<int>(g.point_groups.size()))
                    ? static_cast<std::size_t>(a.point_group_index)
                    : g.point_groups.size();
                PointGroup group = a.point_group_state;
                group.points.clear();
                insert_point_group(insert_at, group);
            }
            if (a.point_group_index >= 0 &&
                a.point_group_index < static_cast<int>(g.point_groups.size())) {
                g.point_groups[static_cast<std::size_t>(a.point_group_index)].points.push_back(a.point);
                g.active_point_group = a.point_group_index;
                active_point_group_index_for_mode(g.point_groups[static_cast<std::size_t>(a.point_group_index)].mode) = g.active_point_group;
                g.marker_color = g.point_groups[static_cast<std::size_t>(a.point_group_index)].color;
                g_undo.push_back(a);
                changed = true;
            }
            break;
        case UndoAction::ADD_LINE:
            g.guides.push_back(a.line);
            g_undo.push_back(a);
            changed = true;
            break;
        case UndoAction::ADD_MARKER:
            g.markers.push_back(a.marker);
            g_undo.push_back(a);
            changed = true;
            break;
        case UndoAction::CLEAR_POINTS:
            g_undo.push_back(a);
            clear_measure_point_groups_in_mode(a.cleared_mode);
            changed = true;
            break;
        case UndoAction::CLEAR_LINES:
            g_undo.push_back(a);
            g.guides.clear();
            changed = true;
            break;
        case UndoAction::CLEAR_MARKERS:
            g_undo.push_back(a);
            g.markers.clear();
            changed = true;
            break;
        case UndoAction::SETTINGS_CHANGE:
            g_undo.push_back(a);
            apply_settings_snapshot(a.after_settings);
            changed = true;
            break;
        default: break;
    }
    if (changed) {
        g.project_revision = a.after_project_revision;
        g.project_dirty = g.project_revision != g.saved_project_revision;
        refresh_active_document_title();
    }
    sync_point_display_from_active_group();
    refresh_side_panel_controls();
}

} // namespace gui
