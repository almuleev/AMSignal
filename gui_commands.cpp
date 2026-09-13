// Commands: native viewer implementation.
#include "gui_commands.hpp"
#include "gui_frf_render.hpp"
#include "gui_frf.hpp"
#include "gui_spectrum.hpp"
#include "gui_gap_details.hpp"
#include "gui_controls.hpp"
#include "gui_hotkeys.hpp"
#include "gui_welcome.hpp"
#include "gui_menu.hpp"
#include "gui_settings_window.hpp"
#include "gui_export.hpp"
#include "gui_dialogs.hpp"
#include "gui_documents.hpp"
#include "gui_ids.hpp"
#include "gui_input.hpp"
#include "gui_layout.hpp"
#include "gui_loading.hpp"
#include "gui_navigation.hpp"
#include "gui_playback.hpp"
#include "gui_processing.hpp"
#include "gui_render_data.hpp"
#include "gui_settings.hpp"
#include "gui_side_panel.hpp"
#include "gui_state.hpp"
#include "gui_state_history.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"
#include "gui_window.hpp"

namespace gui {

LRESULT handle_commands_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_HSCROLL: {
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (!ctl || g.updating_noise_threshold_edits) return 0;
            const int ctl_id = GetDlgCtrlID(ctl);
            if (ctl_id != IDC_SIDE_FILTER_LOW_TRACK && ctl_id != IDC_SIDE_FILTER_HIGH_TRACK) return 0;
            apply_filter_slider_change(ctl_id == IDC_SIDE_FILTER_LOW_TRACK,
                static_cast<int>(SendMessageW(ctl, TBM_GETPOS, 0, 0)), LOWORD(wp) == TB_THUMBTRACK);
            return 0;
        }
        case WM_COMMAND: {
            g_filter_slider_before.reset();
            const int id = LOWORD(wp);
            if (id == IDC_DOCUMENT_SELECTOR && HIWORD(wp) == CBN_SELCHANGE) {
                const int selected = static_cast<int>(SendMessageW(g.document_selector, CB_GETCURSEL, 0, 0));
                if (selected >= 0) switch_to_document(static_cast<std::size_t>(selected));
                return 0;
            }
            if (id >= IDM_OPEN_DOCUMENT_BASE && id < IDM_CLOSE_DOCUMENT) {
                switch_to_document(static_cast<std::size_t>(id - IDM_OPEN_DOCUMENT_BASE));
                return 0;
            }
            if (id == IDM_CLOSE_DOCUMENT || id == IDC_CLOSE_DOCUMENT) {
                close_active_document();
                return 0;
            }
            if (g.mode == AnalysisMode::FRF && !frf_command_supported(id)) return 0;
            if (id >= IDM_RECENT_FILE_BASE &&
                id < IDM_RECENT_FILE_BASE + static_cast<int>(g.recent_files.size())) {
                const std::wstring path = g.recent_files[static_cast<std::size_t>(id - IDM_RECENT_FILE_BASE)];
                queue_open_paths({path});
                return 0;
            }
            switch (id) {
                case IDC_OPEN: open_file(); return 0;
                case IDC_SAVEPNG: save_png_dialog(); return 0;
                case IDC_SAVECSV: save_as_dialog(); return 0;
                case IDC_SAVE_PROJECT: save_current_project(); return 0;
                case IDM_EXIT: DestroyWindow(hwnd); return 0;
                case IDM_MODE_TIME:
                    set_mode(AnalysisMode::Time);
                    return 0;
                case IDM_MODE_FREQ:
                    set_mode(AnalysisMode::FFT);
                    return 0;
                case IDM_MODE_FRF: set_mode(AnalysisMode::FRF); return 0;
                case IDC_PLAY: toggle_play(); return 0;
                case IDC_SHOW_ALL:
                {
                    const SettingsSnapshot before = capture_settings_snapshot();
                    set_all_channels_visible(true);
                    record_settings_change(before);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                case IDC_HIDE_ALL:
                {
                    const SettingsSnapshot before = capture_settings_snapshot();
                    set_all_channels_visible(false);
                    record_settings_change(before);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                case IDC_MEASURE:
                    g.measure_mode = !g.measure_mode;
                    if (g.measure_mode) g.pending_line = 0;
                    if (g.measure_mode) g.pending_marker = false;
                    SendMessageW(g.measure, BM_SETCHECK,
                                 g.measure_mode ? BST_CHECKED : BST_UNCHECKED, 0);
                    InvalidateRect(g.measure, nullptr, FALSE);
                    sync_menu();
                    set_status();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case IDM_SETTINGS: open_settings(); return 0;
                case IDC_SIDEPANEL:
                    g.side_panel_visible = !g.side_panel_visible;
                    save_runtime_settings();
                    layout();
                    set_status();
                    redraw_window_with_children(hwnd);
                    return 0;
                case IDC_SIDE_TAB_CHANNELS:
                    g.side_scroll_y = 0;
                    if (g.mode == AnalysisMode::FRF) g.frf_point_settings_open = false;
                    set_side_panel_tab(0);
                    save_runtime_settings();
                    layout();
                    redraw_window_with_children(hwnd);
                    return 0;
                case IDC_SIDE_TAB_POINTS:
                    g.side_scroll_y = 0;
                    if (g.mode == AnalysisMode::FRF) g.frf_point_settings_open = true;
                    set_side_panel_tab(1);
                    save_runtime_settings();
                    layout();
                    redraw_window_with_children(hwnd);
                    return 0;
                case IDC_SIDE_TAB_FILTER:
                    g.side_scroll_y = 0;
                    set_side_panel_tab(2);
                    save_runtime_settings();
                    layout();
                    redraw_window_with_children(hwnd);
                    return 0;
                case IDC_SIDE_GLOBAL_FORMULA_EDIT:
                case IDC_SIDE_FORMULA_EDIT:
                    if (HIWORD(wp) == EN_SETFOCUS && g.formula_ini_deferred) {
                        ensure_channel_formulas_loaded();
                        load_side_transform_controls();
                    }
                    return 0;
                case IDC_SIDE_GLOBAL_FORMULA_APPLY: {
                    if (!has_data()) return 0;
                    const SettingsSnapshot before = capture_settings_snapshot();
                    std::wstring formula;
                    std::wstring error;
                    std::vector<FormulaToken> compiled;
                    if (!read_formula_edit(g.side_global_formula_edit, formula, compiled, error)) {
                        std::wstring message = (g_str == &kEn) ? L"Invalid global coefficient:\n" : L"Некорректный общий коэффициент:\n";
                        message += error;
                        MessageBoxW(hwnd, message.c_str(), settings_window_title(), MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    assign_global_formula(formula, compiled);
                    on_signal_transform_changed(true);
                    record_settings_change(before);
                    refresh_settings_controls();
                    load_side_transform_controls();
                    return 0;
                }
                case IDC_SIDE_CHANNEL_COLOR:
                    if (g.side_selected_channel >= 0 &&
                        g.side_selected_channel < static_cast<int>(g.ds.channel_count())) {
                        CHOOSECOLORW cc = {};
                        cc.lStructSize = sizeof(cc);
                        cc.hwndOwner = hwnd;
                        cc.lpCustColors = g_custom_colors;
                        cc.rgbResult = channel_color(static_cast<std::size_t>(g.side_selected_channel));
                        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                        if (ChooseColorW(&cc)) {
                            const SettingsSnapshot before = capture_settings_snapshot();
                            if (static_cast<std::size_t>(g.side_selected_channel) >= g.channel_colors.size()) {
                                g.channel_colors.resize(g.ds.channel_count());
                            }
                            g.channel_colors[static_cast<std::size_t>(g.side_selected_channel)] = cc.rgbResult;
                            record_settings_change(before);
                            refresh_settings_controls();
                            refresh_side_panel_controls();
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                    }
                    return 0;
                case IDC_SIDE_FORMULA_APPLY_SELECTED:
                case IDC_SIDE_FORMULA_APPLY_VISIBLE: {
                    if (!has_data()) return 0;
                    const SettingsSnapshot before = capture_settings_snapshot();
                    std::wstring formula;
                    std::wstring error;
                    std::vector<FormulaToken> compiled;
                    if (!read_formula_edit(g.side_formula_edit, formula, compiled, error)) {
                        std::wstring message = (g_str == &kEn) ? L"Invalid coefficient:\n" : L"Некорректный коэффициент:\n";
                        message += error;
                        MessageBoxW(hwnd, message.c_str(), settings_window_title(), MB_OK | MB_ICONWARNING);
                        return 0;
                    }

                    ensure_channel_formula_vectors();
                    bool changed = false;
                    if (LOWORD(wp) == IDC_SIDE_FORMULA_APPLY_SELECTED) {
                        if (g.side_selected_channel >= 0 &&
                            g.side_selected_channel < static_cast<int>(g.channel_formulas.size())) {
                            assign_formula_to_channel(static_cast<std::size_t>(g.side_selected_channel), formula, compiled);
                            changed = true;
                        }
                    } else {
                        for (std::size_t i = 0; i < g.visible.size() && i < g.channel_formulas.size(); ++i) {
                            if (!g.visible[i]) continue;
                            assign_formula_to_channel(i, formula, compiled);
                            changed = true;
                        }
                    }

                    if (changed) {
                        on_signal_transform_changed(true);
                        record_settings_change(before);
                        refresh_settings_controls();
                        load_side_transform_controls();
                    }
                    return 0;
                }
                case IDC_SIDE_FORMULA_RESET_SELECTED:
                    if (g.side_selected_channel >= 0) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        reset_channel_transform(static_cast<std::size_t>(g.side_selected_channel));
                        on_signal_transform_changed(true);
                        record_settings_change(before);
                        refresh_settings_controls();
                        load_side_transform_controls();
                    }
                    return 0;
                case IDC_SIDE_FORMULA_RESET_ALL:
                    if (has_data()) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        reset_all_channel_transforms();
                        on_signal_transform_changed(true);
                        record_settings_change(before);
                        refresh_settings_controls();
                        load_side_transform_controls();
                    }
                    return 0;
                case IDC_AUTOY:
                    if (g.mode == AnalysisMode::FRF) {
                        frf_y_range(g.frf.y_min, g.frf.y_max);
                        g.frf.auto_y = !g.frf.auto_y;
                        sync_menu(); set_status(); InvalidateRect(hwnd, nullptr, TRUE); return 0;
                    }
                    g.auto_y = !g.auto_y;
                    if (!g.auto_y) current_time_yrange(g.y_lock_min, g.y_lock_max);
                    SendMessageW(g.autoy, BM_SETCHECK,
                                 g.auto_y ? BST_CHECKED : BST_UNCHECKED, 0);
                    InvalidateRect(g.autoy, nullptr, FALSE);
                    sync_menu();
                    set_status();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDM_VISMOOTH:
                    g.visual_smooth = !g.visual_smooth;
                    save_runtime_settings();
                    sync_menu();
                    set_status();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDM_VPAN:
                    g.vertical_pan = !g.vertical_pan;
                    save_runtime_settings();
                    sync_menu();
                    set_status();
                    return 0;
                case IDM_THEME:
                    apply_theme_choice((g_theme == &kLightTheme) ? &kDarkTheme : &kLightTheme);
                    return 0;
                case IDM_ADD_VLINE:
                    if (!has_data()) { show_styled_info_prompt(hwnd, g_str->msg_nodata, g_str->msg_openfirst, false); return 0; }
                    if (g.pending_line == 1) {
                        g.pending_line = 0;
                        set_status();
                        sync_menu();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                    g.pending_line = 1;
                    g.pending_marker = false;
                    g.measure_mode = false;
                    SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
                    status_msg(g_str->status_vline);
                    sync_menu();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case IDM_ADD_VLINE_EXACT: {
                    if (!has_data()) { show_styled_info_prompt(hwnd, g_str->msg_nodata, g_str->msg_openfirst, false); return 0; }
                    double value = 0.0;
                    if (!prompt_exact_guide_value(true, value)) return 0;
                    add_guide_line(true, value);
                    return 0;
                }
                case IDM_ADD_HLINE:
                    if (!has_data()) { show_styled_info_prompt(hwnd, g_str->msg_nodata, g_str->msg_openfirst, false); return 0; }
                    if (g.pending_line == 2) {
                        g.pending_line = 0;
                        set_status();
                        sync_menu();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                    g.pending_line = 2;
                    g.pending_marker = false;
                    g.measure_mode = false;
                    SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
                    status_msg(g_str->status_hline);
                    sync_menu();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case IDM_ADD_HLINE_EXACT: {
                    if (!has_data()) { show_styled_info_prompt(hwnd, g_str->msg_nodata, g_str->msg_openfirst, false); return 0; }
                    double value = 0.0;
                    if (!prompt_exact_guide_value(false, value)) return 0;
                    add_guide_line(false, value);
                    return 0;
                }
                case IDM_CLEAR_LINES:
                    if (!g.guides.empty()) {
                        UndoAction ua; ua.type = UndoAction::CLEAR_LINES; ua.saved_lines = g.guides;
                        push_undo(ua);
                        g.guides.clear(); InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    set_status();
                    return 0;
                case IDM_CLEAR_POINTS:
                    if (has_measure_points()) {
                        UndoAction ua;
                        ua.type = UndoAction::CLEAR_POINTS;
                        ua.saved_point_groups = g.point_groups;
                        ua.saved_active_point_group = g.active_point_group;
                        ua.saved_time_active_point_group = g.time_active_point_group;
                        ua.saved_freq_active_point_group = g.freq_active_point_group;
                        ua.saved_frf_active_point_group = g.frf_active_point_group;
                        ua.cleared_mode = current_point_group_mode();
                        push_undo(ua);
                        clear_measure_point_groups();
                        refresh_side_panel_controls();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    set_status();
                    return 0;
                case IDM_ADD_MARKER:
                    if (!has_data()) { show_styled_info_prompt(hwnd, g_str->msg_nodata, g_str->msg_openfirst, false); return 0; }
                    if (g.pending_marker) {
                        g.pending_marker = false;
                        set_status();
                        sync_menu();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                    g.pending_marker = true;
                    g.pending_line = 0;
                    g.measure_mode = false;
                    SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
                    status_msg(g_str->status_marker);
                    sync_menu();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case IDM_CLEAR_MARKERS:
                    if (!g.markers.empty()) {
                        UndoAction ua; ua.type = UndoAction::CLEAR_MARKERS; ua.saved_markers = g.markers;
                        push_undo(ua);
                        g.markers.clear(); InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    g.active_marker = -1;
                    set_status();
                    return 0;
                case IDM_SPEED_00001: set_play_speed(0.0001); return 0;
                case IDM_SPEED_0001: set_play_speed(0.001); return 0;
                case IDM_SPEED_001: set_play_speed(0.01); return 0;
                case IDM_SPEED_01: set_play_speed(0.1); return 0;
                case IDM_SPEED_05: set_play_speed(0.5); return 0;
                case IDM_SPEED_1: set_play_speed(1.0); return 0;
                case IDM_SPEED_2: set_play_speed(2.0); return 0;
                case IDM_SPEED_5: set_play_speed(5.0); return 0;
                case IDM_SPEED_10: set_play_speed(10.0); return 0;
                case IDM_SPEED_CUSTOM: {
                    double speed = g.play_speed;
                    if (prompt_custom_play_speed(speed)) set_play_speed(speed);
                    return 0;
                }
                case IDM_UNDO:
                    pop_undo();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    set_status();
                    return 0;
                case IDM_REDO:
                    pop_redo();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    set_status();
                    return 0;
                case IDC_ZOOMIN: zoom_at(0.5, 0.7); return 0;
                case IDC_ZOOMOUT: zoom_at(0.5, 1.0 / 0.7); return 0;
                case IDC_RESET: reset_view(); return 0;
                case IDC_GOTO_START: goto_start(); return 0;
                case IDC_GOTO_END: goto_end(); return 0;
                case IDC_PANLEFT: pan_by(-0.2); return 0;
                case IDC_PANRIGHT: pan_by(0.2); return 0;
                case IDM_HOTKEYS: show_hotkeys(); return 0;
                case IDM_ABOUT: show_about(); return 0;
                case IDM_LANG_RU: g_str = &kRu; save_runtime_settings(); rebuild_ui(); return 0;
                case IDM_LANG_EN: g_str = &kEn; save_runtime_settings(); rebuild_ui(); return 0;
                case IDC_SIDE_POINT_GROUP_LIST:
                    if (HIWORD(wp) == LBN_SELCHANGE) {
                        const int index = side_selected_point_group();
                        if (index >= 0 &&
                            index < static_cast<int>(g.point_groups.size()) &&
                            point_group_matches_mode(g.point_groups[static_cast<std::size_t>(index)], current_point_group_mode())) {
                            g.active_point_group = index;
                            g.marker_color = g.point_groups[static_cast<std::size_t>(index)].color;
                            active_point_group_index_for_mode(g.point_groups[static_cast<std::size_t>(index)].mode) = index;
                            sync_point_display_from_active_group();
                            save_runtime_settings();
                            refresh_settings_controls();
                            load_side_point_group_controls();
                            refresh_side_panel_controls();
                            set_status();
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                    }
                    return 0;
                case IDC_SIDE_POINT_GROUP_VISIBLE: {
                    const int index = side_selected_point_group();
                    if (index >= 0 &&
                        index < static_cast<int>(g.point_groups.size()) &&
                        point_group_matches_mode(g.point_groups[static_cast<std::size_t>(index)], current_point_group_mode())) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        toggle_checked_state(g.side_point_group_visible);
                        const bool checked = is_toggle_checked(g.side_point_group_visible);
                        g.point_groups[static_cast<std::size_t>(index)].visible = checked;
                        record_settings_change(before);
                        save_runtime_settings();
                        refresh_settings_controls();
                        load_side_point_group_controls();
                        refresh_side_panel_controls();
                        set_status();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                case IDC_SIDE_POINT_GROUP_NEW: {
                    const SettingsSnapshot before = capture_settings_snapshot();
                    create_point_group(g.marker_color);
                    record_settings_change(before);
                    save_runtime_settings();
                    refresh_settings_controls();
                    refresh_side_panel_controls();
                    set_status();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                case IDC_SIDE_POINT_GROUP_DELETE: {
                    const int index = side_selected_point_group();
                    if (index >= 0 &&
                        index < static_cast<int>(g.point_groups.size()) &&
                        point_group_matches_mode(g.point_groups[static_cast<std::size_t>(index)], current_point_group_mode())) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        erase_point_group(static_cast<std::size_t>(index));
                        if (PointGroup* group = active_point_group()) g.marker_color = group->color;
                        record_settings_change(before);
                        save_runtime_settings();
                        refresh_settings_controls();
                        refresh_side_panel_controls();
                        set_status();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                case IDC_SIDE_POINT_GROUP_RENAME: {
                    const int index = side_selected_point_group();
                    if (index >= 0 &&
                        index < static_cast<int>(g.point_groups.size()) &&
                        point_group_matches_mode(g.point_groups[static_cast<std::size_t>(index)], current_point_group_mode()) &&
                        g.side_point_group_name) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        wchar_t buf[256]{};
                        GetWindowTextW(g.side_point_group_name, buf, 256);
                        std::wstring name = buf;
                        if (name.empty()) {
                            name = (g_str == &kEn) ? (L"Group " + std::to_wstring(index + 1)) : (L"Группа " + std::to_wstring(index + 1));
                        }
                        g.point_groups[static_cast<std::size_t>(index)].name = name;
                        record_settings_change(before);
                        refresh_settings_controls();
                        refresh_side_panel_controls();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                case IDC_SIDE_POINT_COLOR_CURRENT: {
                    CHOOSECOLORW cc = {};
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = hwnd;
                    cc.lpCustColors = g_custom_colors;
                    cc.rgbResult = g.marker_color;
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (ChooseColorW(&cc)) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        g.marker_color = cc.rgbResult;
                        PointGroup* group = active_point_group();
                        if (group && group->points.empty()) group->color = g.marker_color;
                        record_settings_change(before);
                        save_runtime_settings();
                        refresh_settings_controls();
                        refresh_side_panel_controls();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                case IDC_SIDE_POINT_GROUP_COLOR: {
                    const int index = side_selected_point_group();
                    if (index < 0 ||
                        index >= static_cast<int>(g.point_groups.size()) ||
                        !point_group_matches_mode(g.point_groups[static_cast<std::size_t>(index)], current_point_group_mode())) return 0;
                    CHOOSECOLORW cc = {};
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = hwnd;
                    cc.lpCustColors = g_custom_colors;
                    cc.rgbResult = g.point_groups[static_cast<std::size_t>(index)].color;
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (ChooseColorW(&cc)) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        g.point_groups[static_cast<std::size_t>(index)].color = cc.rgbResult;
                        g.active_point_group = index;
                        active_point_group_index_for_mode(g.point_groups[static_cast<std::size_t>(index)].mode) = index;
                        g.marker_color = cc.rgbResult;
                        record_settings_change(before);
                        save_runtime_settings();
                        refresh_settings_controls();
                        refresh_side_panel_controls();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                case IDC_SIDE_PT_NUM:
                case IDC_SIDE_PT_X:
                case IDC_SIDE_PT_Y:
                case IDC_SIDE_PT_DX:
                case IDC_SIDE_PT_DY:
                case IDC_SIDE_PT_INVDT:
                case IDC_SIDE_PT_DIST: {
                    const SettingsSnapshot before = capture_settings_snapshot();
                    PointDisplay* display = active_point_display();
                    toggle_checked_state(GetDlgItem(hwnd, id));
                    auto checked = [&](int ctl_id) {
                        return is_toggle_checked(GetDlgItem(hwnd, ctl_id));
                    };
                    PointDisplay next = display ? *display : g.pdisp;
                    next.number = checked(IDC_SIDE_PT_NUM);
                    next.x = checked(IDC_SIDE_PT_X);
                    next.y = checked(IDC_SIDE_PT_Y);
                    next.dx = checked(IDC_SIDE_PT_DX);
                    next.dy = checked(IDC_SIDE_PT_DY);
                    next.inv_dt = checked(IDC_SIDE_PT_INVDT);
                    next.dist = checked(IDC_SIDE_PT_DIST);
                    if (display) *display = next;
                    g.pdisp = next;
                    record_settings_change(before);
                    save_runtime_settings();
                    refresh_settings_controls();
                    refresh_side_panel_controls();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                case IDC_SIDE_PT_SNAP: {
                    const SettingsSnapshot before = capture_settings_snapshot();
                    toggle_checked_state(GetDlgItem(hwnd, id));
                    g.snap_to_data = is_toggle_checked(GetDlgItem(hwnd, id));
                    record_settings_change(before);
                    save_runtime_settings();
                    refresh_settings_controls();
                    refresh_side_panel_controls();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                case IDC_SIDE_FILTER_ENABLE: {
                    if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        toggle_checked_state(GetDlgItem(hwnd, id));
                        g.noise_threshold_enabled = is_toggle_checked(GetDlgItem(hwnd, id));
                        normalize_filter_bounds();
                        commit_filter_settings_change(before);
                    }
                    return 0;
                }
                case IDC_SIDE_FILTER_MODE: {
                    if (HIWORD(wp) == CBN_DROPDOWN) {
                        expand_combo_dropdown(GetDlgItem(hwnd, id));
                        return 0;
                    }
                    if (HIWORD(wp) == CBN_SELCHANGE && !g.updating_noise_threshold_edits) {
                        HWND combo = GetDlgItem(hwnd, id);
                        const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
                        if (sel != CB_ERR) {
                            const SettingsSnapshot before = capture_settings_snapshot();
                            const int value = static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, sel, 0));
                            g.noise_threshold_mode = std::clamp(value,
                                                                static_cast<int>(FilterModeLowPass),
                                                                static_cast<int>(FilterModeBandStop));
                            normalize_filter_bounds();
                            commit_filter_settings_change(before);
                        }
                    }
                    return 0;
                }
                case IDC_SIDE_FILTER_TOPOLOGY: {
                    if (HIWORD(wp) == CBN_DROPDOWN) {
                        expand_combo_dropdown(GetDlgItem(hwnd, id));
                        return 0;
                    }
                    if (HIWORD(wp) == CBN_SELCHANGE && !g.updating_noise_threshold_edits) {
                        HWND combo = GetDlgItem(hwnd, id);
                        const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
                        if (sel != CB_ERR) {
                            const SettingsSnapshot before = capture_settings_snapshot();
                            const int value = static_cast<int>(SendMessageW(combo, CB_GETITEMDATA, sel, 0));
                            g.noise_threshold_topology = std::clamp(value,
                                                                    static_cast<int>(FilterTopologyButterworth),
                                                                    static_cast<int>(FilterTopologyLinkwitzRiley));
                            commit_filter_settings_change(before);
                        }
                    }
                    return 0;
                }
                default: break;
            }
            if (id >= IDC_CHAN_COEFFICIENT_BASE &&
                id < IDC_CHAN_COEFFICIENT_BASE + static_cast<int>(g.channel_coefficient_edits.size())) {
                const int ci = id - IDC_CHAN_COEFFICIENT_BASE;
                if (HIWORD(wp) == EN_SETFOCUS) {
                    g.side_selected_channel = ci;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (HIWORD(wp) == EN_KILLFOCUS && GetWindowTextLengthW(g.channel_coefficient_edits[ci]) > 0) {
                    commit_channel_coefficient(ci);
                }
                return 0;
            }
            if (id >= IDC_CHAN_BASE && id < IDC_CHAN_BASE + static_cast<int>(g.visible.size())) {
                const int ci = id - IDC_CHAN_BASE;
                const SettingsSnapshot before = capture_settings_snapshot();
                g.side_selected_channel = ci;
                toggle_checked_state(g.checks[ci]);
                g.visible[ci] = is_toggle_checked(g.checks[ci]);
                invalidate_plot_analysis_cache();
                record_settings_change(before);
                load_side_transform_controls();
                InvalidateRect(hwnd, nullptr, TRUE);
            } else if (id >= IDC_CHAN_LABEL_BASE &&
                       id < IDC_CHAN_LABEL_BASE + static_cast<int>(g.channel_labels.size())) {
                const int ci = id - IDC_CHAN_LABEL_BASE;
                g.side_selected_channel = ci;
                load_side_transform_controls();
                if (HIWORD(wp) == STN_DBLCLK) {
                    start_channel_rename(ci);
                } else {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void set_mode(AnalysisMode mode) {
    if (g.ds.frequency_axis && mode != AnalysisMode::FFT) return;
    if (g.mode == mode) return;
    normalize_active_point_group();
    active_point_group_index_for_mode(current_point_group_mode()) = g.active_point_group;
    finish_channel_rename(true);
    g.mode = mode;
    g.dragging = false; g.fft_selecting = false;
    g.gap_click_pending = false;
    if (g.main && GetCapture() == g.main) ReleaseCapture();
    g.vvalid = false;
    if (mode != AnalysisMode::Time) {
        stop_play();
        hide_gap_details_card();
    }
    if (mode == AnalysisMode::FRF) {
        g.frf_point_settings_open = g.side_panel_tab == 1;
        if (!g.frf_point_settings_open) g.side_panel_tab = 0;
        normalize_active_point_group();
        if (PointGroup* group = active_point_group()) {
            g.marker_color = group->color;
            sync_point_display_from_active_group();
        }
        refresh_frf_controls(true);
        ensure_current_frf();
    } else {
        normalize_active_point_group();
        if (PointGroup* group = active_point_group()) {
            g.marker_color = group->color;
            sync_point_display_from_active_group();
        }
        if (mode == AnalysisMode::FFT) {
            g.spec_fit_pending = true;
            compute_spectrum_from_current_source();
            g.freq_start = 0.0;
            g.freq_end = g.spec_valid ? g.spec.nyquist : 1.0;
        }
    }
    sync_menu();
    refresh_side_panel_controls();
    if (g.main) layout();
    set_status();
    if (g.main) InvalidateRect(g.main, nullptr, TRUE);
}

} // namespace gui
