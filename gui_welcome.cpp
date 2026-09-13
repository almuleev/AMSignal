// Welcome: native viewer implementation.
#include "gui_welcome.hpp"
#include "gui_settings_window.hpp"
#include "gui_controls.hpp"
#include "gui_hotkeys.hpp"
#include "gui_ids.hpp"
#include "gui_layout.hpp"
#include "gui_loading.hpp"
#include "gui_documents.hpp"
#include "gui_loading_drop.hpp"
#include "gui_settings.hpp"
#include "gui_side_panel.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"
#include "gui_window.hpp"

namespace gui {

std::wstring welcome_version_text() {
    return (g_str == &kEn)
        ? (std::wstring(L"Build: ") + APP_VERSION_W)
        : (std::wstring(L"Версия сборки: ") + APP_VERSION_W);
}

const wchar_t* welcome_actions_title_text() {
    return (g_str == &kEn) ? L"Start here" : L"Начните здесь";
}

const wchar_t* welcome_open_button_text() {
    return (g_str == &kEn) ? L"Open file…" : L"Открыть файл…";
}

const wchar_t* welcome_actions_hint_text() {
    return (g_str == &kEn)
        ? L"Open a file or pick a recent one."
        : L"Откройте файл или выберите недавний.";
}

const wchar_t* welcome_author_credit_text() {
    return (g_str == &kEn)
        ? L"Application developed by Alexander Muleev  |  al.muleev@gmail.com"
        : L"Приложение разработал Александр Мулеев  |  al.muleev@gmail.com";
}

int rect_width(const RECT& r) {
    return r.right - r.left;
}

int rect_height(const RECT& r) {
    return r.bottom - r.top;
}

WelcomeLayout compute_welcome_layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    WelcomeLayout layout{};
    const int client_w = max(0, static_cast<int>(rc.right - rc.left));
    const int client_h = max(0, static_cast<int>(rc.bottom - rc.top));
    const int outer = (client_w >= 1200) ? 36 : ((client_w >= 900) ? 28 : 20);
    const int gap = (client_w >= 900) ? 24 : 16;
    const int content_w = max(320, min(client_w - outer * 2, 1180));
    const int content_h = max(260, client_h - outer * 2);
    const int x0 = max(0, (client_w - content_w) / 2);
    const int y0 = max(0, (client_h - content_h) / 2);

    layout.bounds = { x0, y0, x0 + content_w, y0 + content_h };
    layout.stacked = (content_w < 760);

    if (layout.stacked) {
        int hero_h = std::clamp(content_h * 48 / 100, 220, 340);
        const int min_action_h = 320;
        hero_h = min(hero_h, max(180, content_h - gap - min_action_h));
        layout.hero = { x0, y0, x0 + content_w, y0 + hero_h };
        layout.action = { x0, layout.hero.bottom + gap, x0 + content_w, y0 + content_h };
    } else {
        const int action_w = std::clamp(content_w / 3, 320, 380);
        layout.hero = { x0, y0, x0 + content_w - action_w - gap, y0 + content_h };
        layout.action = { layout.hero.right + gap, y0, x0 + content_w, y0 + content_h };
    }

    layout.compact = rect_height(layout.action) < 560 || rect_width(layout.action) < 340;

    return layout;
}

