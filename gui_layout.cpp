// Layout: native viewer implementation.
#include "gui_layout.hpp"
#include "gui_frf.hpp"
#include "gui_ids.hpp"
#include "gui_side_panel.hpp"
#include "gui_state.hpp"
#include "gui_status.hpp"

namespace gui {

void layout() {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int cw = rc.right, ch = rc.bottom;
    update_status_tooltip();

    g.toolbar_seps.clear();
    int x = 8;
    auto place = [&](HWND h, int w, int row_y) { MoveWindow(h, x, row_y, w, 28, TRUE); x += w + 4; };
    auto sep = [&]() { x += 20; };
    auto text_button_width = [&](HWND h, int min_w, int pad) {
        if (!h) return min_w;
        wchar_t text[128]{};
        GetWindowTextW(h, text, 128);
        HDC dc = GetDC(h);
        HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ old_font = SelectObject(dc, font);
        SIZE sz{};
        GetTextExtentPoint32W(dc, text, lstrlenW(text), &sz);
        SelectObject(dc, old_font);
        ReleaseDC(h, dc);
        return max(min_w, static_cast<int>(sz.cx) + pad);
    };

    // Row 1: source, then a tightly grouped analysis-mode selector.
    x = 8;
    place(g.open, text_button_width(g.open, 94, 32), 8);
    place(g.document_selector, max(140, min(320, cw - 460)), 8);
    SetWindowTextW(g.document_close, L"×");
    place(g.document_close, 28, 8);
    sep();
    place(g.mode_time, text_button_width(g.mode_time, 72, 28), 8);
    x -= 3;
    place(g.mode_freq, text_button_width(g.mode_freq, 82, 28), 8);
    x -= 3;
    place(g.mode_frf, text_button_width(g.mode_frf, 90, 28), 8);
    // Playback and reset remain accessible through View and their hotkeys.
    for (HWND h : {g.play, g.reset, g.vline_btn, g.hline_btn}) ShowWindow(h, SW_HIDE);

    // Row 2: graph tools and side panel
    x = 8;
    place(g.cursor_btn, text_button_width(g.cursor_btn, 76, 24), 40);
    place(g.measure, text_button_width(g.measure, 72, 28), 40);
    place(g.marker_btn, text_button_width(g.marker_btn, 72, 30), 40);
    place(g.line_menu_btn, text_button_width(g.line_menu_btn, 90, 28), 40);
    place(g.annotation_lock_btn, 30, 40);
    // View controls share the right edge as a vertical pair.
    const int auto_w = text_button_width(g.autoy, 108, 34);
    const int side_w = text_button_width(g.sidepanel_btn, 92, 32);
    MoveWindow(g.autoy, cw - auto_w - 8, 8, auto_w, 28, TRUE);
    MoveWindow(g.sidepanel_btn, cw - side_w - 8, 40, side_w, 28, TRUE);

    const int panel_w = side_panel_width();
    const int panel_left = cw - panel_w;
    const int panel_pad_left = 12;
    const int panel_pad_right = 16;
    const int panel_x = panel_left + panel_pad_left;
    const int viewport_bottom = ch - kBottomBar - 6;
    const int channels_viewport_top = kTopBar + 78;
    const int points_viewport_top = kTopBar + 48;
    const int content_w = max(60, panel_w - panel_pad_left - panel_pad_right);

    MoveWindow(g.show_all_btn, panel_x, kTopBar + 42, 86, 28, FALSE);
    MoveWindow(g.hide_all_btn, panel_x + 92, kTopBar + 42, 86, 28, FALSE);
    const int tab_gap = 4;
    const int tab_w = max(84, (content_w - tab_gap * 2) / 3);
    if (g.mode == AnalysisMode::FRF) {
        const int frf_tab_w = max(84, (content_w - tab_gap) / 2);
        if (g.side_tab_channels) MoveWindow(g.side_tab_channels, panel_x, kTopBar + 8, frf_tab_w, 28, FALSE);
        if (g.side_tab_points) MoveWindow(g.side_tab_points, panel_x + frf_tab_w + tab_gap,
            kTopBar + 8, frf_tab_w, 28, FALSE);
    } else {
        if (g.side_tab_channels) MoveWindow(g.side_tab_channels, panel_x, kTopBar + 8, tab_w, 28, FALSE);
        if (g.side_tab_points) MoveWindow(g.side_tab_points, panel_x + tab_w + tab_gap, kTopBar + 8, tab_w, 28, FALSE);
        if (g.side_tab_filter) MoveWindow(g.side_tab_filter, panel_x + (tab_w + tab_gap) * 2, kTopBar + 8, tab_w, 28, FALSE);
    }
    apply_side_panel_visibility();

    const bool show_channels = g.side_panel_visible && g.side_panel_tab == 0 && !welcome_visible() && g.mode != AnalysisMode::FRF;
    const bool show_points = g.side_panel_visible && g.side_panel_tab == 1 && !welcome_visible() &&
        (g.mode != AnalysisMode::FRF || g.frf_point_settings_open);
    const bool show_filter = g.side_panel_visible && g.side_panel_tab == 2 && !welcome_visible() && g.mode != AnalysisMode::FRF;
    const int channels_content_top = channels_viewport_top;
    const int points_content_top = points_viewport_top;
    const int filter_viewport_top = points_viewport_top;
    const int active_viewport_top = (g.side_panel_tab == 0) ? channels_content_top : (g.side_panel_tab == 1 ? points_content_top : filter_viewport_top);

    auto place_scrolled = [&](HWND ctl, int x0, int y_rel, int w, int h, int viewport_top, bool visible_in_tab) {
        if (!ctl) return;
        const int y_abs = viewport_top + y_rel - g.side_scroll_y;
        const bool visible = visible_in_tab && y_abs + h > viewport_top && y_abs < viewport_bottom;
        ShowWindow(ctl, visible ? SW_SHOW : SW_HIDE);
        if (visible) MoveWindow(ctl, x0, y_abs, max(1, w), max(1, h), FALSE);
    };

    int y = 0;
    place_scrolled(g.side_global_formula_label, panel_x, y, content_w, 20, channels_content_top, show_channels);
    y += 24;
    place_scrolled(g.side_global_formula_edit, panel_x, y, content_w, 26, channels_content_top, show_channels);
    y += 32;
    place_scrolled(g.side_global_formula_apply, panel_x, y, content_w, 28, channels_content_top, show_channels);
    y += 38;
    place_scrolled(g.side_channel_separator, panel_x, y, content_w, 2, channels_content_top, show_channels);
    y += 10;
    place_scrolled(g.side_channel_formula_label, panel_x, y, content_w, 20, channels_content_top, show_channels);
    y += 26;
    for (std::size_t i = 0; i < g.checks.size(); ++i) {
        place_scrolled(g.checks[i], panel_x, y + 2, 18, 20, channels_content_top, show_channels);
        const int coefficient_w = 62;
        const int label_w = max(60, content_w - 28 - coefficient_w - 6);
        if (i < g.check_labels.size()) {
            place_scrolled(g.check_labels[i], panel_x + 24, y, label_w, 24, channels_content_top, show_channels);
        }
        if (i < g.channel_coefficient_edits.size()) {
            place_scrolled(g.channel_coefficient_edits[i], panel_x + 24 + label_w + 6, y + 1, coefficient_w, 22,
                           channels_content_top, show_channels);
        }
        y += 26;
    }
    if (g.channel_edit && g.editing_channel >= 0 && g.editing_channel < static_cast<int>(g.check_labels.size())) {
        const bool edit_visible = show_channels && IsWindowVisible(g.check_labels[g.editing_channel]) != FALSE;
        ShowWindow(g.channel_edit, edit_visible ? SW_SHOW : SW_HIDE);
        if (edit_visible) {
            RECT r;
            GetWindowRect(g.check_labels[g.editing_channel], &r);
            MapWindowPoints(nullptr, g.main, reinterpret_cast<LPPOINT>(&r), 2);
            MoveWindow(g.channel_edit, r.left - 2, r.top - 1, (r.right - r.left) + 4, (r.bottom - r.top) + 2, FALSE);
        }
    }

    if (g.side_channel_color) {
        int cy = y + 8;
        place_scrolled(g.side_channel_color, panel_x, cy, content_w, 28, channels_content_top, show_channels);
        g.side_content_height_channels = cy + 28;
    }

    int fy = 0;
    place_scrolled(g.side_channel_hint, panel_x, fy, content_w, 20, filter_viewport_top, show_filter);
    fy += 24;
    place_scrolled(g.side_filter_enable, panel_x, fy, content_w, 24, filter_viewport_top, show_filter);
    fy += 30;
    place_scrolled(g.side_filter_mode_label, panel_x, fy + 2, 76, 20, filter_viewport_top, show_filter);
    place_scrolled(g.side_filter_mode, panel_x + 80, fy, max(84, content_w - 80), 26, filter_viewport_top, show_filter);
    fy += 32;
    place_scrolled(g.side_filter_topology_label, panel_x, fy + 2, 76, 20, filter_viewport_top, show_filter);
    place_scrolled(g.side_filter_topology, panel_x + 80, fy, max(84, content_w - 80), 26, filter_viewport_top, show_filter);
    fy += 34;
    place_scrolled(g.side_filter_low_label, panel_x, fy, max(72, content_w - 92), 20, filter_viewport_top, show_filter);
    place_scrolled(g.side_filter_low_value, panel_x + max(72, content_w - 92) + 6, fy, 86, 20, filter_viewport_top, show_filter);
    fy += 22;
    place_scrolled(g.side_filter_low_track, panel_x, fy, content_w, 24, filter_viewport_top, show_filter);
    fy += 32;
    place_scrolled(g.side_filter_high_label, panel_x, fy, max(72, content_w - 92), 20, filter_viewport_top, show_filter);
    place_scrolled(g.side_filter_high_value, panel_x + max(72, content_w - 92) + 6, fy, 86, 20, filter_viewport_top, show_filter);
    fy += 22;
    place_scrolled(g.side_filter_high_track, panel_x, fy, content_w, 24, filter_viewport_top, show_filter);
    fy += 36;
    g.side_content_height_filter = fy + 28;

    const int points_left = panel_x;
    const int col_gap = 6;
    const int checkbox_col_w = max(110, (content_w - col_gap) / 2);
    const int checkbox_col2_x = points_left + checkbox_col_w + col_gap;
    int py = 0;
    auto place_side = [&](int id, int x0, int y0, int w, int h) {
        HWND ctl = GetDlgItem(g.main, id);
        place_scrolled(ctl, x0, y0, w, h, points_viewport_top, show_points);
    };
    place_side(IDC_SIDE_PT_NUM, points_left, py, checkbox_col_w, 22);
    place_side(IDC_SIDE_PT_X, checkbox_col2_x, py, checkbox_col_w, 22);
    py += 24;
    place_side(IDC_SIDE_PT_Y, points_left, py, checkbox_col_w, 22);
    place_side(IDC_SIDE_PT_DX, checkbox_col2_x, py, checkbox_col_w, 22);
    py += 24;
    place_side(IDC_SIDE_PT_DY, points_left, py, checkbox_col_w, 22);
    place_side(IDC_SIDE_PT_INVDT, checkbox_col2_x, py, checkbox_col_w, 22);
    py += 24;
    place_side(IDC_SIDE_PT_DIST, points_left, py, checkbox_col_w, 22);
    place_side(IDC_SIDE_PT_SNAP, checkbox_col2_x, py, checkbox_col_w, 22);
    py += 30;
    place_side(IDC_SIDE_POINT_COLOR_CURRENT, points_left, py, content_w, 28);
    py += 38;
    place_scrolled(g.side_point_label_groups, points_left, py, content_w, 20, points_viewport_top, show_points);
    py += 24;
    place_scrolled(g.side_point_group_list, points_left, py, content_w, 122, points_viewport_top, show_points);
    py += 130;
    place_scrolled(g.side_point_group_visible, points_left, py, content_w, 22, points_viewport_top, show_points);
    py += 30;
    place_scrolled(g.side_point_group_color, points_left, py, content_w, 28, points_viewport_top, show_points);
    py += 36;
    place_scrolled(g.side_point_group_name, points_left, py, content_w, 24, points_viewport_top, show_points);
    py += 30;
    place_scrolled(g.side_point_group_rename, points_left, py, content_w, 28, points_viewport_top, show_points);
    py += 36;
    if (g.side_point_group_new) place_scrolled(g.side_point_group_new, points_left, py, (content_w - 6) / 2, 28, points_viewport_top, show_points);
    if (g.side_point_group_delete) place_scrolled(g.side_point_group_delete, points_left + (content_w - 6) / 2 + 6, py, (content_w - 6) / 2, 28, points_viewport_top, show_points);
    g.side_content_height_points = py + 28;

    update_side_panel_scrollbar(active_viewport_top,
                                g.side_panel_tab == 0 ? g.side_content_height_channels :
                                (g.side_panel_tab == 1 ? g.side_content_height_points : g.side_content_height_filter));

    layout_frf_panel();
    MoveWindow(g.status, 8, ch - kBottomBar + 4, cw - 16, 20, FALSE);
}

RECT plot_rect() {
    RECT rc;
    GetClientRect(g.main, &rc);
    RECT p;
    p.left = kAxisLeft;
    p.top = kTopBar + 6;
    p.right = rc.right - side_panel_width();
    p.bottom = rc.bottom - kBottomBar - kAxisBottom;
    if (p.right < p.left + 20) p.right = p.left + 20;
    if (p.bottom < p.top + 20) p.bottom = p.top + 20;
    return p;
}

} // namespace gui
