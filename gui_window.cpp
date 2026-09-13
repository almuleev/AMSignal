// Window: native viewer implementation.
#include "gui_window.hpp"
#include "gui_frf.hpp"
#include "gui_menu.hpp"
#include "gui_settings_window.hpp"
#include "gui_controls.hpp"
#include "gui_ids.hpp"
#include "gui_layout.hpp"
#include "gui_documents.hpp"
#include "gui_loading.hpp"
#include "gui_loading_drop.hpp"
#include "gui_playback.hpp"
#include "gui_render.hpp"
#include "gui_settings.hpp"
#include "gui_side_panel.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"
#include "gui_welcome.hpp"

namespace gui {

// ---- UI rebuild (language switch) --------------------------------------
void rebuild_ui() {
    if (!g.main) return;
    finish_channel_rename(true);
    rebuild_menu_bar();

    // Update buttons
    SetWindowTextW(g.open, g_str->btn_open);
    SetWindowTextW(g.document_close, g_str == &kEn ? L"Close" : L"Закрыть");
    refresh_open_document_selector();
    SetWindowTextW(g.savepng, g_str->btn_png);
    SetWindowTextW(g.savecsv, g_str->btn_csv);
    SetWindowTextW(g.mode_time, g_str->st_time);
    SetWindowTextW(g.mode_freq, g_str->st_hz);
    SetWindowTextW(g.mode_frf, g_str == &kEn ? L"FRF" : L"FRF / АЧХ");
    refresh_frf_controls(true);
    SetWindowTextW(g.play, g.playing ? g_str->btn_pause : g_str->btn_play);
    SetWindowTextW(g.measure, g_str->btn_measure);
    SetWindowTextW(g.marker_btn, g_str == &kEn ? L"Marker" : L"Маркер");
    SetWindowTextW(g.vline_btn, g_str == &kEn ? L"V-Line" : L"V-линия");
    SetWindowTextW(g.hline_btn, g_str == &kEn ? L"H-Line" : L"H-линия");
    SetWindowTextW(g.reset, g_str->btn_reset);
    SetWindowTextW(g.autoy, g_str->btn_autoy);
    SetWindowTextW(g.sidepanel_btn, side_panel_button_text());
    SetWindowTextW(g.show_all_btn, channel_show_all_text());
    SetWindowTextW(g.hide_all_btn, channel_hide_all_text());
    refresh_side_panel_controls();

    // Update welcome window if visible
    if (g.welcome_wnd && IsWindowVisible(g.welcome_wnd)) {
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        DestroyWindow(g.welcome_wnd);
        g.welcome_wnd = nullptr;
        show_welcome(inst);
    }

    if (g.settings_wnd) {
        bool was_visible = IsWindowVisible(g.settings_wnd) != FALSE;
        DestroyWindow(g.settings_wnd);
        g.settings_wnd = nullptr;
        if (was_visible) open_settings();
    }

    // Update main window title
    if (!g.file_name.empty()) {
        SetWindowTextW(g.main, (std::wstring(g_str->app_title) + L" — " + g.file_name).c_str());
    } else {
        SetWindowTextW(g.main, g_str->app_title);
    }

    InvalidateRect(g.main, nullptr, TRUE);
    set_status();
}

LRESULT handle_window_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;

