// General settings window and its owner-drawn fields.
#include "gui_settings_window.hpp"
#include "gui_status.hpp"
#include "gui_hotkeys.hpp"
#include "gui_gap_details.hpp"
#include "gui_controls.hpp"
#include "gui_documents.hpp"
#include "gui_settings_hotkeys.hpp"
#include "gui_menu.hpp"
#include "gui_state.hpp"
#include "gui_theme.hpp"
#include "gui_text.hpp"
#include "gui_ids.hpp"
#include "gui_loading_drop.hpp"
#include "gui_settings.hpp"
#include "gui_render_data.hpp"
#include "gui_window.hpp"

namespace gui {

namespace {
WNDPROC g_axis_label_edit_proc = nullptr;

LRESULT CALLBACK AxisLabelEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETDLGCODE) {
        return CallWindowProcW(g_axis_label_edit_proc, hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        SetFocus(GetParent(hwnd));
        return 0;
    }
    return CallWindowProcW(g_axis_label_edit_proc, hwnd, msg, wp, lp);
}

void install_axis_label_edit(HWND edit) {
    if (!edit) return;
    WNDPROC previous = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(AxisLabelEditProc)));
    if (!g_axis_label_edit_proc) g_axis_label_edit_proc = previous;
}
} // namespace

void measure_settings_combo_item(MEASUREITEMSTRUCT* mis) {
    if (!mis || mis->CtlType != ODT_COMBOBOX) return;
    mis->itemHeight = 22;
}

void draw_settings_combo_item(const DRAWITEMSTRUCT* dis) {
    if (!dis || dis->CtlType != ODT_COMBOBOX) return;
    HWND combo = dis->hwndItem;
    RECT r = dis->rcItem;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const bool combo_edit = (dis->itemState & ODS_COMBOBOXEDIT) != 0;
    COLORREF bg = combo_edit ? g_theme->bg_plot : (selected ? g_theme->btn_hover : g_theme->bg_plot);
    COLORREF text_col = disabled ? g_theme->text_secondary : g_theme->text_primary;

    if (combo_edit) {
        fill_rounded_rect(dis->hDC, r, g_theme->bg_plot, g_theme->btn_border, 5);
    } else if (selected) {
        RECT selected_rect = r;
        InflateRect(&selected_rect, -2, -1);
        fill_rounded_rect(dis->hDC, selected_rect, bg, mix_color(g_theme->accent, g_theme->btn_border, 78), 4);
    } else {
        HBRUSH bg_brush = CreateSolidBrush(bg);
        FillRect(dis->hDC, &r, bg_brush);
        DeleteObject(bg_brush);
    }

    UINT item = dis->itemID;
    if (item == static_cast<UINT>(-1) && combo) {
        LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) item = static_cast<UINT>(sel);
    }

    std::wstring text;
    if (combo && item != static_cast<UINT>(-1)) {
        LRESULT len = SendMessageW(combo, CB_GETLBTEXTLEN, item, 0);
        if (len >= 0) {
            text.resize(static_cast<std::size_t>(len));
            SendMessageW(combo, CB_GETLBTEXT, item, reinterpret_cast<LPARAM>(text.data()));
        }
    }
    if (text.empty() && combo && item != static_cast<UINT>(-1)) {
        WORD key = static_cast<WORD>(SendMessageW(combo, CB_GETITEMDATA, item, 0));
        text = key_name(key);
    }

    RECT text_rect = r;
    text_rect.left += 8;
    if (combo_edit) text_rect.right -= 8;
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text_col);
    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ old_font = SelectObject(dis->hDC, font);
    DrawTextW(dis->hDC, text.c_str(), -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dis->hDC, old_font);
}

void measure_settings_list_item(MEASUREITEMSTRUCT* mis) {
    if (!mis || mis->CtlType != ODT_LISTBOX) return;
    if (mis->CtlID != IDC_SET_HOTKEY_LIST && mis->CtlID != IDC_HOTKEYS_DIALOG_LIST) return;
    mis->itemHeight = 24;
}

