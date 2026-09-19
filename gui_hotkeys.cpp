// Keyboard bindings, accelerator routing, and shortcut help.
#include "gui_hotkeys.hpp"
#include "gui_controls.hpp"
#include "gui_state.hpp"
#include "gui_ids.hpp"
#include "gui_text.hpp"
#include "gui_dialogs.hpp"
#include "gui_loading_drop.hpp"
#include "gui_settings_window.hpp"
#include "gui_theme.hpp"

namespace gui {

HotkeysDialogState g_hotkeys_dialog;

const HotkeyBinding* find_hotkey_binding(int command) {
    for (const auto& hk : g.hotkeys)
        if (hk.command == command) return &hk;
    return nullptr;
}

std::vector<HotkeyBinding> default_hotkeys() {
    return {
        {IDC_OPEN, FVIRTKEY | FCONTROL, 'O'},
        {IDC_SAVEPNG, FVIRTKEY | FCONTROL | FALT, 'S'},
        {IDC_SAVECSV, FVIRTKEY | FCONTROL | FSHIFT, 'S'},
        {IDC_SAVE_PROJECT, FVIRTKEY | FCONTROL, 'S'},
        {IDM_UNDO, FVIRTKEY | FCONTROL, 'Z'},
        {IDM_REDO, FVIRTKEY | FCONTROL | FSHIFT, 'Z'},
        {IDM_MODE_TIME, FVIRTKEY, 'T'},
        {IDM_MODE_FREQ, FVIRTKEY, 'F'},
        {IDM_MODE_FRF, FVIRTKEY | FCONTROL, 'R'},
        {IDC_MEASURE, FVIRTKEY, 'P'},
        {IDM_ADD_MARKER, FVIRTKEY, 'M'},
        {IDM_ADD_VLINE, FVIRTKEY, 'V'},
        {IDM_ADD_HLINE, FVIRTKEY, 'H'},
        {IDC_AUTOY, FVIRTKEY, 'A'},
        {IDM_VISMOOTH, FVIRTKEY, 'C'},
        {IDM_VPAN, FVIRTKEY, 'Y'},
        {IDM_THEME, FVIRTKEY, 'D'},
        {IDC_PLAY, FVIRTKEY, VK_SPACE},
        {IDC_ZOOMIN, FVIRTKEY, VK_OEM_PLUS},
        {IDC_ZOOMOUT, FVIRTKEY, VK_OEM_MINUS},
        {IDC_PANLEFT, FVIRTKEY, VK_LEFT},
        {IDC_PANRIGHT, FVIRTKEY, VK_RIGHT},
        {IDC_RESET, FVIRTKEY, VK_HOME},
        {IDC_GOTO_START, FVIRTKEY | FCONTROL, VK_HOME},
        {IDC_GOTO_END, FVIRTKEY | FCONTROL, VK_END},
        {IDM_CLEAR_POINTS, FVIRTKEY, VK_DELETE},
        {IDM_HOTKEYS, FVIRTKEY, VK_F1},
    };
}

void ensure_hotkeys_initialized() {
    if (g.hotkeys.empty()) {
        g.hotkeys = default_hotkeys();
        return;
    }
    // Move the former default Ctrl+S (PNG) to Ctrl+Alt+S. A deliberately
    // customized binding is retained, but a missing project command receives
    // the new standard binding when Ctrl+S is available.
    HotkeyBinding* png = nullptr;
    HotkeyBinding* project = nullptr;
    bool ctrl_s_taken = false;
    for (auto& binding : g.hotkeys) {
        if (binding.command == IDC_SAVEPNG) png = &binding;
        if (binding.command == IDC_SAVE_PROJECT) project = &binding;
        if (binding.fvirt == (FVIRTKEY | FCONTROL) && binding.key == 'S') ctrl_s_taken = true;
    }
    if (png && png->fvirt == (FVIRTKEY | FCONTROL) && png->key == 'S') {
        png->fvirt = FVIRTKEY | FCONTROL | FALT;
        ctrl_s_taken = false;
    }
    if (!project && !ctrl_s_taken) g.hotkeys.push_back({IDC_SAVE_PROJECT, FVIRTKEY | FCONTROL, 'S'});
}

std::wstring key_name(WORD key) {
    switch (key) {
        case 0: return g_str == &kEn ? L"None" : L"Нет";
        case VK_TAB: return L"Tab";
        case VK_BACK: return L"Backspace";
        case VK_RETURN: return L"Enter";
        case VK_INSERT: return L"Insert";
        case VK_PRIOR: return L"Page Up";
        case VK_NEXT: return L"Page Down";
        case VK_SPACE: return g_str == &kEn ? L"Space" : L"Пробел";
        case VK_LEFT: return g_str == &kEn ? L"Left" : L"Влево";
        case VK_RIGHT: return g_str == &kEn ? L"Right" : L"Вправо";
        case VK_UP: return g_str == &kEn ? L"Up" : L"Вверх";
        case VK_DOWN: return g_str == &kEn ? L"Down" : L"Вниз";
        case VK_HOME: return L"Home";
        case VK_END: return L"End";
        case VK_DELETE: return L"Delete";
        case VK_ESCAPE: return L"Esc";
        case VK_PAUSE: return L"Pause";
        case VK_CAPITAL: return L"Caps Lock";
        case VK_NUMLOCK: return L"Num Lock";
        case VK_SCROLL: return L"Scroll Lock";
        case VK_SNAPSHOT: return L"Print Screen";
        case VK_APPS: return L"Menu";
        case VK_OEM_PLUS: return L"+";
        case VK_OEM_MINUS: return L"-";
        default:
            if (key >= VK_F13 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);
            if (key >= 'A' && key <= 'Z') return std::wstring(1, static_cast<wchar_t>(key));
            if (key >= '0' && key <= '9') return std::wstring(1, static_cast<wchar_t>(key));
            if (key >= VK_F1 && key <= VK_F12) return L"F" + std::to_wstring(key - VK_F1 + 1);
            wchar_t buf[16]{};
            swprintf(buf, 16, L"VK_%04X", static_cast<unsigned int>(key & 0xFFFFu));
            return buf;
    }
}

std::wstring hotkey_text(BYTE fvirt, WORD key) {
    if (key == 0) return g_str == &kEn ? L"Not assigned" : L"Не назначено";
    std::wstring out;
    if (fvirt & FCONTROL) out += L"Ctrl+";
    if (fvirt & FSHIFT) out += L"Shift+";
    if (fvirt & FALT) out += L"Alt+";
    out += key_name(key);
    return out;
}

std::wstring hotkey_text_for_command(int command) {
    const HotkeyBinding* hk = find_hotkey_binding(command);
    return hk ? hotkey_text(hk->fvirt, hk->key) : std::wstring();
}

std::wstring hotkey_display_text_for_command(int command) {
    std::wstring text = hotkey_text_for_command(command);
    if (!text.empty()) return text;
    return (g_str == &kEn) ? L"Not assigned" : L"Не назначено";
}

std::wstring command_name(int command) {
    const bool en = (g_str == &kEn);
    switch (command) {
        case IDC_OPEN: return en ? L"Open file" : L"Открыть файл";
        case IDC_SAVEPNG: return en ? L"Save PNG" : L"Сохранить PNG";
        case IDC_SAVECSV: return en ? L"Save as…" : L"Сохранить как…";
        case IDC_SAVE_PROJECT: return en ? L"Save project" : L"Сохранить проект";
        case IDM_UNDO: return en ? L"Undo" : L"Отменить";
        case IDM_REDO: return en ? L"Redo" : L"Повторить";
        case IDM_MODE_TIME: return en ? L"Time view" : L"Режим времени";
        case IDM_MODE_FREQ: return en ? L"Hz / FFT view" : L"Режим Гц / БПФ";
        case IDM_MODE_FRF: return en ? L"FRF view" : L"Режим FRF / АЧХ";
        case IDC_MEASURE: return en ? L"Measurement points" : L"Точки измерения";
        case IDM_ADD_MARKER: return en ? L"Marker" : L"Маркер";
        case IDM_ADD_VLINE: return en ? L"Vertical line" : L"Вертикальная линия";
        case IDM_ADD_HLINE: return en ? L"Horizontal line" : L"Горизонтальная линия";
        case IDM_ADD_VLINE_EXACT: return en ? L"Vertical line (exact)" : L"Вертикальная линия (точно)";
        case IDM_ADD_HLINE_EXACT: return en ? L"Horizontal line (exact)" : L"Горизонтальная линия (точно)";
        case IDC_AUTOY: return en ? L"Auto zoom" : L"Автомасштабирование";
        case IDM_VISMOOTH: return en ? L"Smoothing" : L"Сглаживание";
        case IDM_CURVE_SYMBOLS: return en ? L"Curve symbols for grayscale" : L"Фигуры кривых для Ч/Б";
        case IDM_VPAN: return en ? L"Vertical pan" : L"Вертикальное панорамирование";
        case IDM_THEME: return en ? L"Dark theme" : L"Тёмная тема";
        case IDC_PLAY: return en ? L"Play / Pause" : L"Старт/стоп";
        case IDC_ZOOMIN: return en ? L"Zoom in" : L"Увеличить";
        case IDC_ZOOMOUT: return en ? L"Zoom out" : L"Уменьшить";
        case IDC_PANLEFT: return en ? L"Pan left" : L"Сдвиг влево";
        case IDC_PANRIGHT: return en ? L"Pan right" : L"Сдвиг вправо";
        case IDC_RESET: return en ? L"Reset view" : L"Сброс вида";
        case IDC_GOTO_START: return en ? L"Go to start" : L"В начало";
        case IDC_GOTO_END: return en ? L"Go to end" : L"В конец";
        case IDM_CLEAR_POINTS: return en ? L"Clear points" : L"Очистить точки";
        case IDM_HOTKEYS: return en ? L"Hotkeys help" : L"Справка по клавишам";
        case IDM_SPEED_CUSTOM: return en ? L"Playback speed…" : L"Скорость воспроизведения…";
        default: return L"?";
    }
}

std::vector<int> hotkey_command_order() {
    return {
        IDC_OPEN, IDC_SAVEPNG, IDC_SAVECSV, IDC_SAVE_PROJECT, IDM_UNDO, IDM_REDO,
        IDM_MODE_TIME, IDM_MODE_FREQ, IDM_MODE_FRF, IDC_MEASURE, IDM_ADD_MARKER,
        IDM_ADD_VLINE, IDM_ADD_HLINE, IDC_AUTOY, IDM_VISMOOTH, IDM_CURVE_SYMBOLS,
        IDM_VPAN, IDM_THEME, IDC_PLAY, IDC_ZOOMIN, IDC_ZOOMOUT,
        IDC_PANLEFT, IDC_PANRIGHT, IDC_RESET, IDC_GOTO_START,
        IDC_GOTO_END, IDM_CLEAR_POINTS, IDM_HOTKEYS
    };
}

std::wstring hotkey_list_item_text(int command) {
    return command_name(command) + L"  [" + hotkey_display_text_for_command(command) + L"]";
}

void append_hotkey_line(std::wstring& out, int command) {
    out += L"  " + hotkey_display_text_for_command(command) + L"\t: " + command_name(command) + L"\n";
}

std::wstring hotkeys_body_text() {
    ensure_hotkeys_initialized();
    const bool en = (g_str == &kEn);
    std::wstring out = en ? L"Files\n" : L"Файлы\n";
    append_hotkey_line(out, IDC_OPEN);
    append_hotkey_line(out, IDC_SAVEPNG);
    append_hotkey_line(out, IDC_SAVECSV);
    append_hotkey_line(out, IDC_SAVE_PROJECT);
    append_hotkey_line(out, IDM_UNDO);
    append_hotkey_line(out, IDM_REDO);
    out += L"\n";

    out += en ? L"Modes and tools\n" : L"Режимы и инструменты\n";
    append_hotkey_line(out, IDM_MODE_TIME);
    append_hotkey_line(out, IDM_MODE_FREQ);
    append_hotkey_line(out, IDM_MODE_FRF);
    append_hotkey_line(out, IDC_MEASURE);
    append_hotkey_line(out, IDM_ADD_MARKER);
    append_hotkey_line(out, IDM_ADD_VLINE);
    append_hotkey_line(out, IDM_ADD_HLINE);
    out += en ? L"  Esc\t: Cancel current add mode\n\n" : L"  Esc\t: Отменить текущий режим добавления\n\n";

    out += en ? L"View\n" : L"Вид\n";
    append_hotkey_line(out, IDC_AUTOY);
    append_hotkey_line(out, IDM_VISMOOTH);
    append_hotkey_line(out, IDM_CURVE_SYMBOLS);
    append_hotkey_line(out, IDM_VPAN);
    append_hotkey_line(out, IDM_THEME);
    append_hotkey_line(out, IDC_PLAY);
    append_hotkey_line(out, IDC_ZOOMIN);
    append_hotkey_line(out, IDC_ZOOMOUT);
    append_hotkey_line(out, IDC_PANLEFT);
    append_hotkey_line(out, IDC_PANRIGHT);
    append_hotkey_line(out, IDC_RESET);
    append_hotkey_line(out, IDC_GOTO_START);
    append_hotkey_line(out, IDC_GOTO_END);
    append_hotkey_line(out, IDM_CLEAR_POINTS);
    out += L"\n";

    out += en ? L"Mouse\n" : L"Мышь\n";
    out += en ? L"  Wheel\t: Zoom under cursor\n" : L"  Колесо\t: Масштаб под курсором\n";
    out += en ? L"  Shift+Wheel\t: Pan left / right\n" : L"  Shift+колесо\t: Прокрутка влево / вправо\n";
    out += en ? L"  Ctrl+Wheel\t: Zoom Y\n" : L"  Ctrl+колесо\t: Масштаб по Y\n";
    out += en ? L"  Alt+Wheel\t: Pan up / down (Y)\n" : L"  Alt+колесо\t: Сдвиг вверх / вниз по Y\n";
    out += en ? L"  Left-drag\t: Pan view\n" : L"  ЛКМ + тяга\t: Панорамирование\n";
    out += en ? L"  Left-click\t: Place point / line / marker in active mode\n" : L"  ЛКМ\t: Поставить точку / линию / маркер в активном режиме\n";
    out += en ? L"  Right-click\t: Clear points\n\n" : L"  ПКМ\t: Очистить точки\n\n";
    append_hotkey_line(out, IDM_HOTKEYS);
    return out;
}

const wchar_t* hotkeys_dialog_close_text() {
    return (g_str == &kEn) ? L"Close" : L"Закрыть";
}

std::vector<std::wstring> hotkeys_dialog_lines() {
    std::vector<std::wstring> lines;
    std::wstring body = hotkeys_body_text();
    std::size_t start = 0;
    while (start <= body.size()) {
        std::size_t end = body.find(L'\n', start);
        std::wstring line = body.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return lines;
}

SIZE hotkeys_dialog_client_size() {
    const auto lines = hotkeys_dialog_lines();
    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND measure_wnd = g.main ? g.main : GetDesktopWindow();
    HDC dc = GetDC(measure_wnd);
    HFONT old_font = dc ? reinterpret_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    TEXTMETRICW tm{};
    if (dc) GetTextMetricsW(dc, &tm);

    int max_line_width = 0;
    for (const std::wstring& line : lines) {
        SIZE text_size{};
        const wchar_t* text = line.empty() ? L" " : line.c_str();
        int length = line.empty() ? 1 : static_cast<int>(line.size());
        if (dc && GetTextExtentPoint32W(dc, text, length, &text_size)) {
            max_line_width = max(max_line_width, static_cast<int>(text_size.cx));
        }
    }

    if (dc) {
        if (old_font) SelectObject(dc, old_font);
        ReleaseDC(measure_wnd, dc);
    }

    const int title_h = max(20, static_cast<int>(tm.tmHeight + tm.tmExternalLeading));
    const int line_h = max(18, static_cast<int>(tm.tmHeight + tm.tmExternalLeading + 2));
    const int button_h = 30;
    const int client_w = max(620, max_line_width + 56);
    const int client_h = 16 + title_h + 8 + static_cast<int>(lines.size()) * line_h + 8 + 12 + button_h + 18;
    return { client_w, max(client_h, 300) };
}

void populate_hotkeys_dialog_list(HWND list) {
    if (!list) return;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    const auto lines = hotkeys_dialog_lines();
    for (const std::wstring& line : lines) {
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
}

LRESULT CALLBACK HotkeysDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int pad = 18;
            const int title_y = 16;
            const int title_h = 20;
            const int list_y = 44;
            const int button_h = 30;
            const int button_w = 122;
            const int button_y = rc.bottom - pad - button_h;
            const int list_w = max(200, static_cast<int>(rc.right - pad * 2));
            const int list_h = max(120, button_y - 12 - list_y);
            HWND label = CreateWindowExW(
                0, L"STATIC",
                (g_str == &kEn) ? L"Configured keyboard shortcuts" : L"Настроенные горячие клавиши",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                pad, title_y, list_w, title_h, hwnd, nullptr,
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            g_hotkeys_dialog.list = CreateWindowExW(
                0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT |
                LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                pad, list_y, list_w, list_h, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HOTKEYS_DIALOG_LIST)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND close = CreateWindowExW(
                0, L"BUTTON", hotkeys_dialog_close_text(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                rc.right - pad - button_w, button_y, button_w, button_h, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HOTKEYS_DIALOG_CLOSE)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            if (label) SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (g_hotkeys_dialog.list) SendMessageW(g_hotkeys_dialog.list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (close) SendMessageW(close, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            populate_hotkeys_dialog_list(g_hotkeys_dialog.list);
            enable_file_drop_support(hwnd);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_HOTKEYS_DIALOG_CLOSE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DROPFILES:
            if (g.main && IsWindow(g.main)) {
                SendMessageW(g.main, WM_DROPFILES, wp, lp);
                return 0;
            }
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
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_plot);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mis && mis->CtlType == ODT_LISTBOX && mis->CtlID == IDC_HOTKEYS_DIALOG_LIST) {
                measure_settings_list_item(mis);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis || !dis->hwndItem) break;
            if (dis->CtlType == ODT_LISTBOX && dis->CtlID == IDC_HOTKEYS_DIALOG_LIST) {
                draw_settings_list_item(dis);
                return TRUE;
            }
            if (GetDlgCtrlID(dis->hwndItem) == IDC_HOTKEYS_DIALOG_CLOSE) {
                wchar_t txt[64]{};
                GetWindowTextW(dis->hwndItem, txt, 64);
                const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, false, false);
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            g_hotkeys_dialog.wnd = nullptr;
            g_hotkeys_dialog.list = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void show_hotkeys() {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HotkeysDialogProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"LvmHotkeysDialog";
        atom = RegisterClassExW(&wc);
    }

    if (g_hotkeys_dialog.wnd && IsWindow(g_hotkeys_dialog.wnd)) {
        ShowWindow(g_hotkeys_dialog.wnd, SW_RESTORE);
        SetForegroundWindow(g_hotkeys_dialog.wnd);
        if (g_hotkeys_dialog.list) SetFocus(g_hotkeys_dialog.list);
        return;
    }

    RECT work_area{};
    if (HMONITOR monitor = MonitorFromWindow(g.main ? g.main : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST)) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(monitor, &mi)) work_area = mi.rcWork;
    }
    if (work_area.right <= work_area.left || work_area.bottom <= work_area.top) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    }

    const SIZE desired_client_size = hotkeys_dialog_client_size();
    const int max_client_w = max(420, static_cast<int>((work_area.right - work_area.left) - 24));
    const int max_client_h = max(300, static_cast<int>((work_area.bottom - work_area.top) - 24));
    RECT dlg_rc{
        0, 0,
        min(static_cast<int>(desired_client_size.cx), max_client_w),
        min(static_cast<int>(desired_client_size.cy), max_client_h)
    };
    AdjustWindowRectEx(&dlg_rc, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW);

    g_hotkeys_dialog.wnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"LvmHotkeysDialog",
        g_str->dlg_hotkeys_title,
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, dlg_rc.right - dlg_rc.left, dlg_rc.bottom - dlg_rc.top,
        g.main, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE)),
        nullptr);
    if (!g_hotkeys_dialog.wnd) return;

    RECT mr{}, wr{};
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_hotkeys_dialog.wnd, &wr);
    const int window_w = wr.right - wr.left;
    const int window_h = wr.bottom - wr.top;
    int x = mr.left + ((mr.right - mr.left) - window_w) / 2;
    int y = mr.top + ((mr.bottom - mr.top) - window_h) / 2;
    x = std::clamp(x,
        static_cast<int>(work_area.left),
        max(static_cast<int>(work_area.left), static_cast<int>(work_area.right - window_w)));
    y = std::clamp(y,
        static_cast<int>(work_area.top),
        max(static_cast<int>(work_area.top), static_cast<int>(work_area.bottom - window_h)));
    SetWindowPos(
        g_hotkeys_dialog.wnd, HWND_TOP,
        x, y,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    ShowWindow(g_hotkeys_dialog.wnd, SW_SHOWNORMAL);
    UpdateWindow(g_hotkeys_dialog.wnd);
    if (g_hotkeys_dialog.list) SetFocus(g_hotkeys_dialog.list);
}

