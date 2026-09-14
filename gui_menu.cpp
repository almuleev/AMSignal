// Menu construction, synchronization, and owner drawing.
#include "gui_menu.hpp"
#include "gui_frf.hpp"
#include "gui_hotkeys.hpp"
#include "gui_controls.hpp"
#include "gui_state.hpp"
#include "gui_theme.hpp"
#include "gui_text.hpp"
#include "gui_ids.hpp"
#include "gui_documents.hpp"
#include "gui_playback.hpp"
#include "gui_side_panel.hpp"
#include "gui_settings.hpp"

namespace gui {

std::wstring menu_text(const wchar_t* base, int command) {
    const HotkeyBinding* binding = find_hotkey_binding(command);
    if (!binding || binding->key == 0) return base;
    std::wstring hk = hotkey_text(binding->fvirt, binding->key);
    if (hk.empty()) return base;
    return std::wstring(base) + L"\t" + hk;
}

void append_menu_popup_owner_draw(HMENU menu, HMENU popup, const std::wstring& text,
                                  bool top_level) {
    AppendMenuW(
        menu,
        MF_OWNERDRAW | MF_POPUP,
        reinterpret_cast<UINT_PTR>(popup),
        reinterpret_cast<LPCWSTR>(stash_menu_entry(text, top_level, true)));
}

void append_menu_item_owner_draw(HMENU menu, UINT id, const std::wstring& text,
                                 const std::wstring& subtitle, bool recent_file) {
    AppendMenuW(
        menu,
        MF_OWNERDRAW | MF_STRING,
        id,
        reinterpret_cast<LPCWSTR>(stash_menu_entry(text, false, false, subtitle, recent_file)));
}

void modify_menu_item_owner_draw(HMENU menu, UINT id, const std::wstring& text) {
    ModifyMenuW(
        menu,
        id,
        MF_BYCOMMAND | MF_OWNERDRAW,
        id,
        reinterpret_cast<LPCWSTR>(stash_menu_entry(text, false, false)));
}

std::wstring menu_item_left_text(const std::wstring& text) {
    std::size_t tab = text.find(L'\t');
    return (tab == std::wstring::npos) ? text : text.substr(0, tab);
}

std::wstring menu_item_right_text(const std::wstring& text) {
    std::size_t tab = text.find(L'\t');
    return (tab == std::wstring::npos) ? L"" : text.substr(tab + 1);
}

void measure_owner_draw_menu(MEASUREITEMSTRUCT* mis) {
    if (!mis || mis->CtlType != ODT_MENU) return;
    const OwnerDrawMenuEntry* entry = reinterpret_cast<const OwnerDrawMenuEntry*>(mis->itemData);
    const std::wstring text = entry ? entry->text : L"";
    const std::wstring subtitle = entry ? entry->subtitle : L"";
    const std::wstring left = menu_item_left_text(text);
    const std::wstring right = menu_item_right_text(text);
    HDC dc = GetDC(g.main ? g.main : nullptr);
    HFONT font = (entry && entry->top_level && g.menu_font)
        ? g.menu_font
        : (g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    HGDIOBJ old_font = SelectObject(dc, font);
    SIZE left_sz{};
    SIZE right_sz{};
    GetTextExtentPoint32W(dc, left.c_str(), static_cast<int>(left.size()), &left_sz);
    GetTextExtentPoint32W(dc, right.c_str(), static_cast<int>(right.size()), &right_sz);
    SIZE subtitle_sz{};
    if (!subtitle.empty()) GetTextExtentPoint32W(dc, subtitle.c_str(), static_cast<int>(subtitle.size()), &subtitle_sz);
    SelectObject(dc, old_font);
    ReleaseDC(g.main ? g.main : nullptr, dc);
    if (entry && entry->top_level) {
        mis->itemWidth = left_sz.cx + 24;
        mis->itemHeight = max(34u, static_cast<UINT>(left_sz.cy + 18));
        return;
    }

    const UINT check_col = 28;
    const UINT left_pad = 10;
    const UINT right_pad = 12;
    const UINT gap = right.empty() ? 0 : 24;
    const UINT arrow_space = (entry && entry->popup) ? 18 : 0;
    const LONG content_width = entry && entry->recent_file
        ? min<LONG>(360, max(left_sz.cx, subtitle_sz.cx))
        : left_sz.cx;
    mis->itemWidth = check_col + left_pad + content_width + gap + right_sz.cx + arrow_space + right_pad;
    mis->itemHeight = entry && entry->recent_file
        ? max(42u, static_cast<UINT>(left_sz.cy + subtitle_sz.cy + 15))
        : max(24u, static_cast<UINT>(max(left_sz.cy, right_sz.cy) + 10));
}

void draw_owner_draw_menu(const DRAWITEMSTRUCT* dis) {
    if (!dis || dis->CtlType != ODT_MENU) return;
    auto blend = [](COLORREF a, COLORREF b, int weight_b) {
        weight_b = std::clamp(weight_b, 0, 255);
        const int weight_a = 255 - weight_b;
        return RGB(
            (GetRValue(a) * weight_a + GetRValue(b) * weight_b) / 255,
            (GetGValue(a) * weight_a + GetGValue(b) * weight_b) / 255,
            (GetBValue(a) * weight_a + GetBValue(b) * weight_b) / 255);
    };
    const OwnerDrawMenuEntry* entry = reinterpret_cast<const OwnerDrawMenuEntry*>(dis->itemData);
    const std::wstring text = entry ? entry->text : L"";
    const std::wstring subtitle = entry ? entry->subtitle : L"";
    const std::wstring left = menu_item_left_text(text);
    const std::wstring right = menu_item_right_text(text);
    RECT r = dis->rcItem;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const bool hot = selected || (dis->itemState & ODS_HOTLIGHT) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool checked = (dis->itemState & ODS_CHECKED) != 0;
    COLORREF bg = hot ? g_theme->btn_hover : g_theme->bg_toolbar;
    COLORREF text_col = disabled ? g_theme->text_secondary : g_theme->text_primary;
    HBRUSH bg_brush = CreateSolidBrush(bg);
    FillRect(dis->hDC, &r, bg_brush);
    DeleteObject(bg_brush);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text_col);
    HFONT font = (entry && entry->top_level && g.menu_font)
        ? g.menu_font
        : (g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    HGDIOBJ old_font = SelectObject(dis->hDC, font);
    if (entry && entry->top_level) {
        RECT text_rect = r;
        text_rect.bottom -= 1;
        if (hot && !disabled) {
            RECT hi = r;
            hi.left += 1;
            hi.right -= 1;
            hi.top += 1;
            hi.bottom -= 1;
            const int accent_weight = (g_theme == &kDarkTheme) ? 42 : 58;
            const COLORREF hi_fill = blend(g_theme->bg_toolbar, g_theme->accent, accent_weight);
            const COLORREF hi_border = blend(g_theme->btn_border, g_theme->accent, (g_theme == &kDarkTheme) ? 88 : 120);
            fill_rounded_rect(dis->hDC, hi, hi_fill, hi_border, 4);
            text_col = (g_theme == &kDarkTheme) ? RGB(255, 255, 255) : g_theme->accent_hover;
            SetTextColor(dis->hDC, text_col);

            RECT underline = { hi.left + 5, hi.bottom - 1, hi.right - 5, hi.bottom };
            HBRUSH underline_brush = CreateSolidBrush(g_theme->accent);
            FillRect(dis->hDC, &underline, underline_brush);
            DeleteObject(underline_brush);
        }
        DrawTextW(
            dis->hDC, left.c_str(), -1, &text_rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dis->hDC, old_font);
        return;
    }

    RECT check_rect = r;
    check_rect.right = check_rect.left + 28;
    RECT left_rect = r;
    left_rect.left = check_rect.right + 10;
    left_rect.right = r.right - 12;
    if (!right.empty()) left_rect.right -= 80;
    if (entry && entry->popup) left_rect.right -= 18;
    RECT right_rect = r;
    right_rect.left = max(left_rect.right + 8, r.right - 92);
    right_rect.right = r.right - ((entry && entry->popup) ? 24 : 12);
    if (entry && entry->recent_file) {
        RECT card = r;
        InflateRect(&card, -3, -2);
        if (hot) fill_rounded_rect(dis->hDC, card, mix_color(g_theme->btn_hover, g_theme->accent, 24),
                                   mix_color(g_theme->btn_border, g_theme->accent, 88), 5);
        left_rect.top += 4;
        left_rect.bottom = (r.top + r.bottom) / 2 + 2;
        DrawTextW(dis->hDC, left.c_str(), -1, &left_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        RECT subtitle_rect = left_rect;
        subtitle_rect.top = left_rect.bottom;
        subtitle_rect.bottom = r.bottom - 4;
        SetTextColor(dis->hDC, disabled ? g_theme->text_secondary : mix_color(g_theme->text_secondary, g_theme->bg_toolbar, 28));
        DrawTextW(dis->hDC, subtitle.c_str(), -1, &subtitle_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SetTextColor(dis->hDC, text_col);
    } else {
        DrawTextW(
            dis->hDC, left.c_str(), -1, &left_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if (!right.empty()) {
        DrawTextW(
            dis->hDC, right.c_str(), -1, &right_rect,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (checked) {
        RECT box = check_rect;
        box.left += 7;
        box.right = box.left + 14;
        box.top = r.top + max(0L, ((r.bottom - r.top) - 14) / 2);
        box.bottom = box.top + 14;
        HBRUSH accent_brush = CreateSolidBrush(g_theme->btn_active);
        FillRect(dis->hDC, &box, accent_brush);
        DeleteObject(accent_brush);

        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ old_pen = SelectObject(dis->hDC, pen);
        MoveToEx(dis->hDC, box.left + 3, box.top + 7, nullptr);
        LineTo(dis->hDC, box.left + 6, box.top + 10);
        LineTo(dis->hDC, box.right - 3, box.top + 4);
        SelectObject(dis->hDC, old_pen);
        DeleteObject(pen);
    }

    if (entry && entry->popup) {
        POINT pts[3] = {
            { r.right - 12, r.top + (r.bottom - r.top) / 2 - 4 },
            { r.right - 12, r.top + (r.bottom - r.top) / 2 + 4 },
            { r.right - 7,  r.top + (r.bottom - r.top) / 2 }
        };
        HBRUSH arrow_brush = CreateSolidBrush(text_col);
        HPEN arrow_pen = CreatePen(PS_SOLID, 1, text_col);
        HGDIOBJ old_brush = SelectObject(dis->hDC, arrow_brush);
        HGDIOBJ old_pen = SelectObject(dis->hDC, arrow_pen);
        Polygon(dis->hDC, pts, 3);
        SelectObject(dis->hDC, old_pen);
        SelectObject(dis->hDC, old_brush);
        DeleteObject(arrow_pen);
        DeleteObject(arrow_brush);
    }
    SelectObject(dis->hDC, old_font);
}

// Refresh every checkable menu item from the current app state. Cheap, so we
// just call it whenever a toggle changes (menu, toolbar, or accelerator).
void sync_menu() {
    if (g.mode_time) EnableWindow(g.mode_time, !g.ds.frequency_axis);
    const bool frf_available = has_data() && !g.ds.frequency_axis && g.ds.channel_count() >= 2;
    if (g.mode_frf) EnableWindow(g.mode_frf, frf_available);
    for (HWND button : g.buttons) {
        if (button && !frf_command_supported(GetDlgCtrlID(button))) EnableWindow(button, g.mode != AnalysisMode::FRF);
    }
    if (!g.menu) return;
    EnableMenuItem(g.menu, IDM_MODE_FRF, MF_BYCOMMAND | (frf_available ? MF_ENABLED : MF_GRAYED));
    for (int id : {IDC_PLAY, IDC_MEASURE, IDM_ADD_MARKER, IDM_ADD_VLINE, IDM_ADD_HLINE,
         IDM_ADD_VLINE_EXACT, IDM_ADD_HLINE_EXACT, IDM_CLEAR_POINTS, IDM_CLEAR_MARKERS, IDM_CLEAR_LINES, IDM_VISMOOTH})
        EnableMenuItem(g.menu, id, MF_BYCOMMAND |
            (g.mode == AnalysisMode::FRF && !frf_command_supported(id) ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(g.menu, IDM_MODE_TIME, MF_BYCOMMAND | (g.ds.frequency_axis ? MF_GRAYED : MF_ENABLED));
    auto chk = [&](UINT id, bool on) {
        CheckMenuItem(g.menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    };
    chk(IDM_MODE_TIME, (g.mode == AnalysisMode::Time));
    chk(IDM_MODE_FREQ, (g.mode == AnalysisMode::FFT));
    chk(IDM_MODE_FRF, g.mode == AnalysisMode::FRF);
    chk(IDM_VISMOOTH, g.visual_smooth);
    chk(IDM_VPAN, g.vertical_pan);
    chk(IDC_MEASURE, g.measure_mode);
    chk(IDC_CURSOR_TOOL, !g.measure_mode && !g.pending_marker && g.pending_line == 0);
    chk(IDM_ADD_MARKER, g.pending_marker);
    chk(IDM_ADD_VLINE, g.pending_line == 1);
    chk(IDM_ADD_HLINE, g.pending_line == 2);
    chk(IDC_AUTOY, g.mode == AnalysisMode::FRF ? g.frf.auto_y : g.auto_y);
    chk(IDM_THEME, g_theme == &kDarkTheme);
    chk(IDM_LANG_RU, g_str == &kRu);
    chk(IDM_LANG_EN, g_str == &kEn);
    std::wstring theme_label = menu_text(g_theme == &kDarkTheme ? g_str->theme_light : g_str->theme_dark, IDM_THEME);
    modify_menu_item_owner_draw(g.menu, IDM_THEME, theme_label);
    redraw_toolbar_buttons();
}

HMENU make_menu() {
    const bool english = (g_str == &kEn);
    const auto text = [english](const wchar_t* english_text, const wchar_t* russian_text) {
        return english ? english_text : russian_text;
    };
    const HMENU bar = CreateMenu();

    const HMENU file = CreatePopupMenu();
    const std::wstring open_text = menu_text(text(L"Open file…", L"Открыть файл…"), IDC_OPEN);
    const std::wstring save_png_text = menu_text(text(L"Save PNG…", L"Сохранить PNG…"), IDC_SAVEPNG);
    const std::wstring save_as_text = menu_text(text(L"Save as…", L"Сохранить как…"), IDC_SAVECSV);
    const std::wstring undo_text = menu_text(text(L"Undo", L"Отменить"), IDM_UNDO);
    const std::wstring redo_text = menu_text(text(L"Redo", L"Повторить"), IDM_REDO);
    append_menu_item_owner_draw(file, IDC_OPEN, open_text);
    const HMENU recent = CreatePopupMenu();
    if (g.recent_files.empty()) {
        append_menu_item_owner_draw(recent, IDM_RECENT_FILE_BASE, text(L"No recent files", L"Недавних файлов нет"));
        EnableMenuItem(recent, IDM_RECENT_FILE_BASE, MF_BYCOMMAND | MF_GRAYED);
    } else {
        for (std::size_t i = 0; i < g.recent_files.size() && i < kMaxRecentFiles; ++i) {
            const std::wstring& path = g.recent_files[i];
            const std::size_t slash = path.find_last_of(L"\\/");
            const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
            const std::wstring folder = slash == std::wstring::npos ? L"" : path.substr(0, slash);
            append_menu_item_owner_draw(recent, IDM_RECENT_FILE_BASE + static_cast<int>(i),
                                        name, folder, true);
        }
    }
    append_menu_popup_owner_draw(file, recent, text(L"Recent files", L"Недавние файлы"), false);
    const HMENU documents = CreatePopupMenu();
    const std::size_t document_count = open_document_count();
    if (document_count == 0) {
        append_menu_item_owner_draw(documents, IDM_OPEN_DOCUMENT_BASE,
                                    text(L"No open files", L"Нет открытых файлов"));
        EnableMenuItem(documents, IDM_OPEN_DOCUMENT_BASE, MF_BYCOMMAND | MF_GRAYED);
    } else {
        const std::size_t max_documents = static_cast<std::size_t>(IDM_CLOSE_DOCUMENT - IDM_OPEN_DOCUMENT_BASE);
        for (std::size_t i = 0; i < document_count && i < max_documents; ++i) {
            append_menu_item_owner_draw(documents, IDM_OPEN_DOCUMENT_BASE + static_cast<int>(i),
                                        open_document_label(i, true));
        }
        AppendMenuW(documents, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(documents, IDM_CLOSE_DOCUMENT,
                                    text(L"Close current file", L"Закрыть текущий файл"));
    }
    append_menu_popup_owner_draw(file, documents, text(L"Open files", L"Открытые файлы"), false);
    append_menu_item_owner_draw(file, IDC_SAVEPNG, save_png_text);
    append_menu_item_owner_draw(file, IDC_SAVECSV, save_as_text);
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(file, IDM_UNDO, undo_text);
    append_menu_item_owner_draw(file, IDM_REDO, redo_text);
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(file, IDM_EXIT, text(L"Exit\tAlt+F4", L"Выход\tAlt+F4"));
    append_menu_popup_owner_draw(bar, file, text(L"File", L"Файл"));

    const HMENU view = CreatePopupMenu();
    const std::wstring mode_time_text = menu_text(text(L"Time", L"Время"), IDM_MODE_TIME);
    const std::wstring mode_freq_text = menu_text(text(L"Hz (FFT)", L"Гц (FFT)"), IDM_MODE_FREQ);
    const std::wstring zoom_in_text = menu_text(text(L"Zoom in", L"Увеличить"), IDC_ZOOMIN);
    const std::wstring zoom_out_text = menu_text(text(L"Zoom out", L"Уменьшить"), IDC_ZOOMOUT);
    const std::wstring reset_text = menu_text(text(L"Reset view", L"Сбросить вид"), IDC_RESET);
    const std::wstring start_text = menu_text(text(L"Go to start", L"В начало"), IDC_GOTO_START);
    const std::wstring end_text = menu_text(text(L"Go to end", L"В конец"), IDC_GOTO_END);
    const std::wstring autoy_text = menu_text(text(L"Auto zoom", L"Автомасштабирование"), IDC_AUTOY);
    const std::wstring smooth_text = menu_text(text(L"Smoothing", L"Сглаживание"), IDM_VISMOOTH);
    const std::wstring vpan_text = menu_text(text(L"Vertical pan", L"Вертикальное панорамирование"), IDM_VPAN);
    const std::wstring play_text = menu_text(text(L"Play / Pause signal", L"Воспроизведение / пауза сигнала"), IDC_PLAY);
    const std::wstring theme_text = menu_text(text(L"Dark theme", L"Тёмная тема"), IDM_THEME);
    append_menu_item_owner_draw(view, IDM_MODE_TIME, mode_time_text);
    append_menu_item_owner_draw(view, IDM_MODE_FREQ, mode_freq_text);
    append_menu_item_owner_draw(view, IDM_MODE_FRF, text(L"FRF / Frequency response", L"FRF / АЧХ"));
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(view, IDC_ZOOMIN, zoom_in_text);
    append_menu_item_owner_draw(view, IDC_ZOOMOUT, zoom_out_text);
    append_menu_item_owner_draw(view, IDC_RESET, reset_text);
    append_menu_item_owner_draw(view, IDC_GOTO_START, start_text);
    append_menu_item_owner_draw(view, IDC_GOTO_END, end_text);
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(view, IDC_AUTOY, autoy_text);
    append_menu_item_owner_draw(view, IDM_VISMOOTH, smooth_text);
    append_menu_item_owner_draw(view, IDM_VPAN, vpan_text);
    append_menu_item_owner_draw(view, IDC_PLAY, play_text);
    append_menu_item_owner_draw(view, IDM_SPEED_CUSTOM, speed_menu_text());
    append_menu_item_owner_draw(view, IDM_THEME, theme_text);
    append_menu_popup_owner_draw(bar, view, text(L"View", L"Вид"));

    const HMENU tools = CreatePopupMenu();
    append_menu_item_owner_draw(tools, IDC_CURSOR_TOOL, text(L"Cursor", L"Курсор"));
    const std::wstring measure_text = menu_text(text(L"Points", L"Точки"), IDC_MEASURE);
    const std::wstring marker_text = menu_text(text(L"Marker", L"Маркер"), IDM_ADD_MARKER);
    const std::wstring vline_text = menu_text(text(L"Vertical line", L"Вертикальная линия"), IDM_ADD_VLINE);
    const std::wstring vline_exact_text = menu_text(text(L"Vertical line (exact)", L"Вертикальная линия (точно)"), IDM_ADD_VLINE_EXACT);
    const std::wstring hline_text = menu_text(text(L"Horizontal line", L"Горизонтальная линия"), IDM_ADD_HLINE);
    const std::wstring hline_exact_text = menu_text(text(L"Horizontal line (exact)", L"Горизонтальная линия (точно)"), IDM_ADD_HLINE_EXACT);
    append_menu_item_owner_draw(tools, IDC_MEASURE, measure_text);
    append_menu_item_owner_draw(tools, IDM_ADD_MARKER, marker_text);
    append_menu_item_owner_draw(tools, IDM_ADD_VLINE, vline_text);
    append_menu_item_owner_draw(tools, IDM_ADD_VLINE_EXACT, vline_exact_text);
    append_menu_item_owner_draw(tools, IDM_ADD_HLINE, hline_text);
    append_menu_item_owner_draw(tools, IDM_ADD_HLINE_EXACT, hline_exact_text);
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(tools, IDM_CLEAR_POINTS, text(L"Clear points", L"Очистить точки"));
    append_menu_item_owner_draw(tools, IDM_CLEAR_MARKERS, text(L"Clear markers", L"Очистить маркеры"));
    append_menu_item_owner_draw(tools, IDM_CLEAR_LINES, text(L"Clear lines", L"Очистить линии"));
    append_menu_popup_owner_draw(bar, tools, text(L"Tools", L"Инструменты"));

    const HMENU settings = CreatePopupMenu();
    append_menu_item_owner_draw(settings, IDM_SETTINGS, text(L"General settings", L"Общие настройки"));
    append_menu_popup_owner_draw(bar, settings, text(L"Settings", L"Настройки"));

    const HMENU help = CreatePopupMenu();
    const std::wstring hotkeys_text = menu_text(text(L"Keyboard shortcuts", L"Горячие клавиши"), IDM_HOTKEYS);
    append_menu_item_owner_draw(help, IDM_HOTKEYS, hotkeys_text);
    append_menu_item_owner_draw(help, IDM_ABOUT, text(L"About", L"О программе"));
    append_menu_popup_owner_draw(bar, help, text(L"Help", L"Справка"));
    return bar;
}

void rebuild_menu_bar() {
    if (!g.main) return;
    SetMenu(g.main, nullptr);
    if (g.menu) DestroyMenu(g.menu);
    g_menu_text_storage.clear();
    g.menu = make_menu();
    SetMenu(g.main, welcome_visible() ? nullptr : g.menu);
    MENUINFO mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    mi.hbrBack = CreateSolidBrush(g_theme->bg_toolbar);
    SetMenuInfo(g.menu, &mi);
    sync_menu();
    DrawMenuBar(g.main);
}

} // namespace gui
