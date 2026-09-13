// Side panel: native viewer implementation.
#include "gui_side_panel.hpp"
#include "gui_frf.hpp"
#include "gui_render_data.hpp"
#include "gui_controls.hpp"
#include "gui_settings_window.hpp"
#include "gui_ids.hpp"
#include "gui_layout.hpp"
#include "gui_processing.hpp"
#include "gui_state.hpp"
#include "gui_state_history.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"

namespace gui {

WNDPROC g_channel_edit_proc = nullptr;
WNDPROC g_channel_coefficient_edit_proc = nullptr;
WNDPROC g_side_panel_apply_edit_proc = nullptr;

LRESULT CALLBACK SidePanelApplyEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETDLGCODE) {
        return CallWindowProcW(g_side_panel_apply_edit_proc, hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        const int id = GetDlgCtrlID(hwnd);
        const int action = id == IDC_SIDE_GLOBAL_FORMULA_EDIT ? IDC_SIDE_GLOBAL_FORMULA_APPLY :
                           id == IDC_SIDE_POINT_GROUP_NAME ? IDC_SIDE_POINT_GROUP_RENAME : 0;
        if (action) {
            HWND parent = GetParent(hwnd);
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(action, BN_CLICKED),
                         reinterpret_cast<LPARAM>(GetDlgItem(parent, action)));
            return 0;
        }
    }
    return CallWindowProcW(g_side_panel_apply_edit_proc, hwnd, msg, wp, lp);
}

void install_side_panel_apply_edit(HWND edit) {
    if (!edit) return;
    WNDPROC previous = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SidePanelApplyEditProc)));
    if (!g_side_panel_apply_edit_proc) g_side_panel_apply_edit_proc = previous;
}

void finish_channel_rename(bool apply) {
    if (g.editing_channel < 0 || g.editing_channel >= static_cast<int>(g.channel_labels.size())) return;
    const int ci = g.editing_channel;
    SettingsSnapshot before;
    bool track_change = false;
    if (apply && g.channel_edit) {
        before = capture_settings_snapshot();
        track_change = true;
    }
    if (apply && g.channel_edit) {
        wchar_t buf[256];
        GetWindowTextW(g.channel_edit, buf, 256);
        g.channel_labels[ci] = buf;
        if (ci < static_cast<int>(g.check_labels.size()) && g.check_labels[ci]) {
            const std::wstring label = channel_display_label(static_cast<std::size_t>(ci));
            SetWindowTextW(g.check_labels[ci], label.c_str());
        }
        InvalidateRect(g.main, nullptr, TRUE);
    }
    if (g.channel_edit) {
        DestroyWindow(g.channel_edit);
        g.channel_edit = nullptr;
    }
    g_channel_edit_proc = nullptr;
    g.editing_channel = -1;
    if (track_change) record_settings_change(before);
    refresh_settings_controls();
    set_status();
    InvalidateRect(g.main, nullptr, FALSE);
}

LRESULT CALLBACK ChannelEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_GETDLGCODE:
            return CallWindowProcW(g_channel_edit_proc, hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) {
                finish_channel_rename(true);
                SetFocus(g.main);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                finish_channel_rename(false);
                SetFocus(g.main);
                return 0;
            }
            break;
        case WM_KILLFOCUS:
            finish_channel_rename(true);
            return 0;
    }
    return CallWindowProcW(g_channel_edit_proc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK ChannelCoefficientEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETDLGCODE) {
        return CallWindowProcW(g_channel_coefficient_edit_proc, hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        SetFocus(g.main);
        return 0;
    }
    return CallWindowProcW(g_channel_coefficient_edit_proc, hwnd, msg, wp, lp);
}

