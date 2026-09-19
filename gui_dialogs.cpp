// Dialogs: native viewer implementation.
#include "gui_dialogs.hpp"
#include "gui_analysis_source.hpp"
#include "gui_frf_render.hpp"
#include "gui_settings_window.hpp"
#include "gui_controls.hpp"
#include "gui_ids.hpp"
#include "gui_processing.hpp"
#include "gui_render_data.hpp"
#include "gui_settings.hpp"
#include "gui_side_panel.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"

namespace gui {

NumericPromptState g_numeric_prompt;

RangePromptState g_range_prompt;

InfoPromptState g_info_prompt;

ExportPromptState g_export_prompt;

int prompt_button_width(HDC dc, const wchar_t* text, int min_width) {
    SIZE sz{};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &sz);
    return std::max(min_width, static_cast<int>(sz.cx) + 28);
}

void draw_prompt_surface(HWND hwnd, HDC dc) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    HBRUSH outer = CreateSolidBrush(g_theme->bg_main);
    FillRect(dc, &rc, outer);
    DeleteObject(outer);

    RECT card = rc;
    InflateRect(&card, -2, -2);
    fill_rounded_rect(dc, card, g_theme->bg_panel, g_theme->separator, 12);
}

LRESULT CALLBACK InfoPromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HDC dc = GetDC(hwnd);
            HGDIOBJ old_font = SelectObject(dc, font);
            RECT client{};
            GetClientRect(hwnd, &client);
            const int client_w = std::max(380, static_cast<int>(client.right - client.left));
            const int button_w = prompt_button_width(dc, g_info_prompt.ok_text.c_str(), 104);
            const int button_x = std::max(16, (client_w - button_w) / 2);
            const int button_y = 104;
            g_info_prompt.ok_button = CreateWindowExW(
                0, L"BUTTON", g_info_prompt.ok_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                button_x, button_y, button_w, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            if (g_info_prompt.ok_button) {
                SendMessageW(g_info_prompt.ok_button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                SetFocus(g_info_prompt.ok_button);
            }
            SelectObject(dc, old_font);
            ReleaseDC(hwnd, dc);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wp);
            draw_prompt_surface(hwnd, dc);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            draw_prompt_surface(hwnd, dc);

            RECT client{};
            GetClientRect(hwnd, &client);
            const int icon_x = 26;
            const int icon_y = 42;
            const int icon_size = 32;
            HICON icon = LoadIconW(nullptr, g_info_prompt.error ? IDI_ERROR : IDI_INFORMATION);
            if (icon) {
                DrawIconEx(dc, icon_x, icon_y, icon, icon_size, icon_size, 0, nullptr, DI_NORMAL);
            }

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ old_font = SelectObject(dc, font);
            RECT text_rc = { icon_x + icon_size + 14, 38, client.right - 20, 96 };
            DrawTextW(dc, g_info_prompt.message.c_str(), -1, &text_rc,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old_font);

            EndPaint(hwnd, &ps);
            return 1;
        }
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis || !dis->hwndItem) break;
            if (GetDlgCtrlID(dis->hwndItem) == IDOK) {
                wchar_t txt[64]{};
                GetWindowTextW(dis->hwndItem, txt, 64);
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt,
                                           (dis->itemState & ODS_SELECTED) != 0,
                                           true, false);
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            g_info_prompt.done = true;
            g_info_prompt.wnd = nullptr;
            g_info_prompt.ok_button = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void show_styled_info_prompt(HWND owner, const wchar_t* title, const wchar_t* message, bool error) {
    if (!owner || !IsWindow(owner)) owner = g.main;
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = InfoPromptProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"LvmInfoPrompt";
        atom = RegisterClassExW(&wc);
    }

    g_info_prompt.done = false;
    g_info_prompt.error = error;
    g_info_prompt.title = title ? title : L"";
    g_info_prompt.message = message ? message : L"";
    g_info_prompt.ok_text = (g_str == &kEn) ? L"OK" : L"ОК";
    g_info_prompt.wnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"LvmInfoPrompt",
        g_info_prompt.title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 176,
        owner, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE)),
        nullptr);
    if (!g_info_prompt.wnd) return;

    RECT mr{}, wr{};
    GetWindowRect(owner, &mr);
    GetWindowRect(g_info_prompt.wnd, &wr);
    SetWindowPos(
        g_info_prompt.wnd, HWND_TOP,
        mr.left + ((mr.right - mr.left) - (wr.right - wr.left)) / 2,
        mr.top + ((mr.bottom - mr.top) - (wr.bottom - wr.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    EnableWindow(owner, FALSE);
    if (g_info_prompt.ok_button) SetFocus(g_info_prompt.ok_button);

    MSG msg;
    while (!g_info_prompt.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_info_prompt.wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

double normalize_prompt_bound(double value) {
    return (std::isfinite(value) && std::fabs(value) < 1e-12) ? 0.0 : value;
}

const wchar_t* speed_prompt_title_text() {
    return (g_str == &kEn) ? L"Playback speed" : L"Скорость воспроизведения";
}

const wchar_t* speed_prompt_label_text() {
    return (g_str == &kEn)
        ? L"Enter a playback speed multiplier:"
        : L"Введите множитель скорости воспроизведения:";
}

const wchar_t* speed_prompt_apply_text() {
    return (g_str == &kEn) ? L"Apply" : L"Применить";
}

const wchar_t* speed_prompt_cancel_text() {
    return (g_str == &kEn) ? L"Cancel" : L"Отмена";
}

const wchar_t* speed_prompt_invalid_text() {
    return (g_str == &kEn)
        ? L"Enter a positive number, for example 0.5, 1, or 2.75."
        : L"Введите положительное число, например 0.5, 1 или 2.75.";
}

const wchar_t* guide_prompt_title_text(bool vertical) {
    if (g_str == &kEn) return vertical ? L"Vertical guide line" : L"Horizontal guide line";
    return vertical ? L"Вертикальная линия" : L"Горизонтальная линия";
}

const wchar_t* guide_prompt_label_text(bool vertical) {
    if (g.mode == AnalysisMode::FRF) {
        if (g_str == &kEn) return vertical ? L"Enter frequency, Hz:" : L"Enter linear KD = |H|:";
        return vertical ? L"Введите частоту, Гц:" : L"Введите КД = |H| в натуральной величине:";
    }
    if (g.mode == AnalysisMode::FFT) {
        if (g_str == &kEn) return vertical ? L"Enter frequency, Hz:" : L"Enter amplitude:";
        return vertical ? L"Введите частоту, Гц:" : L"Введите амплитуду:";
    }
    if (g_str == &kEn) {
        return vertical ? L"Enter the exact X-axis value:" : L"Enter the exact Y-axis value:";
    }
    return vertical ? L"Введите точное значение по оси X:" : L"Введите точное значение по оси Y:";
}

const wchar_t* guide_prompt_apply_text() {
    return (g_str == &kEn) ? L"Add" : L"Добавить";
}

const wchar_t* guide_prompt_cancel_text() {
    return speed_prompt_cancel_text();
}

const wchar_t* guide_prompt_invalid_text() {
    return (g_str == &kEn)
        ? L"Enter a finite number, for example -1, 0, or 2.75."
        : L"Введите конечное число, например -1, 0 или 2.75.";
}

LRESULT CALLBACK NumericPromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HDC dc = GetDC(hwnd);
            HGDIOBJ old_font = SelectObject(dc, font);
            CreateWindowExW(0, L"STATIC", g_numeric_prompt.label.c_str(),
                            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                            18, 16, 340, 44, hwnd, nullptr,
                            reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            g_numeric_prompt.edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", format_edit_number(g_numeric_prompt.value).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                18, 66, 340, 24, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_PROMPT_EDIT)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            const int ok_w = prompt_button_width(dc, g_numeric_prompt.apply_text.c_str(), 110);
            const int cancel_w = prompt_button_width(dc, g_numeric_prompt.cancel_text.c_str(), 110);
            const int button_gap = 10;
            const int total_w = ok_w + cancel_w + button_gap;
            int button_x = std::max(16, (384 - total_w) / 2);
            HWND ok = CreateWindowExW(
                0, L"BUTTON", g_numeric_prompt.apply_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                button_x, 104, ok_w, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_PROMPT_OK)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND cancel = CreateWindowExW(
                0, L"BUTTON", g_numeric_prompt.cancel_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                button_x + ok_w + button_gap, 104, cancel_w, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_PROMPT_CANCEL)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            if (ok) SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (cancel) SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (g_numeric_prompt.edit) SendMessageW(g_numeric_prompt.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SelectObject(dc, old_font);
            ReleaseDC(hwnd, dc);
            HWND label = GetWindow(hwnd, GW_CHILD);
            if (label) SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_SPEED_PROMPT_OK: {
                    double value = 0.0;
                    wchar_t buf[128]{};
                    if (g_numeric_prompt.edit) GetWindowTextW(g_numeric_prompt.edit, buf, 128);
                    const bool valid = parse_wide_double_text(buf, value) &&
                                       std::isfinite(value) &&
                                       (!g_numeric_prompt.positive_only || value > 0.0);
                    if (!valid) {
                        MessageBoxW(hwnd, g_numeric_prompt.invalid_text.c_str(), g_numeric_prompt.title.c_str(), MB_OK | MB_ICONWARNING);
                        if (g_numeric_prompt.edit) SetFocus(g_numeric_prompt.edit);
                        return 0;
                    }
                    g_numeric_prompt.value = value;
                    g_numeric_prompt.accepted = true;
                    DestroyWindow(hwnd);
                    return 0;
                }
                case IDC_SPEED_PROMPT_CANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wp);
            draw_prompt_surface(hwnd, dc);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            draw_prompt_surface(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis || !dis->hwndItem) break;
            const int ctl_id = GetDlgCtrlID(dis->hwndItem);
            wchar_t txt[128]{};
            GetWindowTextW(dis->hwndItem, txt, 128);
            const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            if (ctl_id == IDC_SPEED_PROMPT_OK) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, true, false);
                return TRUE;
            }
            if (ctl_id == IDC_SPEED_PROMPT_CANCEL) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, false, false);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_plot);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
        }
        case WM_DESTROY:
            g_numeric_prompt.done = true;
            g_numeric_prompt.wnd = nullptr;
            g_numeric_prompt.edit = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK RangePromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HDC dc = GetDC(hwnd);
            HGDIOBJ old_font = SelectObject(dc, font);
            RECT client{};
            GetClientRect(hwnd, &client);
            const int client_w = std::max(0, static_cast<int>(client.right - client.left));
            const int content_w = std::max(360, client_w - 36);
            const int info_h = 42;
            const int label_h = 18;
            const int edit_h = 24;
            const int start_label_y = 16 + info_h + 10;
            const int start_edit_y = start_label_y + label_h + 4;
            const int end_label_y = start_edit_y + edit_h + 10;
            const int end_edit_y = end_label_y + label_h + 4;
            auto mkstatic = [&](const std::wstring& text, int x, int y, int w, int h) {
                HWND ctl = CreateWindowExW(0, L"STATIC", text.c_str(),
                                           WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                           x, y, w, h, hwnd, nullptr,
                                           reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
                if (ctl) SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return ctl;
            };
            mkstatic(g_range_prompt.info_label, 18, 16, content_w, info_h);
            mkstatic(g_range_prompt.start_label, 18, start_label_y, content_w, label_h);
            g_range_prompt.start_edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", format_edit_number(g_range_prompt.start_value).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                18, start_edit_y, content_w, edit_h, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RANGE_PROMPT_START_EDIT)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            mkstatic(g_range_prompt.end_label, 18, end_label_y, content_w, label_h);
            g_range_prompt.end_edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", format_edit_number(g_range_prompt.end_value).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                18, end_edit_y, content_w, edit_h, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RANGE_PROMPT_END_EDIT)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            const wchar_t* autofill_text = (g_str == &kEn) ? L"Min / Max" : L"Мин / Макс";
            const int autofill_w = prompt_button_width(dc, autofill_text, 98);
            const int apply_w = prompt_button_width(dc, g_range_prompt.apply_text.c_str(), 116);
            const int cancel_w = prompt_button_width(dc, g_range_prompt.cancel_text.c_str(), 88);
            const int button_gap = 10;
            const int total_w = autofill_w + apply_w + cancel_w + button_gap * 2;
            int button_x = std::max(16, (client_w - total_w) / 2);
            if (button_x + total_w > client_w - 16) {
                button_x = std::max(16, client_w - 16 - total_w);
            }
            HWND autofill = CreateWindowExW(
                0, L"BUTTON", autofill_text,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                button_x, 178, autofill_w, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RANGE_PROMPT_AUTOFILL)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND ok = CreateWindowExW(
                0, L"BUTTON", g_range_prompt.apply_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                button_x + autofill_w + button_gap, 178, apply_w, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RANGE_PROMPT_OK)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND cancel = CreateWindowExW(
                0, L"BUTTON", g_range_prompt.cancel_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                button_x + autofill_w + button_gap + apply_w + button_gap, 178, cancel_w, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RANGE_PROMPT_CANCEL)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            if (autofill) SendMessageW(autofill, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (g_range_prompt.start_edit) SendMessageW(g_range_prompt.start_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (g_range_prompt.end_edit) SendMessageW(g_range_prompt.end_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (ok) SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (cancel) SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SelectObject(dc, old_font);
            ReleaseDC(hwnd, dc);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_RANGE_PROMPT_AUTOFILL: {
                    g_range_prompt.start_value = g_range_prompt.min_value;
                    g_range_prompt.end_value = g_range_prompt.max_value;
                    if (g_range_prompt.start_edit) {
                        SetWindowTextW(g_range_prompt.start_edit, format_edit_number(g_range_prompt.start_value).c_str());
                        SendMessageW(g_range_prompt.start_edit, EM_SETSEL, 0, -1);
                        SetFocus(g_range_prompt.start_edit);
                    }
                    if (g_range_prompt.end_edit) {
                        SetWindowTextW(g_range_prompt.end_edit, format_edit_number(g_range_prompt.end_value).c_str());
                    }
                    return 0;
                }
                case IDC_RANGE_PROMPT_OK: {
                    double start = 0.0, end = 0.0;
                    wchar_t buf_start[128]{}, buf_end[128]{};
                    if (g_range_prompt.start_edit) GetWindowTextW(g_range_prompt.start_edit, buf_start, 128);
                    if (g_range_prompt.end_edit) GetWindowTextW(g_range_prompt.end_edit, buf_end, 128);
                    const bool start_valid = parse_wide_double_text(buf_start, start) &&
                                             std::isfinite(start) &&
                                             start >= g_range_prompt.min_value &&
                                             start < g_range_prompt.max_value;
                    if (!start_valid) {
                        MessageBoxW(hwnd, g_range_prompt.invalid_start_text.c_str(), g_range_prompt.title.c_str(), MB_OK | MB_ICONWARNING);
                        if (g_range_prompt.start_edit) SetFocus(g_range_prompt.start_edit);
                        return 0;
                    }
                    const bool end_valid = parse_wide_double_text(buf_end, end) &&
                                           std::isfinite(end) &&
                                           end > start &&
                                           end <= g_range_prompt.max_value;
                    if (!end_valid) {
                        MessageBoxW(hwnd, g_range_prompt.invalid_end_text.c_str(), g_range_prompt.title.c_str(), MB_OK | MB_ICONWARNING);
                        if (g_range_prompt.end_edit) SetFocus(g_range_prompt.end_edit);
                        return 0;
                    }
                    g_range_prompt.start_value = start;
                    g_range_prompt.end_value = end;
                    g_range_prompt.accepted = true;
                    DestroyWindow(hwnd);
                    return 0;
                }
                case IDC_RANGE_PROMPT_CANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wp);
            draw_prompt_surface(hwnd, dc);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            draw_prompt_surface(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis || !dis->hwndItem) break;
            const int ctl_id = GetDlgCtrlID(dis->hwndItem);
            wchar_t txt[256]{};
            GetWindowTextW(dis->hwndItem, txt, 256);
            const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            if (ctl_id == IDC_RANGE_PROMPT_OK) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, true, false);
                return TRUE;
            }
            if (ctl_id == IDC_RANGE_PROMPT_AUTOFILL) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, false, true);
                return TRUE;
            }
            if (ctl_id == IDC_RANGE_PROMPT_CANCEL) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, false, false);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_plot);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
        }
        case WM_DESTROY:
            g_range_prompt.done = true;
            g_range_prompt.wnd = nullptr;
            g_range_prompt.start_edit = nullptr;
            g_range_prompt.end_edit = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