int hotkey_binding_index(int command) {
    for (std::size_t i = 0; i < g.hotkeys.size(); ++i)
        if (g.hotkeys[i].command == command) return static_cast<int>(i);
    return -1;
}

void set_hotkey_binding(int command, BYTE fvirt, WORD key) {
    int idx = hotkey_binding_index(command);
    if (idx >= 0) {
        g.hotkeys[static_cast<std::size_t>(idx)].fvirt = fvirt;
        g.hotkeys[static_cast<std::size_t>(idx)].key = key;
    } else {
        g.hotkeys.push_back({command, fvirt, key});
    }
}

HotkeyBinding default_hotkey_for_command(int command) {
    for (const auto& hk : default_hotkeys())
        if (hk.command == command) return hk;
    return {command, FVIRTKEY, 0};
}

int find_conflicting_hotkey(BYTE fvirt, WORD key, int except_command) {
    if (key == 0) return 0;
    for (const auto& hk : g.hotkeys) {
        if (hk.command == except_command) continue;
        if (hk.key == key && hk.fvirt == fvirt) return hk.command;
    }
    return 0;
}

HACCEL make_accelerators() {
    ensure_hotkeys_initialized();
    std::vector<ACCEL> acc;
    acc.reserve(g.hotkeys.size());
    for (const auto& hk : g.hotkeys) {
        if (hk.key == 0) continue;
        ACCEL a = {};
        a.fVirt = hk.fvirt;
        a.key = hk.key;
        a.cmd = static_cast<WORD>(hk.command);
        acc.push_back(a);
    }
    if (acc.empty()) return nullptr;
    return CreateAcceleratorTableW(acc.data(), static_cast<int>(acc.size()));
}

void rebuild_accelerators() {
    HACCEL fresh = make_accelerators();
    if (g.accel) DestroyAcceleratorTable(g.accel);
    g.accel = fresh;
}

bool should_bypass_accelerators() {
    HWND focus = GetFocus();
    if (!focus) return false;
    if (g.channel_edit && (focus == g.channel_edit || IsChild(g.channel_edit, focus) != FALSE)) return true;
    if (g.settings_wnd && (focus == g.settings_wnd || IsChild(g.settings_wnd, focus) != FALSE)) return true;
    if (g.frf_panel && IsChild(g.frf_panel, focus)) return true;
    wchar_t cls[64]{};
    for (HWND h = focus; h; h = GetParent(h)) {
        GetClassNameW(h, cls, 64);
        if (lstrcmpiW(cls, L"EDIT") == 0 ||
            lstrcmpiW(cls, L"COMBOBOX") == 0 ||
            lstrcmpiW(cls, L"ComboBoxEx32") == 0 ||
            wcsstr(cls, L"RICHEDIT") == cls) {
            return true;
        }
        if (h == g.main) break;
    }
    return false;
}

} // namespace gui
