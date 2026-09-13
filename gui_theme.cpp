// Theme: native viewer implementation.
#include "gui_theme.hpp"
#include "gui_menu.hpp"
#include "gui_settings_window.hpp"
#include "gui_state.hpp"
#include "gui_settings.hpp"

namespace gui {

COLORREF channel_color(std::size_t i) {
    if (i < g.channel_colors.size()) return g.channel_colors[i];
    return kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

const Theme kLightTheme = {
    RGB(250, 252, 255),   // bg_main
    RGB(235, 240, 246),   // bg_toolbar
    RGB(242, 245, 250),   // bg_panel
    RGB(255, 255, 255),   // bg_plot
    RGB(245, 247, 250),   // bg_status
    RGB(230, 235, 240),   // grid
    RGB(240, 243, 246),   // minor_grid
    RGB(180, 190, 200),   // frame
    RGB(80, 90, 100),     // axis_text
    RGB(30, 40, 50),      // text_primary
    RGB(100, 110, 120),   // text_secondary
    RGB(0, 120, 212),     // accent
    RGB(0, 100, 180),     // accent_hover
    RGB(210, 218, 228),   // separator
    RGB(250, 251, 253),   // btn_bg
    RGB(200, 208, 218),   // btn_border
    RGB(230, 235, 242),   // btn_hover
    RGB(0, 120, 212),     // btn_active
    RGB(170, 175, 180),   // btn_pressed
    RGB(220, 40, 40),     // playhead
    RGB(200, 0, 0),       // marker_color
};

const Theme kDarkTheme = {
    RGB(30, 32, 38),      // bg_main
    RGB(45, 48, 55),      // bg_toolbar
    RGB(38, 40, 48),      // bg_panel
    RGB(25, 28, 35),      // bg_plot
    RGB(35, 38, 45),      // bg_status
    RGB(55, 60, 70),      // grid
    RGB(40, 44, 52),      // minor_grid
    RGB(80, 85, 95),      // frame
    RGB(180, 185, 190),   // axis_text
    RGB(220, 225, 230),   // text_primary
    RGB(140, 145, 150),   // text_secondary
    RGB(0, 150, 255),     // accent
    RGB(30, 130, 235),    // accent_hover
    RGB(60, 65, 75),      // separator
    RGB(55, 58, 65),      // btn_bg
    RGB(75, 80, 90),      // btn_border
    RGB(70, 75, 85),      // btn_hover
    RGB(0, 150, 255),     // btn_active
    RGB(20, 25, 35),      // btn_pressed
    RGB(255, 60, 60),     // playhead
    RGB(255, 80, 80),     // marker_color
};

const Theme* g_theme = &kLightTheme;

HBRUSH g_panel_brush = nullptr;

HBRUSH g_input_brush = nullptr;

HBRUSH g_welcome_brush = nullptr;

HBRUSH g_welcome_hero_brush = nullptr;

HBRUSH g_welcome_action_brush = nullptr;

HICON g_program_logo_icon = nullptr;

std::vector<std::unique_ptr<OwnerDrawMenuEntry>> g_menu_text_storage;

void update_theme_brushes() {
    if (g_panel_brush) DeleteObject(g_panel_brush);
    g_panel_brush = CreateSolidBrush(g_theme->bg_panel);
    if (g_input_brush) DeleteObject(g_input_brush);
    g_input_brush = CreateSolidBrush(g_theme->bg_plot);
    if (g_welcome_brush) DeleteObject(g_welcome_brush);
    g_welcome_brush = CreateSolidBrush(g_theme->bg_main);
    if (g_welcome_hero_brush) DeleteObject(g_welcome_hero_brush);
    g_welcome_hero_brush = CreateSolidBrush(g_theme->bg_panel);
    if (g_welcome_action_brush) DeleteObject(g_welcome_action_brush);
    g_welcome_action_brush = CreateSolidBrush(g_theme->btn_bg);
}

void unload_program_logo() {
    if (g_program_logo_icon) {
        DestroyIcon(g_program_logo_icon);
        g_program_logo_icon = nullptr;
    }
}

bool load_program_logo() {
    unload_program_logo();
    HINSTANCE self = GetModuleHandleW(nullptr);
    HICON icon = reinterpret_cast<HICON>(
        LoadImageW(
            self,
            MAKEINTRESOURCEW(IDI_APP_LOGO),
            IMAGE_ICON,
            0,
            0,
            LR_DEFAULTSIZE | LR_DEFAULTCOLOR));
    if (!icon) {
        g_program_logo_icon = nullptr;
        return false;
    }
    g_program_logo_icon = icon;
    return true;
}

const OwnerDrawMenuEntry* stash_menu_entry(const std::wstring& text, bool top_level, bool popup,
                                           const std::wstring& subtitle, bool recent_file) {
    auto entry = std::make_unique<OwnerDrawMenuEntry>();
    entry->text = text;
    entry->subtitle = subtitle;
    entry->top_level = top_level;
    entry->popup = popup;
    entry->recent_file = recent_file;
    g_menu_text_storage.push_back(std::move(entry));
    return g_menu_text_storage.back().get();
}

void refresh_theme_windows() {
    auto redraw = [](HWND wnd) {
        if (!wnd || !IsWindow(wnd)) return;
        RedrawWindow(
            wnd, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    };
    redraw(g.main);
    redraw(g.settings_wnd);
    redraw(g.welcome_wnd);
    redraw(g.channel_edit);
    for (HWND h : g.checks) redraw(h);
    for (HWND h : g.check_labels) redraw(h);
}

void apply_theme_choice(const Theme* theme) {
    if (!theme || g_theme == theme) return;
    g_theme = theme;
    save_app_settings();
    update_theme_brushes();
    if (g.menu) {
        MENUINFO mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
        mi.hbrBack = CreateSolidBrush(g_theme->bg_toolbar);
        SetMenuInfo(g.menu, &mi);
    }
    sync_menu();
    refresh_settings_controls();
    refresh_theme_windows();
}

COLORREF g_custom_colors[16] = {0};

} // namespace gui