void draw_settings_list_item(const DRAWITEMSTRUCT* dis) {
    if (!dis || dis->CtlType != ODT_LISTBOX ||
        (dis->CtlID != IDC_SET_HOTKEY_LIST && dis->CtlID != IDC_HOTKEYS_DIALOG_LIST)) return;

    RECT r = dis->rcItem;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const bool focused = (dis->itemState & ODS_FOCUS) != 0;
    const COLORREF base_bg = g_theme->bg_plot;
    const COLORREF row_bg = selected ? mix_color(base_bg, g_theme->accent, 24) : base_bg;
    const COLORREF row_border = selected ? mix_color(g_theme->accent, g_theme->btn_border, 78) : base_bg;
    const COLORREF text_col = disabled ? g_theme->text_secondary
        : (selected ? ((g_theme == &kDarkTheme) ? RGB(255, 255, 255) : RGB(12, 42, 78))
                    : g_theme->text_primary);

    HBRUSH bg = CreateSolidBrush(base_bg);
    FillRect(dis->hDC, &r, bg);
    DeleteObject(bg);

    if (dis->itemID != static_cast<UINT>(-1)) {
        std::wstring text;
        LRESULT len = SendMessageW(dis->hwndItem, LB_GETTEXTLEN, dis->itemID, 0);
        if (len >= 0) {
            text.resize(static_cast<std::size_t>(len));
            SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, reinterpret_cast<LPARAM>(text.data()));
        }

        if (selected) {
            RECT fill = r;
            InflateRect(&fill, -2, -2);
            fill_rounded_rect(dis->hDC, fill, row_bg, row_border, 5);
        }

        const std::size_t shortcut_begin = text.rfind(L"  [");
        const bool has_shortcut = shortcut_begin != std::wstring::npos && !text.empty() && text.back() == L']';
        const std::wstring title = has_shortcut ? text.substr(0, shortcut_begin) : text;
        const std::wstring shortcut = has_shortcut
            ? text.substr(shortcut_begin + 3, text.size() - shortcut_begin - 4)
            : L"";

        HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ old_font = SelectObject(dis->hDC, font);
        SIZE shortcut_size{};
        if (has_shortcut) {
            GetTextExtentPoint32W(dis->hDC, shortcut.c_str(), static_cast<int>(shortcut.size()), &shortcut_size);
            const int pill_w = shortcut_size.cx + 16;
            RECT pill = {r.right - pill_w - 8, r.top + 4, r.right - 8, r.bottom - 4};
            const COLORREF pill_bg = selected
                ? mix_color(g_theme->accent, row_bg, 72)
                : mix_color(g_theme->btn_hover, base_bg, 86);
            const COLORREF pill_border = selected
                ? mix_color(g_theme->accent, row_border, 60)
                : g_theme->btn_border;
            fill_rounded_rect(dis->hDC, pill, pill_bg, pill_border, 8);
            RECT shortcut_rect = pill;
            shortcut_rect.left += 8;
            shortcut_rect.right -= 8;
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, selected ? RGB(255, 255, 255) : g_theme->text_secondary);
            DrawTextW(dis->hDC, shortcut.c_str(), -1, &shortcut_rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        RECT text_rect = r;
        text_rect.left += 10;
        text_rect.right -= has_shortcut ? shortcut_size.cx + 32 : 10;
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, text_col);
        DrawTextW(dis->hDC, title.c_str(), -1, &text_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dis->hDC, old_font);
    }

    if (focused && dis->itemID != static_cast<UINT>(-1)) {
        RECT focus = r;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(dis->hDC, &focus);
    }
}