bool commit_channel_coefficient(int ci, bool show_error) {
    if (ci < 0 || ci >= static_cast<int>(g.channel_coefficient_edits.size()) ||
        ci >= static_cast<int>(g.ds.channel_count())) return false;
    HWND edit = g.channel_coefficient_edits[static_cast<std::size_t>(ci)];
    wchar_t buffer[128]{};
    GetWindowTextW(edit, buffer, 128);
    double coefficient = 0.0;
    if (!parse_wide_double_text(buffer, coefficient)) {
        if (show_error) {
            MessageBoxW(g.main,
                g_str == &kEn ? L"Enter a finite numeric multiplier." : L"Введите конечный числовой коэффициент.",
                settings_window_title(), MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    std::wstring formula = format_edit_number(coefficient) + L"*x";
    std::wstring error;
    std::vector<FormulaToken> compiled;
    if (!compile_formula_rpn(formula, compiled, error, g_str == &kEn)) return false;
    ensure_channel_formulas_loaded();
    const SettingsSnapshot before = capture_settings_snapshot();
    assign_formula_to_channel(static_cast<std::size_t>(ci), formula, compiled);
    on_signal_transform_changed(true);
    record_settings_change(before);
    SetWindowTextW(edit, format_edit_number(coefficient).c_str());
    return true;
}

void finish_channel_rename_if_click_outside(HWND hwnd) {
    if (!g.channel_edit) return;
    RECT edit_rect;
    GetWindowRect(g.channel_edit, &edit_rect);
    MapWindowPoints(nullptr, hwnd, reinterpret_cast<LPPOINT>(&edit_rect), 2);
    POINT click_point;
    GetCursorPos(&click_point);
    ScreenToClient(hwnd, &click_point);
    if (!PtInRect(&edit_rect, click_point)) {
        finish_channel_rename(true);
        SetFocus(hwnd);
    }
}

void start_channel_rename(int ci) {
    if (ci < 0 || ci >= static_cast<int>(g.channel_labels.size())) return;
    finish_channel_rename(true);
    if (ci >= static_cast<int>(g.check_labels.size()) || !g.check_labels[ci]) return;

    RECT r;
    GetWindowRect(g.check_labels[ci], &r);
    MapWindowPoints(nullptr, g.main, reinterpret_cast<LPPOINT>(&r), 2);
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    g.channel_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", channel_display_label(static_cast<std::size_t>(ci)).c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        r.left - 2, r.top - 1, (r.right - r.left) + 4, (r.bottom - r.top) + 2,
        g.main, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_EDIT)), inst, nullptr);
    if (!g.channel_edit) return;
    SendMessageW(g.channel_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(g.channel_edit, EM_SETSEL, 0, -1);
    g_channel_edit_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g.channel_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ChannelEditProc)));
    g.editing_channel = ci;
    SetFocus(g.channel_edit);
}

void destroy_checks() {
    finish_channel_rename(true);
    for (HWND h : g.checks) DestroyWindow(h);
    g.checks.clear();
    for (HWND h : g.check_labels) DestroyWindow(h);
    g.check_labels.clear();
    for (HWND h : g.channel_coefficient_edits) DestroyWindow(h);
    g.channel_coefficient_edits.clear();
}

void rebuild_checks() {
    destroy_checks();
    if (!has_data()) return;
    if (g.side_selected_channel < 0 || g.side_selected_channel >= static_cast<int>(g.ds.channel_count())) g.side_selected_channel = 0;
    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
    for (std::size_t i = 0; i < g.ds.channel_count(); ++i) {
        HWND c = CreateWindowExW(
            0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW, 0, 0, 10, 10, g.main,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_BASE + i)), inst, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        set_toggle_checked(c, g.visible[i] != 0);
        g.checks.push_back(c);

        HWND lbl = CreateWindowExW(
            0, L"STATIC", channel_display_label(i).c_str(),
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_NOTIFY | SS_CENTERIMAGE,
            0, 0, 10, 10, g.main,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_LABEL_BASE + i)), inst, nullptr);
        SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g.check_labels.push_back(lbl);

        HWND coefficient = CreateWindowExW(
            0, L"EDIT", channel_coefficient_text(i).c_str(),
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT,
            0, 0, 10, 10, g.main,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_COEFFICIENT_BASE + i)), inst, nullptr);
        SendMessageW(coefficient, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        install_compact_themed_edit(coefficient);
        if (!g_channel_coefficient_edit_proc) {
            g_channel_coefficient_edit_proc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(coefficient, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ChannelCoefficientEditProc)));
        } else {
            SetWindowLongPtrW(coefficient, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ChannelCoefficientEditProc));
        }
        g.channel_coefficient_edits.push_back(coefficient);
    }
}