            // Modern UI font (falls back to the stock font if unavailable).
            g.ui_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            NONCLIENTMETRICSW ncm{};
            ncm.cbSize = sizeof(ncm);
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
                g.menu_font = CreateFontIndirectW(&ncm.lfMenuFont);
            }
            if (!g.menu_font) {
                g.menu_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            }
            g.bold_font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g.title_font = CreateFontW(-30, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            if (!g.ui_font) g.ui_font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT font = g.ui_font;

            g.menu = make_menu();
            SetMenu(hwnd, g.menu);
            MENUINFO mi{};
            mi.cbSize = sizeof(mi);
            mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
            mi.hbrBack = CreateSolidBrush(g_theme->bg_toolbar);
            SetMenuInfo(g.menu, &mi);

            auto mk = [&](const wchar_t* text, int id, DWORD extra, bool in_toolbar = true) {
                HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW | extra,
                                         0, 0, 10, 10, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (in_toolbar) g.buttons.push_back(b);
                else ShowWindow(b, SW_HIDE);
                return b;
            };
            g.open = mk(g_str->btn_open, IDC_OPEN, 0);
            g.document_selector = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | CBS_DROPDOWNLIST |
                CBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER,
                0, 0, 10, 10, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DOCUMENT_SELECTOR)), inst, nullptr);
            SendMessageW(g.document_selector, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            install_themed_combo(g.document_selector);
            g.document_close = mk(g_str == &kEn ? L"Close" : L"Закрыть", IDC_CLOSE_DOCUMENT, 0);
            refresh_open_document_selector();
            g.savepng = mk(g_str->btn_png, IDC_SAVEPNG, 0, false);
            g.savecsv = mk(g_str->btn_csv, IDC_SAVECSV, 0, false);
            g.mode_time = mk(g_str->st_time, IDM_MODE_TIME, 0);
            g.mode_freq = mk(g_str->st_hz, IDM_MODE_FREQ, 0);
            g.mode_frf = mk(g_str == &kEn ? L"FRF" : L"FRF / АЧХ", IDM_MODE_FRF, 0);
            create_frf_panel(hwnd, inst);
            g.play = mk(g_str->btn_play, IDC_PLAY, 0);
            g.measure = mk(g_str->btn_measure, IDC_MEASURE, 0);
            g.marker_btn = mk(g_str == &kEn ? L"Marker" : L"Маркер", IDM_ADD_MARKER, 0);
            g.vline_btn = mk(g_str == &kEn ? L"V-Line" : L"V-линия", IDM_ADD_VLINE, 0);
            g.hline_btn = mk(g_str == &kEn ? L"H-Line" : L"H-линия", IDM_ADD_HLINE, 0);
            g.reset = mk(g_str->btn_reset, IDC_RESET, 0);
            g.autoy = mk(g_str->btn_autoy, IDC_AUTOY, 0);
            g.sidepanel_btn = mk(side_panel_button_text(), IDC_SIDEPANEL, 0);
            g.show_all_btn = mk(channel_show_all_text(), IDC_SHOW_ALL, 0);
            g.hide_all_btn = mk(channel_hide_all_text(), IDC_HIDE_ALL, 0);

            auto mk_panel_btn = [&](const wchar_t* text, int id, std::vector<HWND>& bucket) {
                HWND b = mk(text, id, 0);
                bucket.push_back(b);
                return b;
            };
            auto mk_panel_ctl = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, std::vector<HWND>& bucket) {
                HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
                                         0, 0, 10, 10, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                if (c) {
                    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                    bucket.push_back(c);
                }
                return c;
            };

            g.side_tab_channels = mk(side_tab_channels_text(), IDC_SIDE_TAB_CHANNELS, 0);
            g.side_tab_points = mk(side_tab_points_text(), IDC_SIDE_TAB_POINTS, 0);
            g.side_tab_filter = mk(side_tab_filter_text(), IDC_SIDE_TAB_FILTER, 0);
            g.side_channel_hint = mk_panel_ctl(L"STATIC", side_channel_hint_text(),
                                               SS_LEFT | SS_NOPREFIX, IDC_SIDE_CHANNEL_HINT, g.side_filter_controls);
            g.side_filter_enable = mk_panel_ctl(L"BUTTON", filter_toggle_text(),
                                                BS_OWNERDRAW, IDC_SIDE_FILTER_ENABLE, g.side_filter_controls);
            g.side_filter_mode_label = mk_panel_ctl(L"STATIC", filter_mode_label_text(),
                                                    SS_LEFT | SS_NOPREFIX, 0, g.side_filter_controls);
            g.side_filter_mode = mk_panel_ctl(L"COMBOBOX", L"",
                                              CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                                              CBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
                                              IDC_SIDE_FILTER_MODE, g.side_filter_controls);
            g.side_filter_topology_label = mk_panel_ctl(L"STATIC", filter_topology_label_text(),
                                                        SS_LEFT | SS_NOPREFIX, 0, g.side_filter_controls);
            g.side_filter_topology = mk_panel_ctl(L"COMBOBOX", L"",
                                                  CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                                                  CBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
                                                  IDC_SIDE_FILTER_TOPOLOGY, g.side_filter_controls);
            install_themed_combo(g.side_filter_mode);
            install_themed_combo(g.side_filter_topology);
            g.side_filter_low_label = mk_panel_ctl(L"STATIC", filter_low_cutoff_text(),
                                                   SS_LEFT | SS_NOPREFIX, 0, g.side_filter_controls);
            g.side_filter_low_value = mk_panel_ctl(L"STATIC", L"", SS_RIGHT | SS_NOPREFIX, 0, g.side_filter_controls);
            g.side_filter_low_track = mk_panel_ctl(TRACKBAR_CLASS, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                                                   IDC_SIDE_FILTER_LOW_TRACK, g.side_filter_controls);
            g.side_filter_high_label = mk_panel_ctl(L"STATIC", filter_high_cutoff_text(),
                                                    SS_LEFT | SS_NOPREFIX, 0, g.side_filter_controls);
            g.side_filter_high_value = mk_panel_ctl(L"STATIC", L"", SS_RIGHT | SS_NOPREFIX, 0, g.side_filter_controls);
            g.side_filter_high_track = mk_panel_ctl(TRACKBAR_CLASS, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                                                    IDC_SIDE_FILTER_HIGH_TRACK, g.side_filter_controls);
            g.side_global_formula_label = mk_panel_ctl(L"STATIC", side_global_formula_label_text(), SS_LEFT, 0, g.side_channel_controls);
            g.side_global_formula_edit = mk_panel_ctl(L"EDIT", default_channel_formula_text().c_str(),
                                                      WS_BORDER | ES_AUTOHSCROLL, IDC_SIDE_GLOBAL_FORMULA_EDIT, g.side_channel_controls);
            install_side_panel_apply_edit(g.side_global_formula_edit);
            g.side_global_formula_apply = mk_panel_btn(side_global_formula_apply_text(), IDC_SIDE_GLOBAL_FORMULA_APPLY, g.side_channel_controls);
            g.side_channel_separator = mk_panel_ctl(L"STATIC", L"", SS_ETCHEDHORZ, 0, g.side_channel_controls);
            g.side_channel_formula_label = mk_panel_ctl(L"STATIC", side_channel_formula_label_text(), SS_LEFT, 0, g.side_channel_controls);
            g.side_channel_color = mk_panel_btn(side_channel_color_button_text(), IDC_SIDE_CHANNEL_COLOR, g.side_channel_controls);

            const struct PointToggleSeed { int id; const wchar_t* text; bool on; } point_toggle_seeds[] = {
                {IDC_SIDE_PT_NUM, side_pt_num_text(), g.pdisp.number},
                {IDC_SIDE_PT_X, side_pt_x_text(), g.pdisp.x},
                {IDC_SIDE_PT_Y, side_pt_y_text(), g.pdisp.y},
                {IDC_SIDE_PT_DX, side_pt_dx_text(), g.pdisp.dx},
                {IDC_SIDE_PT_DY, side_pt_dy_text(), g.pdisp.dy},
                {IDC_SIDE_PT_INVDT, side_pt_invdt_text(), g.pdisp.inv_dt},
                {IDC_SIDE_PT_DIST, side_pt_dist_text(), g.pdisp.dist},
                {IDC_SIDE_PT_SNAP, side_pt_snap_text(), g.snap_to_data},
            };
            for (const auto& seed : point_toggle_seeds) {
                HWND c = mk_panel_ctl(L"BUTTON", seed.text, BS_OWNERDRAW, seed.id, g.side_point_controls);
                if (c) set_toggle_checked(c, seed.on);
            }
            g.side_point_color_current = mk_panel_btn(point_current_color_button_text(), IDC_SIDE_POINT_COLOR_CURRENT, g.side_point_controls);
            g.side_point_label_groups = mk_panel_ctl(L"STATIC", point_group_list_title(), SS_LEFT, 0, g.side_point_controls);
            g.side_point_group_list = mk_panel_ctl(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER, IDC_SIDE_POINT_GROUP_LIST, g.side_point_controls);
            g.side_point_group_visible = mk_panel_ctl(L"BUTTON", point_group_visible_text(), BS_OWNERDRAW, IDC_SIDE_POINT_GROUP_VISIBLE, g.side_point_controls);
            g.side_point_group_color = mk_panel_btn(point_selected_group_color_button_text(), IDC_SIDE_POINT_GROUP_COLOR, g.side_point_controls);
            g.side_point_group_new = mk_panel_btn(point_group_new_button_text(), IDC_SIDE_POINT_GROUP_NEW, g.side_point_controls);
            g.side_point_group_delete = mk_panel_btn(side_point_group_delete_text(), IDC_SIDE_POINT_GROUP_DELETE, g.side_point_controls);
            g.side_point_group_name = mk_panel_ctl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, IDC_SIDE_POINT_GROUP_NAME, g.side_point_controls);
            install_side_panel_apply_edit(g.side_point_group_name);
            g.side_point_group_rename = mk_panel_btn(side_point_group_rename_text(), IDC_SIDE_POINT_GROUP_RENAME, g.side_point_controls);

            g.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                       0, 0, 10, 10, hwnd, nullptr, inst, nullptr);
            SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            ShowWindow(g.status, SW_HIDE);   // owner-drawn in on_paint

            g.axis_font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            enable_file_drop_support(hwnd);
            SetTimer(hwnd, 2, 50, nullptr);   // hover tracking timer
            update_theme_brushes();
            sync_menu();
            refresh_side_panel_controls();
            set_status();
            return 0;
        }
        case WM_SIZE:
            layout();
            if (welcome_visible()) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                SetWindowPos(g.welcome_wnd, HWND_TOP, 0, 0, rc.right, rc.bottom, SWP_SHOWWINDOW);
                InvalidateRect(g.welcome_wnd, nullptr, FALSE);
                return 0;
            }
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* m = reinterpret_cast<MINMAXINFO*>(lp);
            m->ptMinTrackSize.x = 980;
            m->ptMinTrackSize.y = 560;
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PARENTNOTIFY:
            if (LOWORD(wp) == WM_LBUTTONDOWN) {
                finish_channel_rename_if_click_outside(hwnd);
            }
            break;
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            SelectObject(dc, g.ui_font);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_plot);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_plot);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mis && mis->CtlType == ODT_MENU) {
                measure_owner_draw_menu(mis);
                return TRUE;
            }
            if (mis && mis->CtlType == ODT_COMBOBOX &&
                (mis->CtlID == IDC_SIDE_FILTER_MODE || mis->CtlID == IDC_SIDE_FILTER_TOPOLOGY)) {
                measure_settings_combo_item(mis);
                return TRUE;
            }
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            on_paint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (dis && dis->CtlType == ODT_MENU) {
                draw_owner_draw_menu(dis);
                return TRUE;
            }
            if (dis && dis->CtlType == ODT_COMBOBOX &&
                (dis->CtlID == IDC_SIDE_FILTER_MODE || dis->CtlID == IDC_SIDE_FILTER_TOPOLOGY)) {
                draw_settings_combo_item(dis);
                return TRUE;
            }
            HWND btn = dis->hwndItem;
            if (!btn) break;
            HDC dc = dis->hDC;
            const int ctl_id = GetDlgCtrlID(btn);
            if (is_channel_checkbox_id(ctl_id) || is_side_toggle_id(ctl_id)) {
                wchar_t txt[128]{};
                GetWindowTextW(btn, txt, 128);
                const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                const bool enabled = IsWindowEnabled(btn) != FALSE;
                const bool compact = is_channel_checkbox_id(ctl_id);
                draw_themed_check_control(dc, dis->rcItem, txt,
                    is_toggle_checked(btn), pressed, enabled, false, compact);
                return TRUE;
            }
            RECT r = dis->rcItem;
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            bool active = false;
            if (btn == g.measure) {
                active = g.measure_mode;
            } else if (btn == g.autoy) {
                active = g.mode == AnalysisMode::FRF ? g.frf.auto_y : g.auto_y;
            } else if (btn == g.mode_time) {
                active = (g.mode == AnalysisMode::Time);
            } else if (btn == g.mode_frf) {
                active = g.mode == AnalysisMode::FRF;
            } else if (btn == g.mode_freq) {
                active = (g.mode == AnalysisMode::FFT);
            } else if (btn == g.marker_btn) {
                active = g.pending_marker;
            } else if (btn == g.vline_btn) {
                active = g.pending_line == 1;
            } else if (btn == g.hline_btn) {
                active = g.pending_line == 2;
            } else if (btn == g.sidepanel_btn) {
                active = g.side_panel_visible;
            } else if (btn == g.side_tab_channels) {
                active = g.side_panel_tab == 0;
            } else if (btn == g.side_tab_points) {
                active = g.side_panel_tab == 1;
            } else if (btn == g.side_tab_filter) {
                active = g.side_panel_tab == 2;
            }
            bool hover = (btn == g.hovered_btn);
            wchar_t txt[128];
            GetWindowTextW(btn, txt, 128);
            draw_themed_button(dc, r, txt, pressed, active, hover);
            return TRUE;
        }
        case WM_DESTROY:
            KillTimer(hwnd, 3);
            if (g_settings_dirty) { save_runtime_settings_now(); g_settings_dirty = false; }
            g_frf_worker.cancel();
            g_spectrum_worker.cancel();
            request_async_load_cancel();
            if (g_load_worker.joinable()) g_load_worker.join();
            {
                MSG pending{};
                while (PeekMessageW(&pending, hwnd, WM_APP_ASYNC_SCAN_DONE, WM_APP_ASYNC_LOAD_DONE, PM_REMOVE)) {
                    if (pending.message == WM_APP_ASYNC_SCAN_DONE) delete reinterpret_cast<AsyncScanResult*>(pending.lParam);
                    else delete reinterpret_cast<AsyncLoadResult*>(pending.lParam);
                }
            }
            hide_loading();
            save_app_settings();
            stop_play();
            KillTimer(hwnd, 2);
            release_backbuffer();
            if (g.ui_font && g.ui_font != reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)))
                DeleteObject(g.ui_font);
            if (g.menu_font) DeleteObject(g.menu_font);
            if (g.bold_font) DeleteObject(g.bold_font);
            if (g.title_font) DeleteObject(g.title_font);
            if (g.axis_font) DeleteObject(g.axis_font);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace gui