void layout_welcome_controls(HWND hwnd) {
    WelcomeLayout layout = compute_welcome_layout(hwnd);
    auto place = [&](int id, int x, int y, int w, int h, bool visible = true) {
        HWND ctl = GetDlgItem(hwnd, id);
        if (ctl) {
            MoveWindow(ctl, x, y, max(1, w), max(1, h), TRUE);
            ShowWindow(ctl, visible ? SW_SHOWNA : SW_HIDE);
        }
    };

    const int hero_pad = layout.compact ? 20 : (layout.stacked ? 22 : 30);
    const int hx = layout.hero.left + hero_pad;
    int hy = layout.hero.top + hero_pad;
    const int hw = max(180, rect_width(layout.hero) - hero_pad * 2);
    const int title_h = layout.compact ? 40 : (layout.stacked ? 46 : 52);
    const int version_h = layout.compact ? 18 : 20;
    place(IDW_TITLE, hx, hy, hw, title_h);
    hy += title_h + (layout.compact ? 6 : 8);
    place(IDW_VERSION, hx, hy, hw, version_h);
    hy += version_h + (layout.compact ? 4 : 6);

    const int action_button_h = layout.compact ? 34 : 38;
    const int button_gap = layout.compact ? 8 : (layout.stacked ? 10 : 12);
    const int section_hint_h = layout.compact ? 20 : 22;
    place(IDW_ACTIONS_TITLE, hx, hy, hw, 22);
    hy += 24;
    place(IDW_ACTIONS_HINT, hx, hy, hw, section_hint_h);
    hy += section_hint_h + (layout.compact ? 10 : 14);

    const int action_pad = layout.compact ? 16 : (layout.stacked ? 20 : 24);
    const int ax = layout.hero.left + action_pad;
    const int aw = max(180, rect_width(layout.hero) - action_pad * 2);
    const int settings_col_gap = layout.compact ? 8 : 12;
    const int segment_button_h = layout.compact ? 26 : 30;
    if (aw < 360) {
        int ay = hy;
        place(IDC_OPEN, ax, ay, aw, action_button_h + 2);
        ay += action_button_h + 2 + button_gap;
        place(IDW_RECENT_FILES, ax, ay, aw, action_button_h);
        ay += action_button_h + button_gap;
        place(IDC_PTSETTINGS, ax, ay, aw, action_button_h);
        ay += action_button_h + button_gap;
        place(IDM_HOTKEYS, ax, ay, aw, action_button_h);
        ay += action_button_h + button_gap;
        place(IDW_START, ax, ay, aw, action_button_h + 2);
    } else {
        int col_w = max(140, (aw - settings_col_gap) / 2);
        int left_x = ax;
        int right_x = ax + col_w + settings_col_gap;
        int grid_y = hy;
        place(IDC_OPEN, left_x, grid_y, col_w, action_button_h + 2);
        place(IDC_PTSETTINGS, right_x, grid_y, col_w, action_button_h);
        grid_y += action_button_h + button_gap;
        place(IDW_RECENT_FILES, left_x, grid_y, col_w, action_button_h);
        place(IDM_HOTKEYS, right_x, grid_y, col_w, action_button_h);
        grid_y += action_button_h + button_gap;
        place(IDW_START, ax, grid_y, aw, action_button_h + 2);
    }

    const int settings_pad = layout.compact ? 16 : (layout.stacked ? 20 : 24);
    const int sx = layout.action.left + settings_pad;
    int sy = layout.action.top + settings_pad;
    const int sw = max(180, rect_width(layout.action) - settings_pad * 2);
    place(IDW_LANG_LABEL, sx, sy, sw, 20);
    sy += layout.compact ? 22 : 24;
    place(IDM_LANG_RU, sx, sy, sw, segment_button_h);
    sy += segment_button_h + (layout.compact ? 8 : 12);
    place(IDM_LANG_EN, sx, sy, sw, segment_button_h);
    sy += segment_button_h + (layout.compact ? 10 : 14);
    place(IDW_THEME_LABEL, sx, sy, sw, 20);
    sy += layout.compact ? 22 : 24;
    place(IDW_THEME_LIGHT, sx, sy, sw, segment_button_h);
    sy += segment_button_h + (layout.compact ? 8 : 12);
    place(IDW_THEME_DARK, sx, sy, sw, segment_button_h);
    sy += segment_button_h + (layout.compact ? 10 : 14);
    place(IDW_LIGHT_MODE, sx, sy, sw, segment_button_h);
}

RecentFilesPanelState g_recent_files_panel;