void hide_ui_controls() {
    if (g.main) {
        SetMenu(g.main, nullptr);
        DrawMenuBar(g.main);
    }
    if (g.frf_panel) ShowWindow(g.frf_panel, SW_HIDE);
    if (g.document_selector) ShowWindow(g.document_selector, SW_HIDE);
    for (HWND b : g.buttons) ShowWindow(b, SW_HIDE);
    for (HWND c : g.checks) ShowWindow(c, SW_HIDE);
    for (HWND c : g.check_labels) ShowWindow(c, SW_HIDE);
    for (HWND c : g.channel_coefficient_edits) ShowWindow(c, SW_HIDE);
    for (HWND c : g.side_channel_controls) ShowWindow(c, SW_HIDE);
    for (HWND c : g.side_filter_controls) ShowWindow(c, SW_HIDE);
    for (HWND c : g.side_point_controls) ShowWindow(c, SW_HIDE);
    if (g.channel_edit) ShowWindow(g.channel_edit, SW_HIDE);
    if (g.status) ShowWindow(g.status, SW_HIDE);
}

void show_ui_controls() {
    if (g.main) {
        SetMenu(g.main, g.menu);
        DrawMenuBar(g.main);
    }
    for (HWND b : g.buttons) ShowWindow(b, SW_SHOW);
    if (g.document_selector) ShowWindow(g.document_selector, SW_SHOW);
    apply_side_panel_visibility();
    if (g.channel_edit && g.side_panel_visible && g.side_panel_tab == 0) ShowWindow(g.channel_edit, SW_SHOW);
    // The native STATIC control only retains text for accessibility; the
    // owner-drawn status bar supplies the visible status and author credit.
    if (g.status) ShowWindow(g.status, SW_HIDE);
}

bool welcome_visible() {
    return g.welcome_wnd && IsWindow(g.welcome_wnd) && IsWindowVisible(g.welcome_wnd);
}

int side_panel_width() {
    return (!welcome_visible() && g.side_panel_visible) ? kRightPanel : 0;
}

int side_selected_point_group() {
    if (!g.side_point_group_list) return -1;
    int sel = static_cast<int>(SendMessageW(g.side_point_group_list, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return -1;
    return static_cast<int>(SendMessageW(g.side_point_group_list, LB_GETITEMDATA, sel, 0));
}

void populate_filter_mode_combo(HWND combo) {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    auto add = [&](const wchar_t* text, int value) {
        int idx = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text)));
        SendMessageW(combo, CB_SETITEMDATA, idx, static_cast<LPARAM>(value));
    };
    add(filter_mode_lowpass_text(), FilterModeLowPass);
    add(filter_mode_highpass_text(), FilterModeHighPass);
    add(filter_mode_bandpass_text(), FilterModeBandPass);
    add(filter_mode_bandstop_text(), FilterModeBandStop);
    SendMessageW(combo, CB_SETMINVISIBLE, 4, 0);
    SendMessageW(combo, CB_SETDROPPEDWIDTH, 220, 0);
}