template <typename T>
int combo_index_for_value(HWND combo, T value) {
    if (!combo) return -1;
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        const auto item = static_cast<T>(SendMessageW(combo, CB_GETITEMDATA, i, 0));
        if (item == value) return i;
    }
    return -1;
}

ExportFileFormat export_prompt_selected_format() {
    if (!g_export_prompt.format_combo) return ExportFileFormat::Csv;
    const int sel = static_cast<int>(SendMessageW(g_export_prompt.format_combo, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return ExportFileFormat::Csv;
    const int value = static_cast<int>(SendMessageW(g_export_prompt.format_combo, CB_GETITEMDATA, sel, 0));
    switch (value) {
        case static_cast<int>(ExportFileFormat::Txt): return ExportFileFormat::Txt;
        case static_cast<int>(ExportFileFormat::Csv): return ExportFileFormat::Csv;
        case static_cast<int>(ExportFileFormat::Lvm): return ExportFileFormat::Lvm;
    }
    return ExportFileFormat::Csv;
}

ExportSaveMode export_prompt_selected_save_mode() {
    if (!g_export_prompt.save_mode_combo) return ExportSaveMode::OriginalData;
    const int sel = static_cast<int>(SendMessageW(g_export_prompt.save_mode_combo, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return ExportSaveMode::OriginalData;
    const int value = static_cast<int>(SendMessageW(g_export_prompt.save_mode_combo, CB_GETITEMDATA, sel, 0));
    switch (value) {
        case static_cast<int>(ExportSaveMode::AppliedSettings): return ExportSaveMode::AppliedSettings;
        case static_cast<int>(ExportSaveMode::Project): return ExportSaveMode::Project;
    }
    return ExportSaveMode::OriginalData;
}

ExportRangeMode export_prompt_selected_range() {
    if (!g_export_prompt.range_combo) return ExportRangeMode::Visible;
    const int sel = static_cast<int>(SendMessageW(g_export_prompt.range_combo, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return ExportRangeMode::Visible;
    const int value = static_cast<int>(SendMessageW(g_export_prompt.range_combo, CB_GETITEMDATA, sel, 0));
    switch (value) {
        case static_cast<int>(ExportRangeMode::Selected): return ExportRangeMode::Selected;
        case static_cast<int>(ExportRangeMode::Visible): return ExportRangeMode::Visible;
        case static_cast<int>(ExportRangeMode::Whole): return ExportRangeMode::Whole;
    }
    return ExportRangeMode::Visible;
}

void sync_export_prompt_state_from_controls() {
    g_export_prompt.save_mode = export_prompt_selected_save_mode();
    g_export_prompt.selected_format = export_prompt_selected_format();
    g_export_prompt.selected_range = export_prompt_selected_range();
    if (g_export_prompt.include_channel_names_check) {
        g_export_prompt.include_channel_names =
            SendMessageW(g_export_prompt.include_channel_names_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    if (g_export_prompt.include_hidden_channels_check) {
        g_export_prompt.include_hidden_channels =
            SendMessageW(g_export_prompt.include_hidden_channels_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    if (g_export_prompt.include_points_check) {
        g_export_prompt.include_points =
            SendMessageW(g_export_prompt.include_points_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    if (g_export_prompt.include_markers_check) {
        g_export_prompt.include_markers =
            SendMessageW(g_export_prompt.include_markers_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    if (g_export_prompt.include_guides_check) {
        g_export_prompt.include_guides =
            SendMessageW(g_export_prompt.include_guides_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    if (g_export_prompt.include_formulas_check) {
        g_export_prompt.include_formulas =
            SendMessageW(g_export_prompt.include_formulas_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    if (g_export_prompt.include_filter_check) {
        g_export_prompt.include_filter_settings =
            SendMessageW(g_export_prompt.include_filter_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    if (g_export_prompt.include_graph_settings_check) {
        g_export_prompt.include_graph_settings =
            SendMessageW(g_export_prompt.include_graph_settings_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
}

void update_export_prompt_controls() {
    const bool applied = g_export_prompt.save_mode == ExportSaveMode::AppliedSettings;
    const bool data_export = g_export_prompt.save_mode != ExportSaveMode::Project;
    const std::initializer_list<HWND> settings_controls = {
        g_export_prompt.include_channel_names_check, g_export_prompt.include_hidden_channels_check,
        g_export_prompt.include_points_check, g_export_prompt.include_markers_check,
        g_export_prompt.include_guides_check, g_export_prompt.include_formulas_check,
        g_export_prompt.include_filter_check, g_export_prompt.include_graph_settings_check,
    };
    for (HWND control : settings_controls) if (control) EnableWindow(control, applied);
    if (g_export_prompt.range_combo) EnableWindow(g_export_prompt.range_combo, data_export);
    if (g_export_prompt.format_combo) EnableWindow(g_export_prompt.format_combo, data_export);
}

LRESULT CALLBACK ExportPromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HDC dc = GetDC(hwnd);
            HGDIOBJ old_font = SelectObject(dc, font);
            RECT client{};
            GetClientRect(hwnd, &client);
            const int client_w = std::max(0, static_cast<int>(client.right - client.left));
            const int content_w = std::max(680, client_w - 36);
            const int section_h = 20;
            const int option_h = 24;
            const int col_gap = 16;
            const int col_w = (content_w - col_gap) / 2;
            const int label_w = 88;
            const int combo_x = 18 + label_w;
            const int combo_w = content_w - label_w - 6;
            auto mkstatic = [&](const std::wstring& text, int x, int y, int w, int h) {
                HWND ctl = CreateWindowExW(0, L"STATIC", text.c_str(),
                                           WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                           x, y, w, h, hwnd, nullptr,
                                           reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
                if (ctl) SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return ctl;
            };
            auto mkcombo = [&](int x, int y, int w, int h, int id) {
                HWND ctl = CreateWindowExW(
                    0, L"COMBOBOX", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
                    CBS_HASSTRINGS | CBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER,
                    x, y, w, h, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                    reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
                if (ctl) SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                install_themed_combo(ctl);
                return ctl;
            };
            auto add_combo_item = [&](HWND combo, const std::wstring& text, int value) {
                int idx = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
                if (idx >= 0) {
                    SendMessageW(combo, CB_SETITEMDATA, idx, static_cast<LPARAM>(value));
                }
            };
            auto mkcheck = [&](const std::wstring& text, int x, int y, int w, int h, int id) {
                HWND ctl = CreateWindowExW(
                    0, L"BUTTON", text.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_MULTILINE,
                    x, y, w, h, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                    reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
                if (ctl) SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return ctl;
            };
            mkstatic(g_export_prompt.save_mode_label_text, 18, 18, label_w, section_h);
            g_export_prompt.save_mode_combo = mkcombo(combo_x, 14, combo_w, 260, IDC_EXPORT_SAVE_MODE);
            if (g_export_prompt.save_mode_combo) {
                add_combo_item(g_export_prompt.save_mode_combo, g_export_prompt.save_project_text, static_cast<int>(ExportSaveMode::Project));
                add_combo_item(g_export_prompt.save_mode_combo, g_export_prompt.save_applied_text, static_cast<int>(ExportSaveMode::AppliedSettings));
                add_combo_item(g_export_prompt.save_mode_combo, g_export_prompt.save_original_text, static_cast<int>(ExportSaveMode::OriginalData));
                SendMessageW(g_export_prompt.save_mode_combo, CB_SETMINVISIBLE, 4, 0);
                SendMessageW(g_export_prompt.save_mode_combo, CB_SETDROPPEDWIDTH, std::max(300, combo_w), 0);
            }
            mkstatic(g_export_prompt.format_label_text, 18, 52, label_w, section_h);
            g_export_prompt.format_combo = mkcombo(combo_x, 48, combo_w, 260, IDC_EXPORT_SCOPE_CURRENT);
            if (g_export_prompt.format_combo) {
                add_combo_item(g_export_prompt.format_combo, g_export_prompt.format_txt_text, static_cast<int>(ExportFileFormat::Txt));
                add_combo_item(g_export_prompt.format_combo, g_export_prompt.format_csv_text, static_cast<int>(ExportFileFormat::Csv));
                add_combo_item(g_export_prompt.format_combo, g_export_prompt.format_lvm_text, static_cast<int>(ExportFileFormat::Lvm));
                SendMessageW(g_export_prompt.format_combo, CB_SETMINVISIBLE, 4, 0);
                SendMessageW(g_export_prompt.format_combo, CB_SETDROPPEDWIDTH, std::max(220, combo_w), 0);
            }

            mkstatic(g_export_prompt.range_label_text, 18, 86, label_w, section_h);
            g_export_prompt.range_combo = mkcombo(combo_x, 82, combo_w, 260, IDC_EXPORT_SCOPE_FRAGMENT);
            if (g_export_prompt.range_combo) {
                add_combo_item(g_export_prompt.range_combo, g_export_prompt.range_selected_text, static_cast<int>(ExportRangeMode::Selected));
                add_combo_item(g_export_prompt.range_combo, g_export_prompt.range_visible_text, static_cast<int>(ExportRangeMode::Visible));
                add_combo_item(g_export_prompt.range_combo, g_export_prompt.range_whole_text, static_cast<int>(ExportRangeMode::Whole));
                SendMessageW(g_export_prompt.range_combo, CB_SETMINVISIBLE, 4, 0);
                SendMessageW(g_export_prompt.range_combo, CB_SETDROPPEDWIDTH, std::max(220, combo_w), 0);
            }

            mkstatic(g_str == &kEn ? L"Additional data:" : L"Дополнительные данные:", 18, 122, content_w, section_h);
            g_export_prompt.include_channel_names_check = mkcheck(g_export_prompt.channel_names_text, 18, 146, col_w, option_h, IDC_EXPORT_INCLUDE_CHANNEL_NAMES);
            g_export_prompt.include_formulas_check = mkcheck(g_export_prompt.formulas_text, 18 + col_w + col_gap, 146, col_w, option_h, IDC_EXPORT_INCLUDE_FORMULAS);
            g_export_prompt.include_points_check = mkcheck(g_export_prompt.points_text, 18, 174, col_w, option_h, IDC_EXPORT_INCLUDE_POINTS);
            g_export_prompt.include_filter_check = mkcheck(g_export_prompt.filter_text, 18 + col_w + col_gap, 174, col_w, option_h, IDC_EXPORT_INCLUDE_FILTER);
            g_export_prompt.include_markers_check = mkcheck(g_export_prompt.markers_text, 18, 202, col_w, option_h, IDC_EXPORT_INCLUDE_MARKERS);
            g_export_prompt.include_graph_settings_check = mkcheck(g_export_prompt.graph_settings_text, 18 + col_w + col_gap, 202, col_w, option_h, IDC_EXPORT_INCLUDE_GRAPH_SETTINGS);
            g_export_prompt.include_guides_check = mkcheck(g_export_prompt.guides_text, 18, 230, col_w, option_h, IDC_EXPORT_INCLUDE_GUIDES);
            g_export_prompt.include_hidden_channels_check = mkcheck(g_export_prompt.hidden_channels_text, 18 + col_w + col_gap, 230, col_w, option_h, IDC_EXPORT_INCLUDE_HIDDEN_CHANNELS);

            const int continue_w = prompt_button_width(dc, g_export_prompt.continue_text.c_str(), 130);
            const int cancel_w = prompt_button_width(dc, g_export_prompt.cancel_text.c_str(), 100);
            const int button_gap = 10;
            const int total_w = continue_w + cancel_w + button_gap;
            int button_x = std::max(16, (client_w - total_w) / 2);
            if (button_x + total_w > client_w - 16) {
                button_x = std::max(16, client_w - 16 - total_w);
            }
            const int button_y = 282;
            HWND ok = CreateWindowExW(
                0, L"BUTTON", g_export_prompt.continue_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                button_x, button_y, continue_w, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND cancel = CreateWindowExW(
                0, L"BUTTON", g_export_prompt.cancel_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                button_x + continue_w + button_gap, button_y, cancel_w, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            auto set_check = [&](HWND ctl, bool on) {
                set_toggle_checked(ctl, on);
            };
            set_check(g_export_prompt.include_channel_names_check, g_export_prompt.include_channel_names);
            set_check(g_export_prompt.include_points_check, g_export_prompt.include_points);
            set_check(g_export_prompt.include_markers_check, g_export_prompt.include_markers);
            set_check(g_export_prompt.include_guides_check, g_export_prompt.include_guides);
            set_check(g_export_prompt.include_formulas_check, g_export_prompt.include_formulas);
            set_check(g_export_prompt.include_filter_check, g_export_prompt.include_filter_settings);
            set_check(g_export_prompt.include_graph_settings_check, g_export_prompt.include_graph_settings);
            set_check(g_export_prompt.include_hidden_channels_check, g_export_prompt.include_hidden_channels);
            if (g_export_prompt.format_combo) {
                const int fmt_index = combo_index_for_value(g_export_prompt.format_combo, g_export_prompt.selected_format);
                SendMessageW(g_export_prompt.format_combo, CB_SETCURSEL, fmt_index >= 0 ? fmt_index : 1, 0);
            }
            if (g_export_prompt.save_mode_combo) {
                const int mode_index = combo_index_for_value(g_export_prompt.save_mode_combo, g_export_prompt.save_mode);
                SendMessageW(g_export_prompt.save_mode_combo, CB_SETCURSEL, mode_index >= 0 ? mode_index : 0, 0);
            }
            if (g_export_prompt.range_combo) {
                const int range_index = combo_index_for_value(g_export_prompt.range_combo, g_export_prompt.selected_range);
                SendMessageW(g_export_prompt.range_combo, CB_SETCURSEL, range_index >= 0 ? range_index : 1, 0);
            }
            if (ok) SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (cancel) SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (g_export_prompt.format_combo) {
                SetFocus(g_export_prompt.format_combo);
            }
            sync_export_prompt_state_from_controls();
            update_export_prompt_controls();
            SelectObject(dc, old_font);
            ReleaseDC(hwnd, dc);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_EXPORT_SAVE_MODE:
                    if (HIWORD(wp) == CBN_DROPDOWN) {
                        expand_combo_dropdown(GetDlgItem(hwnd, LOWORD(wp)));
                        return 0;
                    }
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        sync_export_prompt_state_from_controls();
                        update_export_prompt_controls();
                        return 0;
                    }
                    break;
                case IDC_EXPORT_SCOPE_CURRENT:
                    if (HIWORD(wp) == CBN_DROPDOWN) {
                        expand_combo_dropdown(GetDlgItem(hwnd, LOWORD(wp)));
                        return 0;
                    }
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        sync_export_prompt_state_from_controls();
                        return 0;
                    }
                    break;
                case IDC_EXPORT_SCOPE_FRAGMENT:
                    if (HIWORD(wp) == CBN_DROPDOWN) {
                        expand_combo_dropdown(GetDlgItem(hwnd, LOWORD(wp)));
                        return 0;
                    }
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        sync_export_prompt_state_from_controls();
                        return 0;
                    }
                    break;
                case IDC_EXPORT_INCLUDE_CHANNEL_NAMES:
                case IDC_EXPORT_INCLUDE_POINTS:
                case IDC_EXPORT_INCLUDE_MARKERS:
                case IDC_EXPORT_INCLUDE_GUIDES:
                case IDC_EXPORT_INCLUDE_FORMULAS:
                case IDC_EXPORT_INCLUDE_FILTER:
                case IDC_EXPORT_INCLUDE_GRAPH_SETTINGS:
                case IDC_EXPORT_INCLUDE_HIDDEN_CHANNELS:
                    toggle_checked_state(GetDlgItem(hwnd, LOWORD(wp)));
                    sync_export_prompt_state_from_controls();
                    return 0;
                case IDOK:
                    sync_export_prompt_state_from_controls();
                    if (g_export_prompt.save_mode != ExportSaveMode::Project &&
                        g_export_prompt.selected_format == ExportFileFormat::Lvm && (g.mode == AnalysisMode::FFT)) {
                        MessageBoxW(hwnd,
                                    g_str == &kEn ? L"LVM export is available only in Time mode."
                                                   : L"Экспорт LVM доступен только в режиме Время.",
                                    g_str->msg_error_title, MB_ICONINFORMATION);
                        if (g_export_prompt.format_combo) SetFocus(g_export_prompt.format_combo);
                        return 0;
                    }
                    g_export_prompt.accepted = true;
                    DestroyWindow(hwnd);
                    return 0;
                case IDCANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wp);
            draw_prompt_surface(hwnd, dc);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            draw_prompt_surface(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 1;
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (!mis) break;
            if (mis->CtlType == ODT_COMBOBOX &&
                (mis->CtlID == IDC_EXPORT_SAVE_MODE || mis->CtlID == IDC_EXPORT_SCOPE_CURRENT || mis->CtlID == IDC_EXPORT_SCOPE_FRAGMENT)) {
                measure_settings_combo_item(mis);
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
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis || !dis->hwndItem) break;
            if (dis->CtlType == ODT_COMBOBOX &&
                (dis->CtlID == IDC_EXPORT_SAVE_MODE || dis->CtlID == IDC_EXPORT_SCOPE_CURRENT || dis->CtlID == IDC_EXPORT_SCOPE_FRAGMENT)) {
                draw_settings_combo_item(dis);
                return TRUE;
            }
            const int ctl_id = GetDlgCtrlID(dis->hwndItem);
            wchar_t txt[128]{};
            GetWindowTextW(dis->hwndItem, txt, 128);
            const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            if (ctl_id == IDOK) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, true, false);
                return TRUE;
            }
            if (ctl_id == IDCANCEL) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, false, false);
                return TRUE;
            }
            if (ctl_id == IDC_EXPORT_INCLUDE_CHANNEL_NAMES ||
                ctl_id == IDC_EXPORT_INCLUDE_POINTS ||
                ctl_id == IDC_EXPORT_INCLUDE_MARKERS ||
                ctl_id == IDC_EXPORT_INCLUDE_GUIDES ||
                ctl_id == IDC_EXPORT_INCLUDE_FORMULAS ||
                ctl_id == IDC_EXPORT_INCLUDE_FILTER ||
                ctl_id == IDC_EXPORT_INCLUDE_GRAPH_SETTINGS ||
                ctl_id == IDC_EXPORT_INCLUDE_HIDDEN_CHANNELS) {
                const bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
                draw_themed_check_control(dis->hDC, dis->rcItem, txt,
                    is_toggle_checked(dis->hwndItem), pressed, enabled, false, false, CLR_INVALID);
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            g_export_prompt.done = true;
            g_export_prompt.wnd = nullptr;
            g_export_prompt.save_mode_combo = nullptr;
            g_export_prompt.format_combo = nullptr;
            g_export_prompt.range_combo = nullptr;
            g_export_prompt.include_channel_names_check = nullptr;
            g_export_prompt.include_points_check = nullptr;
            g_export_prompt.include_markers_check = nullptr;
            g_export_prompt.include_guides_check = nullptr;
            g_export_prompt.include_formulas_check = nullptr;
            g_export_prompt.include_filter_check = nullptr;
            g_export_prompt.include_graph_settings_check = nullptr;
            g_export_prompt.include_hidden_channels_check = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool prompt_numeric_value(const wchar_t* title, const wchar_t* label,
                          const wchar_t* apply_text, const wchar_t* cancel_text,
                          const wchar_t* invalid_text, double initial_value,
                          bool positive_only, double& out_value) {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = NumericPromptProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"LvmNumericPrompt";
        atom = RegisterClassExW(&wc);
    }

    g_numeric_prompt.done = false;
    g_numeric_prompt.accepted = false;
    g_numeric_prompt.positive_only = positive_only;
    g_numeric_prompt.value = initial_value;
    g_numeric_prompt.title = title;
    g_numeric_prompt.label = label;
    g_numeric_prompt.apply_text = apply_text;
    g_numeric_prompt.cancel_text = cancel_text;
    g_numeric_prompt.invalid_text = invalid_text;
    g_numeric_prompt.wnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"LvmNumericPrompt",
        g_numeric_prompt.title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 384, 172,
        g.main, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE)),
        nullptr);
    if (!g_numeric_prompt.wnd) return false;

    RECT mr{}, wr{};
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_numeric_prompt.wnd, &wr);
    SetWindowPos(
        g_numeric_prompt.wnd, HWND_TOP,
        mr.left + ((mr.right - mr.left) - (wr.right - wr.left)) / 2,
        mr.top + ((mr.bottom - mr.top) - (wr.bottom - wr.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    EnableWindow(g.main, FALSE);
    if (g_numeric_prompt.edit) {
        SetFocus(g_numeric_prompt.edit);
        SendMessageW(g_numeric_prompt.edit, EM_SETSEL, 0, -1);
    }

    MSG msg;
    while (!g_numeric_prompt.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_numeric_prompt.wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(g.main, TRUE);
    SetForegroundWindow(g.main);
    if (!g_numeric_prompt.accepted) return false;
    out_value = g_numeric_prompt.value;
    return true;
}

bool prompt_light_mode_window(double range_start, double range_end, double& out_start, double& out_end) {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = RangePromptProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"LvmRangePrompt";
        atom = RegisterClassExW(&wc);
    }

    const bool en = (g_str == &kEn);
    g_range_prompt.done = false;
    g_range_prompt.accepted = false;
    range_start = normalize_prompt_bound(range_start);
    range_end = normalize_prompt_bound(range_end);
    if (!std::isfinite(range_start)) range_start = 0.0;
    if (!std::isfinite(range_end)) range_end = range_start + 10.0;
    if (range_end < range_start) std::swap(range_start, range_end);
    if (range_end <= range_start) range_end = range_start + 10.0;

    g_range_prompt.min_value = range_start;
    g_range_prompt.max_value = range_end;
    g_range_prompt.start_value = normalize_prompt_bound(std::clamp(g.light_mode_open_start, range_start, range_end));
    g_range_prompt.end_value = normalize_prompt_bound(std::clamp(g.light_mode_open_end, g_range_prompt.start_value + 1e-9, range_end));
    if (g_range_prompt.end_value <= g_range_prompt.start_value) {
        g_range_prompt.end_value = std::min(range_end, g_range_prompt.start_value + 10.0);
    }
    g_range_prompt.title = g_str->light_mode_range_title;
    g_range_prompt.start_label = g_str->light_mode_range_start;
    g_range_prompt.end_label = g_str->light_mode_range_end;
    g_range_prompt.apply_text = g_str->light_mode_range_apply;
    g_range_prompt.cancel_text = speed_prompt_cancel_text();
    g_range_prompt.info_label = (en ? L"Available range:\r\n" : L"Доступный диапазон:\r\n") +
                                format_edit_number(range_start) + L" .. " +
                                format_edit_number(range_end) + (en ? L" s" : L" c");
    g_range_prompt.invalid_start_text = en
        ? (L"Enter a finite number not less than " + format_edit_number(range_start) + L".")
        : (L"Введите конечное число не меньше " + format_edit_number(range_start) + L".");
    g_range_prompt.invalid_end_text = g_str->light_mode_range_invalid_end;
    g_range_prompt.wnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"LvmRangePrompt",
        g_range_prompt.title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 470, 256,
        g.main, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE)),
        nullptr);
    if (!g_range_prompt.wnd) return false;

    RECT mr{}, wr{};
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_range_prompt.wnd, &wr);
    SetWindowPos(
        g_range_prompt.wnd, HWND_TOP,
        mr.left + ((mr.right - mr.left) - (wr.right - wr.left)) / 2,
        mr.top + ((mr.bottom - mr.top) - (wr.bottom - wr.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    EnableWindow(g.main, FALSE);
    if (g_range_prompt.start_edit) {
        SetFocus(g_range_prompt.start_edit);
        SendMessageW(g_range_prompt.start_edit, EM_SETSEL, 0, -1);
    }

    MSG msg;
    while (!g_range_prompt.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_range_prompt.wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(g.main, TRUE);
    SetForegroundWindow(g.main);
    if (!g_range_prompt.accepted) return false;
    g.light_mode_open_start = g_range_prompt.start_value;
    g.light_mode_open_end = g_range_prompt.end_value;
    save_runtime_settings();
    out_start = g_range_prompt.start_value;
    out_end = g_range_prompt.end_value;
    return true;
}

bool prompt_export_options(ExportOptions& out_options) {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ExportPromptProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"LvmExportPrompt";
        atom = RegisterClassExW(&wc);
    }

    const bool en = (g_str == &kEn);
    g_export_prompt.done = false;
    g_export_prompt.accepted = false;
    g_export_prompt.save_mode = ExportSaveMode::Project;
    g_export_prompt.selected_format = ExportFileFormat::Csv;
    g_export_prompt.selected_range = has_fft_window() ? ExportRangeMode::Selected : ExportRangeMode::Visible;
    g_export_prompt.apply_processing_to_data = true;
    g_export_prompt.include_channel_names = true;
    g_export_prompt.include_hidden_channels = false;
    g_export_prompt.include_points = true;
    g_export_prompt.include_markers = true;
    g_export_prompt.include_guides = true;
    g_export_prompt.include_formulas = true;
    g_export_prompt.include_filter_settings = true;
    g_export_prompt.include_graph_settings = true;
    g_export_prompt.title = export_prompt_title_text(en);
    g_export_prompt.save_mode_label_text = en ? L"Save:" : L"Сохранение:";
    g_export_prompt.save_original_text = en ? L"Original channels" : L"Исходные каналы";
    g_export_prompt.save_applied_text = en ? L"Processed channels" : L"Обработанные каналы";
    g_export_prompt.save_project_text = en ? L"AMSignal project" : L"Проект AMSignal";
    g_export_prompt.format_label_text = en ? L"Format:" : L"Формат:";
    g_export_prompt.range_label_text = en ? L"Area:" : L"Область:";
    g_export_prompt.format_txt_text = L"TXT";
    g_export_prompt.format_csv_text = L"CSV";
    g_export_prompt.format_lvm_text = L"LVM";
    g_export_prompt.range_selected_text = en ? L"Selected" : L"Выделенная";
    g_export_prompt.range_visible_text = en ? L"Visible" : L"Видимая";
    g_export_prompt.range_whole_text = en ? L"Whole" : L"Весь";
    g_export_prompt.channel_names_text = export_include_channel_names_text(en);
    g_export_prompt.hidden_channels_text = en ? L"All channels" : L"Все каналы";
    g_export_prompt.points_text = export_include_points_text(en);
    g_export_prompt.markers_text = export_include_markers_text(en);
    g_export_prompt.guides_text = export_include_guides_text(en);
    g_export_prompt.formulas_text = export_include_formulas_text(en);
    g_export_prompt.filter_text = export_include_filter_text(en);
    g_export_prompt.graph_settings_text = export_include_graph_settings_text(en);
    g_export_prompt.continue_text = export_prompt_continue_text(en);
    g_export_prompt.cancel_text = export_prompt_cancel_text(en);
    g_export_prompt.wnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"LvmExportPrompt",
        g_export_prompt.title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 784, 348,
        g.main, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE)),
        nullptr);
    if (!g_export_prompt.wnd) return false;

    RECT mr{}, wr{};
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_export_prompt.wnd, &wr);
    SetWindowPos(
        g_export_prompt.wnd, HWND_TOP,
        mr.left + ((mr.right - mr.left) - (wr.right - wr.left)) / 2,
        mr.top + ((mr.bottom - mr.top) - (wr.bottom - wr.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    EnableWindow(g.main, FALSE);
    if (g_export_prompt.save_mode_combo) {
        SetFocus(g_export_prompt.save_mode_combo);
    }

    MSG msg;
    while (!g_export_prompt.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_export_prompt.wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(g.main, TRUE);
    SetForegroundWindow(g.main);
    if (!g_export_prompt.accepted) return false;
    out_options.save_mode = g_export_prompt.save_mode;
    out_options.format = g_export_prompt.selected_format;
    out_options.selected_range = g_export_prompt.selected_range;
    out_options.apply_processing_to_data = out_options.save_mode == ExportSaveMode::AppliedSettings;
    out_options.include_channel_names = g_export_prompt.include_channel_names;
    out_options.include_hidden_channels = g_export_prompt.include_hidden_channels;
    out_options.include_points = g_export_prompt.include_points;
    out_options.include_markers = g_export_prompt.include_markers;
    out_options.include_guides = g_export_prompt.include_guides;
    out_options.include_formulas = g_export_prompt.include_formulas;
    out_options.include_filter_settings = g_export_prompt.include_filter_settings;
    out_options.include_graph_settings = g_export_prompt.include_graph_settings;
    out_options.include_metadata = out_options.save_mode == ExportSaveMode::AppliedSettings;
    if (out_options.save_mode == ExportSaveMode::OriginalData) {
        out_options.apply_processing_to_data = false;
        out_options.include_channel_names = true;
        out_options.include_hidden_channels = true;
        out_options.include_points = false;
        out_options.include_markers = false;
        out_options.include_guides = false;
        out_options.include_formulas = false;
        out_options.include_filter_settings = false;
        out_options.include_graph_settings = false;
    }
    return true;
}

bool prompt_exact_guide_value(bool vertical, double& out_value) {
    double default_value = 0.0;
    if (vertical) {
        default_value = (g.mode == AnalysisMode::FRF) ? std::pow(10.0,0.5*(g.frf.log_start+g.frf.log_end)) :
                        (g.mode == AnalysisMode::FFT) ? 0.5 * (g.freq_start + g.freq_end)
                                    : 0.5 * (g.win_start + g.win_end);
    } else if ((g.mode == AnalysisMode::FRF)) {
        double ymin=0.0,ymax=1.0;
        frf_y_range(ymin,ymax);
        default_value=0.5*(ymin+ymax);
    } else if ((g.mode == AnalysisMode::FFT)) {
        double ymin = 0.0, ymax = 0.0;
        if (!current_freq_yrange(ymin, ymax)) {
            ymin = 0.0;
            ymax = 1.0;
        }
        default_value = 0.5 * (ymin + ymax);
    } else {
        double ymin = 0.0, ymax = 0.0;
        if (!current_time_yrange(ymin, ymax)) {
            ymin = -1.0;
            ymax = 1.0;
        } else if (!g.auto_y) {
            ymin = g.y_lock_min;
            ymax = g.y_lock_max;
        }
        default_value = 0.5 * (ymin + ymax);
    }
    return prompt_numeric_value(
        guide_prompt_title_text(vertical),
        guide_prompt_label_text(vertical),
        guide_prompt_apply_text(),
        guide_prompt_cancel_text(),
        guide_prompt_invalid_text(),
        default_value,
        false,
        out_value);
}

bool prompt_custom_play_speed(double& out_speed) {
    return prompt_numeric_value(
        speed_prompt_title_text(),
        speed_prompt_label_text(),
        speed_prompt_apply_text(),
        speed_prompt_cancel_text(),
        speed_prompt_invalid_text(),
        g.play_speed,
        true,
        out_speed);
}

} // namespace gui