std::wstring recent_file_panel_item_text(const std::wstring& path) {
    const wchar_t* base = wcsrchr(path.c_str(), L'\\');
    if (!base) base = wcsrchr(path.c_str(), L'/');
    std::wstring name = base ? base + 1 : path;
    std::wstring folder;
    if (base && base > path.c_str()) {
        folder.assign(path.c_str(), base - path.c_str());
        const std::size_t keep = 30;
        if (folder.size() > keep) {
            folder = L"..." + folder.substr(folder.size() - keep);
        }
    }
    if (folder.empty()) return name;
    return name + L"\n" + folder;
}

std::pair<std::wstring, std::wstring> split_recent_file_panel_text(const wchar_t* text) {
    std::wstring value = text ? text : L"";
    const std::size_t sep = value.find(L'\n');
    if (sep == std::wstring::npos) return { std::move(value), std::wstring() };
    std::wstring title = value.substr(0, sep);
    std::wstring subtitle = value.substr(sep + 1);
    return { std::move(title), std::move(subtitle) };
}

void draw_recent_file_panel_button(HDC dc, const RECT& r, const wchar_t* txt, bool pressed) {
    COLORREF bg_col, border_col, text_col, sub_col;
    if (g_theme == &kDarkTheme) {
        bg_col = pressed ? mix_color(g_theme->btn_hover, g_theme->separator, 62)
                         : mix_color(g_theme->btn_bg, g_theme->bg_panel, 68);
        border_col = mix_color(g_theme->btn_border, g_theme->separator, 44);
        text_col = g_theme->text_primary;
        sub_col = g_theme->text_secondary;
    } else {
        bg_col = pressed ? g_theme->btn_hover : g_theme->btn_bg;
        border_col = g_theme->btn_border;
        text_col = g_theme->text_primary;
        sub_col = g_theme->text_secondary;
    }

    fill_rounded_rect(dc, r, bg_col, border_col, 7);

    RECT inner = { r.left + 1, r.top + 1, r.right - 1, r.top + 10 };
    if (g_theme != &kDarkTheme) {
        HBRUSH gloss = CreateSolidBrush(mix_color(bg_col, RGB(255, 255, 255), pressed ? 8 : 18));
        FillRect(dc, &inner, gloss);
        DeleteObject(gloss);
    } else {
        HBRUSH sheen = CreateSolidBrush(mix_color(bg_col, RGB(255, 255, 255), pressed ? 4 : 10));
        FillRect(dc, &inner, sheen);
        DeleteObject(sheen);
    }

    const auto [title, subtitle] = split_recent_file_panel_text(txt);
    const int icon_size = 10;
    RECT icon = { r.left + 12, r.top + 14, r.left + 12 + icon_size, r.top + 14 + icon_size };
    HBRUSH icon_brush = CreateSolidBrush(g_theme->accent);
    FillRect(dc, &icon, icon_brush);
    DeleteObject(icon_brush);

    HFONT base_font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT title_font = g.bold_font ? g.bold_font : base_font;
    HFONT subtitle_font = g.axis_font ? g.axis_font : base_font;

    SetBkMode(dc, TRANSPARENT);
    RECT title_rect = { r.left + 30, r.top + 8, r.right - 12, r.top + 25 };
    RECT subtitle_rect = { r.left + 30, r.top + 24, r.right - 12, r.bottom - 7 };
    if (pressed) {
        OffsetRect(&title_rect, 0, 1);
        OffsetRect(&subtitle_rect, 0, 1);
        OffsetRect(&icon, 0, 1);
    }
    HGDIOBJ old_font = SelectObject(dc, title_font);
    SetTextColor(dc, text_col);
    DrawTextW(dc, title.c_str(), -1, &title_rect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, subtitle_font);
    SetTextColor(dc, sub_col);
    DrawTextW(dc, subtitle.c_str(), -1, &subtitle_rect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

void update_recent_files_panel_items() {
    if (!g_recent_files_panel.wnd || !IsWindow(g_recent_files_panel.wnd)) return;
    const HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for (std::size_t i = 0; i < kMaxRecentFiles; ++i) {
        HWND item = g_recent_files_panel.items[i];
        if (!item || !IsWindow(item)) continue;
        if (i < g.recent_files.size()) {
            const std::wstring text = recent_file_panel_item_text(g.recent_files[i]);
            SetWindowTextW(item, text.c_str());
            SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            ShowWindow(item, SW_SHOWNA);
        } else {
            ShowWindow(item, SW_HIDE);
        }
    }
    RedrawWindow(g_recent_files_panel.wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void layout_welcome_recent_files_panel(HWND owner) {
    if (!owner || !g_recent_files_panel.wnd || !IsWindow(g_recent_files_panel.wnd)) return;
    WelcomeLayout layout = compute_welcome_layout(owner);
    HWND anchor = GetDlgItem(owner, IDW_RECENT_FILES);
    RECT anchor_rc{};
    if (anchor && IsWindow(anchor)) {
        GetWindowRect(anchor, &anchor_rc);
        POINT pts[2] = { {anchor_rc.left, anchor_rc.top}, {anchor_rc.right, anchor_rc.bottom} };
        MapWindowPoints(nullptr, owner, pts, 2);
        anchor_rc.left = pts[0].x;
        anchor_rc.top = pts[0].y;
        anchor_rc.right = pts[1].x;
        anchor_rc.bottom = pts[1].y;
    } else {
        anchor_rc = { layout.hero.left + 24, layout.hero.top + 160, layout.hero.left + 180, layout.hero.top + 194 };
    }

    const std::size_t visible_items = std::min<std::size_t>(g.recent_files.size(), kMaxRecentFiles);
    const int panel_w = std::clamp(std::max(320, rect_width(layout.hero) / 2), 320, std::max(320, std::min(460, rect_width(layout.hero) - 18)));
    const int header_h = kRecentPanelHeaderHeight;
    const int items_h = visible_items
        ? static_cast<int>(visible_items) * kRecentPanelItemHeight + static_cast<int>(visible_items - 1) * kRecentPanelItemGap
        : 52;
    const int panel_h = kRecentPanelPad * 2 + header_h + 6 + items_h;

    int x = anchor_rc.left;
    int y = anchor_rc.bottom + 8;
    const int min_x = layout.hero.left + 12;
    const int max_x = layout.hero.right - panel_w - 12;
    if (x > max_x) x = max_x;
    if (x < min_x) x = min_x;
    const int max_y = layout.bounds.bottom - panel_h - 12;
    if (y > max_y) {
        const int above = anchor_rc.top - 8 - panel_h;
        y = (above >= layout.bounds.top + 12) ? above : max_y;
    }
    y = std::max(y, static_cast<int>(layout.bounds.top) + 12);

    SetWindowPos(g_recent_files_panel.wnd, HWND_TOP, x, y, panel_w, panel_h, SWP_SHOWWINDOW);

    const int item_x = kRecentPanelPad;
    const int item_w = panel_w - kRecentPanelPad * 2;
    int item_y = kRecentPanelPad + header_h + 6;
    for (std::size_t i = 0; i < kMaxRecentFiles; ++i) {
        HWND item = g_recent_files_panel.items[i];
        if (!item || !IsWindow(item)) continue;
        if (i < visible_items) {
            MoveWindow(item, item_x, item_y, item_w, kRecentPanelItemHeight, TRUE);
            ShowWindow(item, SW_SHOWNA);
            item_y += kRecentPanelItemHeight + kRecentPanelItemGap;
        } else {
            ShowWindow(item, SW_HIDE);
        }
    }
    update_recent_files_panel_items();
}

void hide_welcome_recent_files_panel() {
    g_recent_files_panel.visible = false;
    if (g_recent_files_panel.wnd && IsWindow(g_recent_files_panel.wnd)) {
        ShowWindow(g_recent_files_panel.wnd, SW_HIDE);
    }
    if (g.welcome_wnd && IsWindow(g.welcome_wnd)) {
        RedrawWindow(g.welcome_wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

void show_welcome_recent_files_panel(HWND owner) {
    if (!g_recent_files_panel.visible) return;
    if (!owner || !IsWindow(owner)) owner = g.welcome_wnd;
    if (!owner || !IsWindow(owner)) return;
    register_recent_files_panel_class(owner);
    if (!g_recent_files_panel.wnd || !IsWindow(g_recent_files_panel.wnd)) {
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(owner, GWLP_HINSTANCE));
        HWND wnd = CreateWindowExW(
            0, L"LvmWelcomeRecentPanel", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            0, 0, 10, 10, owner, nullptr, inst, nullptr);
        if (!wnd) return;
        g_recent_files_panel.wnd = wnd;
    }
    layout_welcome_recent_files_panel(owner);
    ShowWindow(g_recent_files_panel.wnd, SW_SHOWNA);
    SetWindowPos(g_recent_files_panel.wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    RedrawWindow(g_recent_files_panel.wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void toggle_welcome_recent_files_panel(HWND owner) {
    g_recent_files_panel.visible = !g_recent_files_panel.visible;
    if (g_recent_files_panel.visible) {
        show_welcome_recent_files_panel(owner);
    } else {
        hide_welcome_recent_files_panel();
    }
}

LRESULT CALLBACK WelcomeRecentPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            for (std::size_t i = 0; i < kMaxRecentFiles; ++i) {
                HWND item = CreateWindowExW(
                    0, L"BUTTON", L"",
                    WS_CHILD | BS_OWNERDRAW,
                    0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRecentPanelItemBase + static_cast<int>(i))),
                    inst, nullptr);
                if (item) {
                    SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                    g_recent_files_panel.items[i] = item;
                }
            }
            update_recent_files_panel_items();
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id >= kRecentPanelItemBase && id < kRecentPanelItemBase + static_cast<int>(kMaxRecentFiles)) {
                const std::size_t index = static_cast<std::size_t>(id - kRecentPanelItemBase);
                if (index < g.recent_files.size()) {
                    const std::wstring path = g.recent_files[index];
                    hide_welcome_recent_files_panel();
                    queue_open_paths({path});
                }
                return 0;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            bool on_item = false;
            for (std::size_t i = 0; i < kMaxRecentFiles; ++i) {
                HWND item = g_recent_files_panel.items[i];
                if (!item || !IsWindow(item) || !IsWindowVisible(item)) continue;
                RECT item_rc{};
                GetWindowRect(item, &item_rc);
                POINT pts[2] = { {item_rc.left, item_rc.top}, {item_rc.right, item_rc.bottom} };
                MapWindowPoints(nullptr, hwnd, pts, 2);
                item_rc.left = pts[0].x;
                item_rc.top = pts[0].y;
                item_rc.right = pts[1].x;
                item_rc.bottom = pts[1].y;
                if (PtInRect(&item_rc, pt)) {
                    on_item = true;
                    break;
                }
            }
            if (!on_item) {
                hide_welcome_recent_files_panel();
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            fill_rounded_rect(dc, rc, g_theme->bg_panel, g_theme->separator, 14);

            RECT accent = { rc.left + kRecentPanelPad, rc.top + 12, rc.left + min(120, rect_width(rc) - 2 * kRecentPanelPad), rc.top + 16 };
            HBRUSH accent_brush = CreateSolidBrush(g_theme->accent);
            FillRect(dc, &accent, accent_brush);
            DeleteObject(accent_brush);

            SetBkMode(dc, TRANSPARENT);
            HFONT title_font = g.bold_font ? g.bold_font : (g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
            HGDIOBJ old_font = SelectObject(dc, title_font);
            SetTextColor(dc, g_theme->text_primary);
            RECT title = { rc.left + kRecentPanelPad, rc.top + 20, rc.right - kRecentPanelPad - 40, rc.top + 44 };
            DrawTextW(dc, g_str->welcome_btn_recent, -1, &title, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dc, old_font);

            if (g.recent_files.empty()) {
                RECT msg = { rc.left + kRecentPanelPad, rc.top + 56, rc.right - kRecentPanelPad, rc.bottom - kRecentPanelPad };
                SetTextColor(dc, g_theme->text_secondary);
                DrawTextW(
                    dc,
                    (g_str == &kEn) ? L"No recent files yet.\r\nOpen a file to add one." : L"Недавних файлов пока нет.\r\nОткройте файл, чтобы список появился.",
                    -1, &msg, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis || !dis->hwndItem) break;
            const int id = GetDlgCtrlID(dis->hwndItem);
            if (id >= kRecentPanelItemBase && id < kRecentPanelItemBase + static_cast<int>(kMaxRecentFiles)) {
                wchar_t txt[512]{};
                GetWindowTextW(dis->hwndItem, txt, 512);
                const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                draw_recent_file_panel_button(dis->hDC, dis->rcItem, txt, pressed);
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            g_recent_files_panel.wnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void register_recent_files_panel_class(HWND owner) {
    static ATOM atom = 0;
    if (atom) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WelcomeRecentPanelProc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(owner, GWLP_HINSTANCE));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"LvmWelcomeRecentPanel";
    atom = RegisterClassExW(&wc);
}

LRESULT CALLBACK WelcomeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            auto mkstatic = [&](const wchar_t* text, int id, HFONT use_font) {
                HWND ctl = CreateWindowExW(
                    0, L"STATIC", text,
                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_NOPREFIX,
                    0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(use_font ? use_font : font), TRUE);
                return ctl;
            };
            auto mkbtn = [&](const wchar_t* text, int id) {
                HWND ctl = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                    0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return ctl;
            };
            auto mkcheck = [&](const wchar_t* text, int id) {
                HWND ctl = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_OWNERDRAW,
                    0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return ctl;
            };

            mkstatic(g_str->welcome_title, IDW_TITLE, g.title_font ? g.title_font : font);
            mkstatic(welcome_version_text().c_str(), IDW_VERSION, g.axis_font ? g.axis_font : font);
            mkstatic(g_str->m_lang, IDW_LANG_LABEL, g.bold_font ? g.bold_font : font);
            mkbtn(g_str->lang_ru, IDM_LANG_RU);
            mkbtn(g_str->lang_en, IDM_LANG_EN);
            mkstatic((g_str == &kEn) ? L"Theme" : L"Тема", IDW_THEME_LABEL, g.bold_font ? g.bold_font : font);
            mkbtn(g_str->theme_light, IDW_THEME_LIGHT);
            mkbtn(g_str->theme_dark, IDW_THEME_DARK);
            mkstatic(welcome_actions_title_text(), IDW_ACTIONS_TITLE, g.bold_font ? g.bold_font : font);
            mkstatic(welcome_actions_hint_text(), IDW_ACTIONS_HINT, font);
            mkbtn(welcome_open_button_text(), IDC_OPEN);
            mkbtn(g_str->welcome_btn_recent, IDW_RECENT_FILES);
            HWND light_mode = mkcheck(g_str->light_mode, IDW_LIGHT_MODE);
            set_toggle_checked(light_mode, g.light_mode);
            mkbtn(settings_button_text(), IDC_PTSETTINGS);
            mkbtn(g_str->welcome_btn_hotkeys, IDM_HOTKEYS);
            mkbtn(g_str->welcome_btn_start, IDW_START);
            if (g_program_logo_icon) {
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_program_logo_icon));
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_program_logo_icon));
            }
            layout_welcome_controls(hwnd);
            enable_file_drop_support(hwnd);
            return 0;
        }
        case WM_SIZE:
            layout_welcome_controls(hwnd);
            show_welcome_recent_files_panel(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(g_theme->bg_main);
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);

            WelcomeLayout layout = compute_welcome_layout(hwnd);
            fill_rounded_rect(hdc, layout.hero, g_theme->bg_panel, g_theme->separator, 18);
            fill_rounded_rect(hdc, layout.action, g_theme->btn_bg, g_theme->separator, 18);

            RECT hero_accent = {
                layout.hero.left + 22,
                layout.hero.top + 18,
                layout.hero.left + min(140, rect_width(layout.hero) - 44),
                layout.hero.top + 24
            };
            HBRUSH accent_brush = CreateSolidBrush(g_theme->accent);
            FillRect(hdc, &hero_accent, accent_brush);
            DeleteObject(accent_brush);

            RECT action_header = {
                layout.action.left + 20,
                layout.action.top + 18,
                layout.action.right - 20,
                layout.action.top + 22
            };
            HBRUSH action_accent = CreateSolidBrush(g_theme->separator);
            FillRect(hdc, &action_header, action_accent);
            DeleteObject(action_accent);

            HFONT old_font = reinterpret_cast<HFONT>(SelectObject(
                hdc, g.axis_font ? g.axis_font : (g.ui_font ? g.ui_font : GetStockObject(DEFAULT_GUI_FONT))));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, g_theme->text_secondary);
            RECT credit = {
                max(rc.left + 220, layout.bounds.right - 420),
                max(layout.bounds.bottom + 2, rc.bottom - 24),
                rc.right - 24,
                rc.bottom - 8
            };
            DrawTextW(
                hdc,
                welcome_author_credit_text(),
                -1,
                &credit,
                DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(hdc, old_font);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND:
            if (LOWORD(wp) != IDW_RECENT_FILES) {
                hide_welcome_recent_files_panel();
            }
            switch (LOWORD(wp)) {
                case IDC_OPEN: ShowWindow(hwnd, SW_HIDE); show_ui_controls(); SendMessageW(g.main, WM_COMMAND, IDC_OPEN, 0); return 0;
                case IDW_RECENT_FILES: show_recent_files_menu(hwnd); return 0;
                case IDC_PTSETTINGS: open_settings(); return 0;
                case IDM_HOTKEYS: show_hotkeys(); return 0;
                case IDW_START: ShowWindow(hwnd, SW_HIDE); show_ui_controls(); return 0;
                case IDW_LIGHT_MODE:
                    if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED) {
                        HWND light = GetDlgItem(hwnd, IDW_LIGHT_MODE);
                        toggle_checked_state(light);
                        apply_light_mode(is_toggle_checked(light));
                    }
                    return 0;
                case IDW_THEME_LIGHT: apply_theme_choice(&kLightTheme); return 0;
                case IDW_THEME_DARK: apply_theme_choice(&kDarkTheme); return 0;
                case IDM_LANG_RU: if (g_str != &kRu) { g_str = &kRu; save_runtime_settings(); rebuild_ui(); } return 0;
                case IDM_LANG_EN: if (g_str != &kEn) { g_str = &kEn; save_runtime_settings(); rebuild_ui(); } return 0;
            }
            return 0;
        case WM_DROPFILES:
            if (g.main && IsWindow(g.main)) {
                SendMessageW(g.main, WM_DROPFILES, wp, lp);
                return 0;
            }
            return 0;
        case WM_LBUTTONDOWN: {
            if (!g_recent_files_panel.visible || !g_recent_files_panel.wnd || !IsWindow(g_recent_files_panel.wnd)) break;
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT panel_rc{};
            GetWindowRect(g_recent_files_panel.wnd, &panel_rc);
            POINT pts[2] = { {panel_rc.left, panel_rc.top}, {panel_rc.right, panel_rc.bottom} };
            MapWindowPoints(nullptr, hwnd, pts, 2);
            panel_rc.left = pts[0].x;
            panel_rc.top = pts[0].y;
            panel_rc.right = pts[1].x;
            panel_rc.bottom = pts[1].y;
            if (!PtInRect(&panel_rc, pt)) {
                hide_welcome_recent_files_panel();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis->hwndItem) break;
            const HDC dc = dis->hDC;
            const RECT r = dis->rcItem;
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            int ctl_id = GetDlgCtrlID(dis->hwndItem);
            bool is_lang_button = (ctl_id == IDM_LANG_RU || ctl_id == IDM_LANG_EN);
            bool is_theme_button = (ctl_id == IDW_THEME_LIGHT || ctl_id == IDW_THEME_DARK);
            bool active = (is_lang_button &&
                ((ctl_id == IDM_LANG_RU && g_str == &kRu) || (ctl_id == IDM_LANG_EN && g_str == &kEn))) ||
                (is_theme_button &&
                ((ctl_id == IDW_THEME_LIGHT && g_theme == &kLightTheme) || (ctl_id == IDW_THEME_DARK && g_theme == &kDarkTheme)));
            wchar_t txt[128]{};
            GetWindowTextW(dis->hwndItem, txt, 128);

            if (ctl_id == IDW_START) {
                draw_welcome_action_button(dc, r, txt, pressed, true, false);
                return TRUE;
            }
            if (ctl_id == IDC_OPEN) {
                draw_welcome_action_button(dc, r, txt, pressed, false, true);
                return TRUE;
            }
            if (ctl_id == IDW_RECENT_FILES) {
                draw_welcome_action_button(dc, r, txt, pressed, false, g_recent_files_panel.visible);
                return TRUE;
            }
            if (ctl_id == IDC_PTSETTINGS || ctl_id == IDM_HOTKEYS) {
                draw_welcome_action_button(dc, r, txt, pressed, false, false);
                return TRUE;
            }
            if (ctl_id == IDW_LIGHT_MODE) {
                draw_themed_check_control(
                    dc, r, txt,
                    is_toggle_checked(dis->hwndItem), pressed,
                    IsWindowEnabled(dis->hwndItem) != FALSE,
                    false, false, g_theme->btn_bg);
                return TRUE;
            }
            draw_themed_button(dc, r, txt, pressed, active, false);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            int ctl_id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if (ctl_id == IDW_VERSION || ctl_id == IDW_ACTIONS_HINT) {
                SetTextColor(dc, g_theme->text_secondary);
            } else if (ctl_id == IDW_LANG_LABEL || ctl_id == IDW_THEME_LABEL || ctl_id == IDW_ACTIONS_TITLE) {
                SetTextColor(dc, g_theme->accent);
            } else {
                SetTextColor(dc, g_theme->text_primary);
            }
            if (ctl_id == IDW_TITLE || ctl_id == IDW_VERSION || ctl_id == IDW_ACTIONS_TITLE || ctl_id == IDW_ACTIONS_HINT) {
                return reinterpret_cast<LRESULT>(g_welcome_hero_brush ? g_welcome_hero_brush : g_welcome_brush);
            }
            return reinterpret_cast<LRESULT>(g_welcome_action_brush ? g_welcome_action_brush : g_welcome_brush);
        }
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_welcome_action_brush ? g_welcome_action_brush : g_welcome_brush);
        }
        case WM_DESTROY:
            hide_welcome_recent_files_panel();
            g.welcome_wnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void show_welcome(HINSTANCE inst) {
    if (g.welcome_wnd) {
        hide_ui_controls();
        RECT rc;
        GetClientRect(g.main, &rc);
        SetWindowPos(g.welcome_wnd, HWND_TOP, 0, 0, rc.right, rc.bottom, SWP_SHOWWINDOW);
        show_welcome_recent_files_panel(g.welcome_wnd);
        RedrawWindow(g.welcome_wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        return;
    }
    RECT rc;
    GetClientRect(g.main, &rc);
    g.welcome_wnd = CreateWindowExW(
        0,
        L"LvmWelcome", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, rc.right, rc.bottom,
        g.main, nullptr, inst, nullptr);
    if (!g.welcome_wnd) return;
    hide_ui_controls();
    SetWindowPos(g.welcome_wnd, HWND_TOP, 0, 0, rc.right, rc.bottom, SWP_SHOWWINDOW);
    show_welcome_recent_files_panel(g.welcome_wnd);
    RedrawWindow(g.welcome_wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void raise_main_window() {
    if (!g.main || !IsWindow(g.main)) return;
    ShowWindow(g.main, SW_SHOW);
    SetWindowPos(g.main, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g.main);
    SetActiveWindow(g.main);
}

void show_about() {
    show_welcome(GetModuleHandleW(nullptr));
}

} // namespace gui