void draw_settings_group_box(HDC dc, const RECT& r, const wchar_t* text) {
    RECT text_rect = r;
    text_rect.left += 10;
    text_rect.right -= 10;
    text_rect.top += 1;

    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, g_theme->text_primary);

    SIZE sz{};
    const int text_len = lstrlenW(text);
    GetTextExtentPoint32W(dc, text, text_len, &sz);

    const int text_x = r.left + 12;
    const int text_y = r.top + 1;
    const int gap_left = text_x - 6;
    const int gap_right = text_x + sz.cx + 6;
    const int line_y = r.top + 10;

    HPEN pen = CreatePen(PS_SOLID, 1, g_theme->btn_border);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, r.left, line_y, nullptr); LineTo(dc, static_cast<int>(max<LONG>(r.left, gap_left)), line_y);
    MoveToEx(dc, static_cast<int>(min<LONG>(r.right, gap_right)), line_y, nullptr); LineTo(dc, r.right, line_y);
    MoveToEx(dc, r.left, line_y, nullptr); LineTo(dc, r.left, r.bottom);
    MoveToEx(dc, r.right - 1, line_y, nullptr); LineTo(dc, r.right - 1, r.bottom);
    MoveToEx(dc, r.left, r.bottom - 1, nullptr); LineTo(dc, r.right, r.bottom - 1);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    SetBkColor(dc, g_theme->bg_panel);
    ExtTextOutW(dc, text_x, text_y, ETO_OPAQUE, nullptr, text, text_len, nullptr);
    SelectObject(dc, old_font);
}

const wchar_t* settings_button_text() {
    return (g_str == &kEn) ? L"Settings" : L"Настройки";
}

const wchar_t* settings_window_title() {
    return (g_str == &kEn) ? L"Settings" : L"Настройки";
}

