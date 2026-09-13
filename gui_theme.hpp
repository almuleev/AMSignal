#pragma once
#include "gui_platform.hpp"

namespace gui {

inline const COLORREF kPalette[] = {
    RGB(31, 119, 180), RGB(255, 127, 14), RGB(44, 160, 44), RGB(214, 39, 40),
    RGB(148, 103, 189), RGB(140, 86, 75), RGB(227, 119, 194), RGB(127, 127, 127),
    RGB(188, 189, 34), RGB(23, 190, 207),
};

COLORREF channel_color(std::size_t i);

struct Theme {
    COLORREF bg_main, bg_toolbar, bg_panel, bg_plot, bg_status;
    COLORREF grid, minor_grid, frame, axis_text;
    COLORREF text_primary, text_secondary;
    COLORREF accent, accent_hover, separator;
    COLORREF btn_bg, btn_border, btn_hover, btn_active;
    COLORREF btn_pressed; // "pressed" look for active toggle buttons
    COLORREF playhead;
    COLORREF marker_color;
};

extern const Theme kLightTheme;

extern const Theme kDarkTheme;

extern const Theme* g_theme;

extern HBRUSH g_panel_brush;

extern HBRUSH g_input_brush;

extern HBRUSH g_welcome_brush;

extern HBRUSH g_welcome_hero_brush;

extern HBRUSH g_welcome_action_brush;

extern HICON g_program_logo_icon;

struct OwnerDrawMenuEntry {
    std::wstring text;
    std::wstring subtitle;
    bool top_level = false;
    bool popup = false;
    bool recent_file = false;
};

extern std::vector<std::unique_ptr<OwnerDrawMenuEntry>> g_menu_text_storage;

void update_theme_brushes();

inline constexpr int IDI_APP_LOGO = 101;

void unload_program_logo();

bool load_program_logo();

const OwnerDrawMenuEntry* stash_menu_entry(const std::wstring& text, bool top_level, bool popup,
                                           const std::wstring& subtitle = L"", bool recent_file = false);

void refresh_theme_windows();

void apply_theme_choice(const Theme* theme);

extern COLORREF g_custom_colors[16];

} // namespace gui