void populate_filter_topology_combo(HWND combo) {
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    auto add = [&](const wchar_t* text, int value) {
        int idx = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text)));
        SendMessageW(combo, CB_SETITEMDATA, idx, static_cast<LPARAM>(value));
    };
    add(filter_topology_butterworth_text(), FilterTopologyButterworth);
    add(filter_topology_bessel_text(), FilterTopologyBessel);
    add(filter_topology_chebyshev_text(), FilterTopologyChebyshev);
    add(filter_topology_linkwitz_text(), FilterTopologyLinkwitzRiley);
    SendMessageW(combo, CB_SETMINVISIBLE, 4, 0);
    SendMessageW(combo, CB_SETDROPPEDWIDTH, 220, 0);
}

void expand_combo_dropdown(HWND combo) {
    if (!combo) return;
    COMBOBOXINFO cbi{};
    cbi.cbSize = sizeof(cbi);
    if (!GetComboBoxInfo(combo, &cbi) || !cbi.hwndList) return;

    RECT list_rc{};
    if (!GetWindowRect(cbi.hwndList, &list_rc)) return;

    int item_h = static_cast<int>(SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0));
    if (item_h <= 0) item_h = 20;
    const int item_count = max(1, static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0)));
    const int visible_items = min(item_count, 4);
    const int desired_h = max(item_h * visible_items + 4, item_h + 4);
    const int desired_w = max(static_cast<int>(list_rc.right - list_rc.left), 220);

    SetWindowPos(cbi.hwndList, HWND_TOP,
                 list_rc.left, list_rc.top,
                 desired_w, desired_h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void sync_filter_controls_from_state() {
    const double nyquist = current_filter_nyquist();
    const bool has_signal = has_data() && nyquist > 0.0;
    const bool filter_configurable = has_signal;

    if (g.side_filter_enable) {
        SetWindowTextW(g.side_filter_enable, filter_toggle_text());
        set_toggle_checked(g.side_filter_enable, g.noise_threshold_enabled);
        EnableWindow(g.side_filter_enable, has_signal);
    }
    if (g.side_filter_mode_label) SetWindowTextW(g.side_filter_mode_label, filter_mode_label_text());
    if (g.side_filter_topology_label) SetWindowTextW(g.side_filter_topology_label, filter_topology_label_text());
    if (g.side_filter_low_label) SetWindowTextW(g.side_filter_low_label, filter_low_cutoff_text());
    if (g.side_filter_high_label) SetWindowTextW(g.side_filter_high_label, filter_high_cutoff_text());

    if (g.side_filter_mode) {
        populate_filter_mode_combo(g.side_filter_mode);
        SendMessageW(g.side_filter_mode, CB_SETCURSEL,
            std::clamp(g.noise_threshold_mode,
                       static_cast<int>(FilterModeLowPass),
                       static_cast<int>(FilterModeBandStop)), 0);
        EnableWindow(g.side_filter_mode, filter_configurable);
    }
    if (g.side_filter_topology) {
        populate_filter_topology_combo(g.side_filter_topology);
        SendMessageW(g.side_filter_topology, CB_SETCURSEL,
            std::clamp(g.noise_threshold_topology,
                       static_cast<int>(FilterTopologyButterworth),
                       static_cast<int>(FilterTopologyLinkwitzRiley)), 0);
        EnableWindow(g.side_filter_topology, filter_configurable);
    }

    g.updating_noise_threshold_edits = true;
    if (g.side_filter_low_value) SetWindowTextW(g.side_filter_low_value, filter_frequency_text(g.noise_threshold_min).c_str());
    if (g.side_filter_high_value) SetWindowTextW(g.side_filter_high_value, filter_frequency_text(g.noise_threshold_max).c_str());
    if (g.side_filter_low_track) {
        SendMessageW(g.side_filter_low_track, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
        SendMessageW(g.side_filter_low_track, TBM_SETPOS, TRUE, frequency_to_filter_slider(g.noise_threshold_min, nyquist));
        EnableWindow(g.side_filter_low_track, filter_configurable);
    }
    if (g.side_filter_high_track) {
        SendMessageW(g.side_filter_high_track, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
        SendMessageW(g.side_filter_high_track, TBM_SETPOS, TRUE, frequency_to_filter_slider(g.noise_threshold_max, nyquist));
        EnableWindow(g.side_filter_high_track, filter_configurable);
    }
    g.updating_noise_threshold_edits = false;
}

void load_side_transform_controls() {
    const bool formulas_ready = !g.formula_ini_deferred;
    const std::wstring default_formula = default_channel_formula_text();
    if (g.side_global_formula_edit) {
        SetWindowTextW(g.side_global_formula_edit,
            formulas_ready ? g.global_formula.c_str() : default_formula.c_str());
        EnableWindow(g.side_global_formula_edit, has_data());
    }
    if (g.side_global_formula_apply) EnableWindow(g.side_global_formula_apply, has_data());
    const int ci = g.side_selected_channel;
    const bool valid = ci >= 0 && ci < static_cast<int>(g.ds.channel_count());
    if (g.side_formula_edit) {
        const wchar_t* text = default_formula.c_str();
        if (valid && static_cast<std::size_t>(ci) < g.channel_formulas.size()) {
            text = formulas_ready
                ? g.channel_formulas[static_cast<std::size_t>(ci)].c_str()
                : default_formula.c_str();
        }
        SetWindowTextW(g.side_formula_edit, text);
        EnableWindow(g.side_formula_edit, has_data());
    }
    for (std::size_t i = 0; i < g.channel_coefficient_edits.size(); ++i) {
        HWND edit = g.channel_coefficient_edits[i];
        if (edit && GetFocus() != edit) SetWindowTextW(edit, channel_coefficient_text(i).c_str());
        if (edit) EnableWindow(edit, has_data());
    }
    if (g.side_channel_color) EnableWindow(g.side_channel_color, valid);
    if (g.side_formula_apply_selected) EnableWindow(g.side_formula_apply_selected, valid);
    if (g.side_formula_apply_visible) EnableWindow(g.side_formula_apply_visible, has_data());
    if (g.side_formula_reset_selected) EnableWindow(g.side_formula_reset_selected, valid);
    if (g.side_formula_reset_all) EnableWindow(g.side_formula_reset_all, has_data());
    sync_filter_controls_from_state();
}

void load_side_point_group_controls() {
    const int index = side_selected_point_group();
    const bool valid = index >= 0 &&
        index < static_cast<int>(g.point_groups.size()) &&
        point_group_matches_mode(g.point_groups[static_cast<std::size_t>(index)], current_point_group_mode());
    if (valid) {
        g.active_point_group = index;
        active_point_group_index_for_mode(g.point_groups[static_cast<std::size_t>(index)].mode) = index;
        sync_point_display_from_active_group();
    }
    if (g.side_point_group_visible) {
        set_toggle_checked(
            g.side_point_group_visible,
            valid && g.point_groups[static_cast<std::size_t>(index)].visible);
        EnableWindow(g.side_point_group_visible, valid);
    }
    if (g.side_point_group_color) EnableWindow(g.side_point_group_color, valid);
    if (g.side_point_group_delete) EnableWindow(g.side_point_group_delete, valid);
    if (g.side_point_group_name) {
        SetWindowTextW(g.side_point_group_name,
            valid ? g.point_groups[static_cast<std::size_t>(index)].name.c_str() : L"");
        EnableWindow(g.side_point_group_name, valid);
    }
    if (g.side_point_group_rename) EnableWindow(g.side_point_group_rename, valid);
    // These are defaults for the first point group when none exists yet.
    if (HWND num = GetDlgItem(g.main, IDC_SIDE_PT_NUM)) EnableWindow(num, TRUE);
    if (HWND x = GetDlgItem(g.main, IDC_SIDE_PT_X)) EnableWindow(x, TRUE);
    if (HWND y = GetDlgItem(g.main, IDC_SIDE_PT_Y)) EnableWindow(y, TRUE);
    if (HWND dx = GetDlgItem(g.main, IDC_SIDE_PT_DX)) EnableWindow(dx, TRUE);
    if (HWND dy = GetDlgItem(g.main, IDC_SIDE_PT_DY)) EnableWindow(dy, TRUE);
    if (HWND invdt = GetDlgItem(g.main, IDC_SIDE_PT_INVDT)) EnableWindow(invdt, TRUE);
    if (HWND dist = GetDlgItem(g.main, IDC_SIDE_PT_DIST)) EnableWindow(dist, TRUE);
    if (HWND snap = GetDlgItem(g.main, IDC_SIDE_PT_SNAP)) EnableWindow(snap, TRUE);
}

void populate_side_point_group_list() {
    if (!g.side_point_group_list) return;
    const int previous = side_selected_point_group();
    SendMessageW(g.side_point_group_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.side_point_group_list, LB_RESETCONTENT, 0, 0);
    normalize_active_point_group();
    int selected_index = LB_ERR;
    bool have_mode_groups = false;
    for (const auto& group : g.point_groups) {
        if (point_group_matches_mode(group, current_point_group_mode())) {
            have_mode_groups = true;
            break;
        }
    }
    if (!have_mode_groups) {
        const int idx = static_cast<int>(SendMessageW(
            g.side_point_group_list, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(point_group_empty_text())));
        if (idx != LB_ERR) {
            SendMessageW(g.side_point_group_list, LB_SETITEMDATA, idx, static_cast<LPARAM>(-1));
            selected_index = idx;
            g.side_scroll_y = 0;
        }
    } else {
        for (std::size_t i = 0; i < g.point_groups.size(); ++i) {
            if (!point_group_matches_mode(g.point_groups[i], current_point_group_mode())) continue;
            std::wstring label = point_group_list_label(i, g.point_groups[i]);
            int idx = static_cast<int>(SendMessageW(g.side_point_group_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
            SendMessageW(g.side_point_group_list, LB_SETITEMDATA, idx, static_cast<LPARAM>(i));
            if (static_cast<int>(i) == previous || static_cast<int>(i) == g.active_point_group) selected_index = idx;
        }
    }
    if (selected_index != LB_ERR) {
        SendMessageW(g.side_point_group_list, LB_SETCURSEL, selected_index, 0);
        SendMessageW(g.side_point_group_list, LB_SETTOPINDEX, selected_index, 0);
    }
    SendMessageW(g.side_point_group_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.side_point_group_list, nullptr, TRUE);
    load_side_point_group_controls();
}

bool side_panel_hit_test(const POINT& pt) {
    if (!g.side_panel_visible || welcome_visible()) return false;
    RECT rc{};
    GetClientRect(g.main, &rc);
    const int panel_left = rc.right - side_panel_width();
    return pt.x >= panel_left && pt.x <= rc.right && pt.y >= kTopBar && pt.y <= rc.bottom - kBottomBar;
}

void update_side_panel_scrollbar(int viewport_top, int content_height) {
    if (g.mode == AnalysisMode::FRF && !g.frf_point_settings_open) { g.side_scroll_max = 0; return; }
    g.side_content_height_channels = max(g.side_content_height_channels, 0);
    g.side_content_height_points = max(g.side_content_height_points, 0);
    g.side_content_height_filter = max(g.side_content_height_filter, 0);
    RECT rc{};
    GetClientRect(g.main, &rc);
    const int viewport_bottom = rc.bottom - kBottomBar - 6;
    const int viewport_height = max(0, viewport_bottom - viewport_top);
    g.side_scroll_max = max(0, content_height - viewport_height);
    if (g.side_scroll_y > g.side_scroll_max) g.side_scroll_y = g.side_scroll_max;
    if (g.side_scroll_y < 0) g.side_scroll_y = 0;
}

void scroll_side_panel(int delta) {
    if (delta == 0 || g.side_scroll_max <= 0) return;
    const int next = std::clamp(g.side_scroll_y + delta, 0, g.side_scroll_max);
    if (next == g.side_scroll_y) return;
    g.side_scroll_y = next;
    layout();
    RedrawWindow(g.main, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void set_side_panel_tab(int tab) {
    g.side_panel_tab = (tab >= 0 && tab <= 2) ? tab : 0;
    const bool show_channels = g.side_panel_visible && g.side_panel_tab == 0 && !welcome_visible() && g.mode != AnalysisMode::FRF;
    const bool show_points = g.side_panel_visible && g.side_panel_tab == 1 && !welcome_visible() &&
        (g.mode != AnalysisMode::FRF || g.frf_point_settings_open);
    const bool show_filter = g.side_panel_visible && g.side_panel_tab == 2 && !welcome_visible() && g.mode != AnalysisMode::FRF;
    if (g.show_all_btn) ShowWindow(g.show_all_btn, show_channels ? SW_SHOW : SW_HIDE);
    if (g.hide_all_btn) ShowWindow(g.hide_all_btn, show_channels ? SW_SHOW : SW_HIDE);
    for (HWND h : g.side_channel_controls) if (h) ShowWindow(h, show_channels ? SW_SHOW : SW_HIDE);
    for (HWND h : g.side_filter_controls) if (h) ShowWindow(h, show_filter ? SW_SHOW : SW_HIDE);
    for (HWND h : g.side_point_controls) if (h) ShowWindow(h, show_points ? SW_SHOW : SW_HIDE);
    for (HWND h : g.checks) if (h) ShowWindow(h, show_channels ? SW_SHOW : SW_HIDE);
    for (HWND h : g.check_labels) if (h) ShowWindow(h, show_channels ? SW_SHOW : SW_HIDE);
    for (HWND h : g.channel_coefficient_edits) if (h) ShowWindow(h, show_channels ? SW_SHOW : SW_HIDE);
    if (!show_channels && g.channel_edit) ShowWindow(g.channel_edit, SW_HIDE);
    if (g.side_tab_channels) InvalidateRect(g.side_tab_channels, nullptr, FALSE);
    if (g.side_tab_points) InvalidateRect(g.side_tab_points, nullptr, FALSE);
    if (g.side_tab_filter) InvalidateRect(g.side_tab_filter, nullptr, FALSE);
    if (show_channels) {
        load_side_transform_controls();
    } else if (show_points) {
        populate_side_point_group_list();
    } else if (show_filter) {
        sync_filter_controls_from_state();
    }
}

void apply_side_panel_visibility() {
    const bool show = g.side_panel_visible && !welcome_visible();
    const bool frf = g.mode == AnalysisMode::FRF;
    if (g.side_tab_channels) ShowWindow(g.side_tab_channels, show ? SW_SHOW : SW_HIDE);
    if (g.side_tab_points) ShowWindow(g.side_tab_points, show ? SW_SHOW : SW_HIDE);
    if (g.side_tab_filter) ShowWindow(g.side_tab_filter, show && !frf ? SW_SHOW : SW_HIDE);
    set_side_panel_tab(g.side_panel_tab);
}

void refresh_side_panel_controls() {
    sync_point_display_from_active_group();
    if (g.sidepanel_btn) SetWindowTextW(g.sidepanel_btn, side_panel_button_text());
    if (g.side_tab_channels) SetWindowTextW(g.side_tab_channels, side_tab_channels_text());
    if (g.side_tab_points) SetWindowTextW(g.side_tab_points, side_tab_points_text());
    if (g.side_tab_filter) SetWindowTextW(g.side_tab_filter, side_tab_filter_text());
    if (g.side_channel_hint) SetWindowTextW(g.side_channel_hint, side_channel_hint_text());
    if (g.side_filter_enable) SetWindowTextW(g.side_filter_enable, filter_toggle_text());
    if (g.side_filter_mode_label) SetWindowTextW(g.side_filter_mode_label, filter_mode_label_text());
    if (g.side_filter_topology_label) SetWindowTextW(g.side_filter_topology_label, filter_topology_label_text());
    if (g.side_filter_low_label) SetWindowTextW(g.side_filter_low_label, filter_low_cutoff_text());
    if (g.side_filter_high_label) SetWindowTextW(g.side_filter_high_label, filter_high_cutoff_text());
    if (g.side_global_formula_label) SetWindowTextW(g.side_global_formula_label, side_global_formula_label_text());
    if (g.side_global_formula_apply) SetWindowTextW(g.side_global_formula_apply, side_global_formula_apply_text());
    if (g.side_channel_separator) SetWindowTextW(g.side_channel_separator, L"");
    if (g.side_channel_formula_label) SetWindowTextW(g.side_channel_formula_label, side_channel_formula_label_text());
    if (g.side_channel_color) SetWindowTextW(g.side_channel_color, side_channel_color_button_text());
    if (g.side_point_group_visible) SetWindowTextW(g.side_point_group_visible, point_group_visible_text());
    if (g.side_point_group_new) SetWindowTextW(g.side_point_group_new, point_group_new_button_text());
    if (g.side_point_group_delete) SetWindowTextW(g.side_point_group_delete, side_point_group_delete_text());
    if (g.side_point_group_rename) SetWindowTextW(g.side_point_group_rename, side_point_group_rename_text());
    if (g.side_point_color_current) SetWindowTextW(g.side_point_color_current, point_current_color_button_text());
    if (g.side_point_group_color) SetWindowTextW(g.side_point_group_color, point_selected_group_color_button_text());
    if (g.side_point_label_groups) SetWindowTextW(g.side_point_label_groups, point_group_list_title());
    if (g.side_formula_apply_selected) SetWindowTextW(g.side_formula_apply_selected, side_formula_apply_selected_text());
    if (g.side_formula_apply_visible) SetWindowTextW(g.side_formula_apply_visible, side_formula_apply_visible_text());
    if (g.side_formula_reset_selected) SetWindowTextW(g.side_formula_reset_selected, side_formula_reset_selected_text());
    if (g.side_formula_reset_all) SetWindowTextW(g.side_formula_reset_all, side_formula_reset_all_text());

    const struct ToggleMap { int id; bool value; const wchar_t* text; } point_toggles[] = {
        {IDC_SIDE_PT_NUM, g.pdisp.number, side_pt_num_text()},
        {IDC_SIDE_PT_X, g.pdisp.x, side_pt_x_text()},
        {IDC_SIDE_PT_Y, g.pdisp.y, side_pt_y_text()},
        {IDC_SIDE_PT_DX, g.pdisp.dx, side_pt_dx_text()},
        {IDC_SIDE_PT_DY, g.pdisp.dy, side_pt_dy_text()},
        {IDC_SIDE_PT_INVDT, g.pdisp.inv_dt, side_pt_invdt_text()},
        {IDC_SIDE_PT_DIST, g.pdisp.dist, side_pt_dist_text()},
        {IDC_SIDE_PT_SNAP, g.snap_to_data, side_pt_snap_text()},
    };
    for (const auto& item : point_toggles) {
        HWND ctl = GetDlgItem(g.main, item.id);
        if (!ctl) continue;
        SetWindowTextW(ctl, item.text);
        set_toggle_checked(ctl, item.value);
    }
    sync_filter_controls_from_state();
    apply_side_panel_visibility();
}

const wchar_t* channel_show_all_text() {
    return (g_str == &kEn) ? L"All" : L"Все";
}

const wchar_t* channel_hide_all_text() {
    return (g_str == &kEn) ? L"None" : L"Скрыть";
}

void set_all_channels_visible(bool visible) {
    for (std::size_t i = 0; i < g.visible.size(); ++i) {
        g.visible[i] = visible ? 1 : 0;
        if (i < g.checks.size()) {
            set_toggle_checked(g.checks[i], visible);
        }
    }
    invalidate_plot_analysis_cache();
}

} // namespace gui