void refresh_settings_controls() {
    if (!g.settings_wnd) return;
    CheckRadioButton(g.settings_wnd, IDC_SET_LANG_RU, IDC_SET_LANG_EN, g_str == &kEn ? IDC_SET_LANG_EN : IDC_SET_LANG_RU);
    if (HWND theme = GetDlgItem(g.settings_wnd, IDW_THEME_LIGHT)) InvalidateRect(theme, nullptr, FALSE);
    if (HWND theme = GetDlgItem(g.settings_wnd, IDW_THEME_DARK)) InvalidateRect(theme, nullptr, FALSE);
    if (HWND light = GetDlgItem(g.settings_wnd, IDW_LIGHT_MODE)) {
        set_toggle_checked(light, g.light_mode);
    }
    set_toggle_checked(GetDlgItem(g.settings_wnd, IDC_SET_GAP_MARKERS), g.show_gap_markers);
    if (HWND gap = GetDlgItem(g.settings_wnd, IDC_SET_GAP_MARKERS)) SetWindowTextW(gap, gap_markers_toggle_text());
    set_toggle_checked(GetDlgItem(g.settings_wnd, IDC_SET_STITCH_GAPS), g.stitch_time_gaps);
    if (HWND stitch = GetDlgItem(g.settings_wnd, IDC_SET_STITCH_GAPS)) SetWindowTextW(stitch, stitch_gaps_toggle_text());
    const std::pair<int,const std::wstring*> axis_edits[] = {
        {IDC_SET_TIME_AXIS_X_EDIT,&g.axis_x_label}, {IDC_SET_TIME_AXIS_Y_EDIT,&g.time_axis_y_label},
        {IDC_SET_FFT_AXIS_X_EDIT,&g.fft_axis_x_label},
        {IDC_SET_FFT_AXIS_Y_EDIT,&g.fft_axis_y_label}, {IDC_SET_FRF_AXIS_X_EDIT,&g.frf_axis_x_label},
        {IDC_SET_FRF_AXIS_Y_EDIT,&g.frf_axis_y_label}};
    for (const auto& [id,value] : axis_edits) if (HWND edit=GetDlgItem(g.settings_wnd,id)) {
        g.updating_axis_label_edits=true; SetWindowTextW(edit,value->c_str()); g.updating_axis_label_edits=false;
    }
    populate_hotkey_list(g.settings_wnd);
    load_selected_hotkey_controls(g.settings_wnd);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            auto is_settings_group = [](int id) {
                return id == IDC_SET_GROUP_GENERAL ||
                       id == IDC_SET_GROUP_HOTKEYS;
            };
            auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
                HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
                    id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr, inst, nullptr);
                if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (c && is_settings_group(id)) EnableWindow(c, FALSE);
                return c;
            };
            auto mkcheck = [&](const wchar_t* text, int x, int y, int w, int h, int id) {
                return mk(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, x, y, w, h, id);
            };
            const bool en = (g_str == &kEn);
            mk(L"BUTTON", en ? L"General" : L"Общие", BS_OWNERDRAW, 12, 10, 510, 332, IDC_SET_GROUP_GENERAL);
            mk(L"BUTTON", g_str->lang_ru, BS_OWNERDRAW, 28, 36, 110, 22, IDC_SET_LANG_RU);
            mk(L"BUTTON", g_str->lang_en, BS_OWNERDRAW, 144, 36, 110, 22, IDC_SET_LANG_EN);
            mk(L"BUTTON", g_str->theme_light, BS_OWNERDRAW, 264, 36, 120, 22, IDW_THEME_LIGHT);
            mk(L"BUTTON", g_str->theme_dark, BS_OWNERDRAW, 390, 36, 120, 22, IDW_THEME_DARK);
            mkcheck(g_str->light_mode, 28, 64, 278, 28, IDW_LIGHT_MODE);
            set_toggle_checked(GetDlgItem(hwnd, IDW_LIGHT_MODE), g.light_mode);
            mkcheck(gap_markers_toggle_text(), 28, 98, 278, 28, IDC_SET_GAP_MARKERS);
            mkcheck(stitch_gaps_toggle_text(), 28, 128, 360, 28, IDC_SET_STITCH_GAPS);
            // Keep coordinate names visibly separate from the general settings.
            mk(L"STATIC", en ? L"Time" : L"Время", SS_LEFT, 28, 180, 120, 20, 0);
            install_axis_label_edit(mk(L"EDIT", g.axis_x_label.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 144, 176, 120, 24, IDC_SET_TIME_AXIS_X_EDIT));
            install_axis_label_edit(mk(L"EDIT", g.time_axis_y_label.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 276, 176, 120, 24, IDC_SET_TIME_AXIS_Y_EDIT));
            mk(L"STATIC", en ? L"Spectrum" : L"Спектр", SS_LEFT, 28, 208, 120, 20, 0);
            install_axis_label_edit(mk(L"EDIT", g.fft_axis_x_label.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 144, 204, 120, 24, IDC_SET_FFT_AXIS_X_EDIT));
            install_axis_label_edit(mk(L"EDIT", g.fft_axis_y_label.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 276, 204, 120, 24, IDC_SET_FFT_AXIS_Y_EDIT));
            mk(L"STATIC", en ? L"FRF" : L"АЧХ", SS_LEFT, 28, 236, 120, 20, 0);
            install_axis_label_edit(mk(L"EDIT", g.frf_axis_x_label.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 144, 232, 120, 24, IDC_SET_FRF_AXIS_X_EDIT));
            install_axis_label_edit(mk(L"EDIT", g.frf_axis_y_label.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 276, 232, 120, 24, IDC_SET_FRF_AXIS_Y_EDIT));
            mk(L"BUTTON", en ? L"Hotkeys" : L"Горячие клавиши", BS_OWNERDRAW, 12, 342, 510, 188, IDC_SET_GROUP_HOTKEYS);
            mk(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                24, 366, 240, 146, IDC_SET_HOTKEY_LIST);
            mk(L"BUTTON", L"Ctrl", BS_OWNERDRAW, 284, 374, 70, 22, IDC_SET_HOTKEY_CTRL);
            mk(L"BUTTON", L"Shift", BS_OWNERDRAW, 356, 374, 70, 22, IDC_SET_HOTKEY_SHIFT);
            mk(L"BUTTON", L"Alt", BS_OWNERDRAW, 428, 374, 70, 22, IDC_SET_HOTKEY_ALT);
            mk(L"STATIC", en ? L"Key:" : L"Клавиша:", SS_LEFT, 284, 406, 80, 20, 0);
            HWND combo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | CBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER, 284, 426, 214, 260, IDC_SET_HOTKEY_KEY);
            install_themed_combo(combo);
            populate_hotkey_key_combo(combo);
            mk(L"BUTTON", en ? L"Apply" : L"Применить", BS_OWNERDRAW, 284, 464, 100, 28, IDC_SET_HOTKEY_APPLY);
            mk(L"BUTTON", en ? L"Reset" : L"Сбросить", BS_OWNERDRAW, 398, 464, 100, 28, IDC_SET_HOTKEY_RESET);
            mk(L"BUTTON", en ? L"Clear" : L"Очистить", BS_OWNERDRAW, 284, 498, 100, 28, IDC_SET_HOTKEY_CLEAR);
            mk(L"BUTTON", en ? L"Reset all" : L"Сбросить всё", BS_OWNERDRAW, 398, 498, 100, 28, IDC_SET_HOTKEY_RESET_ALL);
            populate_hotkey_list(hwnd);
            load_selected_hotkey_controls(hwnd);
            CheckRadioButton(hwnd, IDC_SET_LANG_RU, IDC_SET_LANG_EN, g_str == &kEn ? IDC_SET_LANG_EN : IDC_SET_LANG_RU);
            set_toggle_checked(GetDlgItem(hwnd, IDW_LIGHT_MODE), g.light_mode);
            set_toggle_checked(GetDlgItem(hwnd, IDC_SET_GAP_MARKERS), g.show_gap_markers);
            set_toggle_checked(GetDlgItem(hwnd, IDC_SET_STITCH_GAPS), g.stitch_time_gaps);
            enable_file_drop_support(hwnd);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (!ctl && id) ctl = GetDlgItem(hwnd, id);
            auto checked = [&]() { return is_toggle_checked(ctl); };
            switch (id) {
                case IDC_SET_LANG_RU:
                    if (HIWORD(wp) == BN_CLICKED && g_str != &kRu) { g_str = &kRu; save_runtime_settings(); rebuild_ui(); }
                    break;
                case IDC_SET_LANG_EN:
                    if (HIWORD(wp) == BN_CLICKED && g_str != &kEn) { g_str = &kEn; save_runtime_settings(); rebuild_ui(); }
                    break;
                case IDW_LIGHT_MODE:
                    if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED) {
                        HWND light = GetDlgItem(hwnd, IDW_LIGHT_MODE);
                        toggle_checked_state(light);
                        apply_light_mode(is_toggle_checked(light));
                    }
                    return 0;
                case IDW_THEME_LIGHT:
                    if (HIWORD(wp) == BN_CLICKED) apply_theme_choice(&kLightTheme);
                    return 0;
                case IDW_THEME_DARK:
                    if (HIWORD(wp) == BN_CLICKED) apply_theme_choice(&kDarkTheme);
                    return 0;
                case IDC_SET_GAP_MARKERS:
                    if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED) {
                        toggle_checked_state(ctl);
                        g.show_gap_markers = checked();
                        mark_active_document_dirty();
                        if (!g.show_gap_markers) hide_gap_details_card();
                        invalidate_plot_analysis_cache();
                        save_runtime_settings();
                        InvalidateRect(g.main, nullptr, FALSE);
                        refresh_settings_controls();
                    }
                    return 0;
                case IDC_SET_STITCH_GAPS:
                    if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED) {
                        toggle_checked_state(ctl);
                        g.stitch_time_gaps = checked();
                        mark_active_document_dirty();
                        hide_gap_details_card();
                        invalidate_plot_analysis_cache();
                        save_runtime_settings();
                        InvalidateRect(g.main, nullptr, FALSE);
                        refresh_settings_controls();
                    }
                    return 0;
                case IDC_SET_TIME_AXIS_X_EDIT:
                case IDC_SET_TIME_AXIS_Y_EDIT:
                case IDC_SET_FFT_AXIS_X_EDIT:
                case IDC_SET_FFT_AXIS_Y_EDIT:
                case IDC_SET_FRF_AXIS_X_EDIT:
                case IDC_SET_FRF_AXIS_Y_EDIT: {
                    if (HIWORD(wp) != EN_KILLFOCUS || g.updating_axis_label_edits) return 0;
                    wchar_t buf[128]{};
                    GetWindowTextW(ctl, buf, 128);
                    const bool y_axis = id == IDC_SET_TIME_AXIS_Y_EDIT || id == IDC_SET_FFT_AXIS_Y_EDIT ||
                        id == IDC_SET_FRF_AXIS_Y_EDIT;
                    std::wstring label = normalize_axis_label_text(buf, y_axis ? L"Y" : L"X");
                    std::wstring* target = id == IDC_SET_TIME_AXIS_X_EDIT ? &g.axis_x_label :
                        id == IDC_SET_TIME_AXIS_Y_EDIT ? &g.time_axis_y_label :
                        id == IDC_SET_FFT_AXIS_X_EDIT ? &g.fft_axis_x_label :
                        id == IDC_SET_FFT_AXIS_Y_EDIT ? &g.fft_axis_y_label :
                        id == IDC_SET_FRF_AXIS_X_EDIT ? &g.frf_axis_x_label : &g.frf_axis_y_label;
                    const bool changed = *target != label;
                    *target = label;
                    if (changed) mark_active_document_dirty();
                    g.updating_axis_label_edits = true;
                    SetWindowTextW(ctl, label.c_str());
                    g.updating_axis_label_edits = false;
                    save_runtime_settings();
                    InvalidateRect(g.main, nullptr, FALSE);
                    return 0;
                }
                case IDC_SET_HOTKEY_CTRL:
                case IDC_SET_HOTKEY_SHIFT:
                case IDC_SET_HOTKEY_ALT:
                    if (HIWORD(wp) == BN_CLICKED) toggle_checked_state(ctl);
                    return 0;
                case IDC_SET_HOTKEY_LIST:
                    if (HIWORD(wp) == LBN_SELCHANGE) load_selected_hotkey_controls(hwnd);
                    return 0;
                case IDC_SET_HOTKEY_APPLY:
                case IDC_SET_HOTKEY_RESET:
                case IDC_SET_HOTKEY_CLEAR:
                case IDC_SET_HOTKEY_RESET_ALL: {
                    if (id == IDC_SET_HOTKEY_RESET_ALL) {
                        if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED) {
                            reset_all_hotkeys_to_defaults(hwnd);
                        }
                        return 0;
                    }
                    const int command = settings_selected_hotkey_command(hwnd);
                    if (!command) return 0;
                    BYTE fvirt = FVIRTKEY;
                    WORD key = 0;
                    if (id == IDC_SET_HOTKEY_RESET) {
                        HotkeyBinding def = default_hotkey_for_command(command);
                        fvirt = def.fvirt;
                        key = def.key;
                    } else if (id != IDC_SET_HOTKEY_CLEAR) {
                        if (is_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_CTRL))) fvirt |= FCONTROL;
                        if (is_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_SHIFT))) fvirt |= FSHIFT;
                        if (is_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_ALT))) fvirt |= FALT;
                        HWND combo = GetDlgItem(hwnd, IDC_SET_HOTKEY_KEY);
                        int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
                        if (sel != CB_ERR) key = static_cast<WORD>(SendMessageW(combo, CB_GETITEMDATA, sel, 0));
                    }
                    int conflict = find_conflicting_hotkey(fvirt, key, command);
                    if (conflict) {
                        std::wstring msg = (g_str == &kEn ? L"Already used by: " : L"Уже используется действием: ");
                        msg += command_name(conflict);
                        MessageBoxW(hwnd, msg.c_str(), settings_window_title(), MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    set_hotkey_binding(command, fvirt, key);
                    rebuild_accelerators();
                    rebuild_menu_bar();
                    populate_hotkey_list(hwnd);
                    load_selected_hotkey_controls(hwnd);
                    save_runtime_settings();
                    set_status();
                    InvalidateRect(g.main, nullptr, TRUE);
                    return 0;
                }
                default: return 0;
            }
            InvalidateRect(g.main, nullptr, FALSE);
            return 0;
        }
        case WM_DROPFILES:
            if (g.main && IsWindow(g.main)) {
                SendMessageW(g.main, WM_DROPFILES, wp, lp);
                return 0;
            }
            return 0;
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wp);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(g_theme->bg_panel);
            FillRect(dc, &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (dis && dis->CtlType == ODT_COMBOBOX && dis->CtlID == IDC_SET_HOTKEY_KEY) {
                draw_settings_combo_item(dis);
                return TRUE;
            }
            if (dis && dis->CtlType == ODT_LISTBOX && dis->CtlID == IDC_SET_HOTKEY_LIST) {
                draw_settings_list_item(dis);
                return TRUE;
            }
            if (!dis->hwndItem) break;
            const int ctl_id = GetDlgCtrlID(dis->hwndItem);
            if (ctl_id == IDC_SET_GROUP_GENERAL ||
                ctl_id == IDC_SET_GROUP_HOTKEYS) {
                wchar_t txt[128]{};
                GetWindowTextW(dis->hwndItem, txt, 128);
                draw_settings_group_box(dis->hDC, dis->rcItem, txt);
                return TRUE;
            }
            wchar_t txt[128];
            GetWindowTextW(dis->hwndItem, txt, 128);
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            if (is_settings_toggle_button_id(ctl_id)) {
                if (is_settings_hotkey_modifier_id(ctl_id)) {
                    const bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
                    draw_themed_check_control(dis->hDC, dis->rcItem, txt,
                        is_toggle_checked(dis->hwndItem), pressed, enabled, false, false);
                } else {
                    bool active = false;
                    if (ctl_id == IDC_SET_LANG_RU) active = g_str == &kRu;
                    else if (ctl_id == IDC_SET_LANG_EN) active = g_str == &kEn;
                    else if (ctl_id == IDW_THEME_LIGHT) active = g_theme == &kLightTheme;
                    else if (ctl_id == IDW_THEME_DARK) active = g_theme == &kDarkTheme;
                    draw_themed_button(dis->hDC, dis->rcItem, txt, pressed, active, false);
                }
                return TRUE;
            }
            if (is_settings_checkbox_id(ctl_id)) {
                const bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
                draw_themed_check_control(dis->hDC, dis->rcItem, txt,
                    is_toggle_checked(dis->hwndItem), pressed, enabled, false, false);
                return TRUE;
            }
            draw_themed_button(dis->hDC, dis->rcItem, txt, pressed, false, false);
            return TRUE;
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mis && mis->CtlType == ODT_COMBOBOX && mis->CtlID == IDC_SET_HOTKEY_KEY) {
                measure_settings_combo_item(mis);
                return TRUE;
            }
            if (mis && mis->CtlType == ODT_LISTBOX && mis->CtlID == IDC_SET_HOTKEY_LIST) {
                measure_settings_list_item(mis);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_plot);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
        }
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_plot);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);   // keep state; reopen instantly
            return 0;
        case WM_DESTROY:
            g.settings_wnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void open_settings() {
    if (!g.settings_wnd) {
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
            g.settings_wnd = CreateWindowExW(
            WS_EX_TOOLWINDOW, L"LvmPtSettings", settings_window_title(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 540, 568,
            g.main, nullptr, inst, nullptr);
        if (!g.settings_wnd) return;
        RECT mr, sr;
        GetWindowRect(g.main, &mr);
        GetWindowRect(g.settings_wnd, &sr);
        const int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
        SetWindowPos(g.settings_wnd, nullptr,
                     mr.left + ((mr.right - mr.left) - sw) / 2,
                     mr.top + ((mr.bottom - mr.top) - sh) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    refresh_settings_controls();
    ShowWindow(g.settings_wnd, SW_SHOW);
    SetForegroundWindow(g.settings_wnd);
}

} // namespace gui
