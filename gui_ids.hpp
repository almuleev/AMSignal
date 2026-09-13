#pragma once
#include "gui_platform.hpp"

namespace gui {

enum {
    IDC_OPEN = 1001,
    IDC_SAVEPNG,
    IDC_SAVECSV,
    IDC_PLAY = 1005,
    IDC_MEASURE,
    IDC_ZOOMIN,
    IDC_ZOOMOUT,
    IDC_RESET,
    IDC_PANLEFT,
    IDC_PANRIGHT,
    IDC_GOTO_START,
    IDC_GOTO_END,
    IDC_AUTOY,          // toolbar: auto-fit vertical scale (was lock_y)
    IDC_PTSETTINGS,     // welcome screen: open the point/settings action
    IDC_SIDEPANEL,      // toolbar: show / hide the docked side panel
    IDC_SHOW_ALL,
    IDC_HIDE_ALL,
    IDC_EXPORT_SCOPE_CURRENT,
    IDC_EXPORT_SCOPE_FRAGMENT,
    IDC_EXPORT_APPLY_SETTINGS,
    IDC_EXPORT_APPLY_DATA,
    IDC_EXPORT_INCLUDE_CHANNEL_NAMES,
    IDC_EXPORT_INCLUDE_HIDDEN_CHANNELS,
    IDC_EXPORT_INCLUDE_POINTS,
    IDC_EXPORT_INCLUDE_MARKERS,
    IDC_EXPORT_INCLUDE_GUIDES,
    IDC_EXPORT_INCLUDE_FORMULAS,
    IDC_EXPORT_INCLUDE_FILTER,
    IDC_EXPORT_INCLUDE_GRAPH_SETTINGS,
    IDC_EXPORT_SAVE_MODE,
    IDC_SAVE_PROJECT,
    IDC_DOCUMENT_SELECTOR,
    IDC_CLOSE_DOCUMENT,

    // Menu-only commands (no toolbar button).
    IDM_EXIT = 1100,
    IDM_VISMOOTH,       // visual (spline) smoothing toggle
    IDM_VPAN,           // vertical pan toggle
    IDM_ADD_VLINE,      // arm: place a vertical guide line
    IDM_ADD_HLINE,      // arm: place a horizontal guide line
    IDM_ADD_VLINE_EXACT, // add a vertical guide line by exact value
    IDM_ADD_HLINE_EXACT, // add a horizontal guide line by exact value
    IDM_ADD_MARKER,     // arm: place a marker
    IDM_CLEAR_LINES,
    IDM_CLEAR_MARKERS,
    IDM_CLEAR_POINTS,
    IDM_HOTKEYS,
    IDM_ABOUT,
    IDM_SETTINGS,
    IDW_START = 1114,   // welcome screen: start working

    // Playback speed menu items.
    IDM_SPEED_00001 = 1300,
    IDM_SPEED_0001,
    IDM_SPEED_001,
    IDM_SPEED_01,
    IDM_SPEED_05,
    IDM_SPEED_1,
    IDM_SPEED_2,
    IDM_SPEED_5,
    IDM_SPEED_10,
    IDM_SPEED_CUSTOM,

    IDM_UNDO = 1400,
    IDM_REDO,
    IDM_THEME = 1500,
    IDM_LANG_RU = 1600,
    IDM_LANG_EN = 1601,
    IDM_MODE_TIME = 1700,
    IDM_MODE_FREQ,
    IDM_MODE_FRF,
    IDM_RECENT_FILE_BASE = 1900,
    IDM_OPEN_DOCUMENT_BASE = 1950,
    IDM_CLOSE_DOCUMENT = 1999,
    IDW_LIGHT_MODE = 1800,

    IDC_CHAN_BASE = 2000,
    IDC_CHAN_LABEL_BASE = 3000,
    IDC_CHAN_EDIT = 4000,
    IDC_CHAN_COEFFICIENT_BASE = 7000,

    IDC_SIDE_TAB_CHANNELS = 4200,
    IDC_SIDE_TAB_POINTS,
    IDC_SIDE_TAB_FILTER,
    IDC_SIDE_CHANNEL_COLOR,
    IDC_SIDE_CHANNEL_HINT,
    IDC_SIDE_GLOBAL_FORMULA_EDIT,
    IDC_SIDE_GLOBAL_FORMULA_APPLY,
    IDC_SIDE_FORMULA_EDIT,
    IDC_SIDE_FORMULA_APPLY_SELECTED,
    IDC_SIDE_FORMULA_APPLY_VISIBLE,
    IDC_SIDE_FORMULA_RESET_SELECTED,
    IDC_SIDE_FORMULA_RESET_ALL,
    IDC_SIDE_POINT_GROUP_LIST,
    IDC_SIDE_POINT_GROUP_VISIBLE,
    IDC_SIDE_POINT_GROUP_NEW,
    IDC_SIDE_POINT_GROUP_DELETE,
    IDC_SIDE_POINT_GROUP_NAME,
    IDC_SIDE_POINT_GROUP_RENAME,
    IDC_SIDE_POINT_COLOR_CURRENT,
    IDC_SIDE_POINT_GROUP_COLOR,
    IDC_SIDE_PT_NUM,
    IDC_SIDE_PT_X,
    IDC_SIDE_PT_Y,
    IDC_SIDE_PT_DX,
    IDC_SIDE_PT_DY,
    IDC_SIDE_PT_INVDT,
    IDC_SIDE_PT_DIST,
    IDC_SIDE_PT_SNAP,
    IDC_SIDE_FILTER_ENABLE,
    IDC_SIDE_FILTER_MODE,
    IDC_SIDE_FILTER_TOPOLOGY,
    IDC_SIDE_FILTER_LOW_TRACK,
    IDC_SIDE_FILTER_HIGH_TRACK,

    IDC_SET_LANG_RU = 5000,
    IDC_SET_LANG_EN,
    IDC_SET_HOTKEY_LIST,
    IDC_SET_HOTKEY_CTRL,
    IDC_SET_HOTKEY_SHIFT,
    IDC_SET_HOTKEY_ALT,
    IDC_SET_HOTKEY_KEY,
    IDC_SET_HOTKEY_APPLY,
    IDC_SET_HOTKEY_RESET,
    IDC_SET_HOTKEY_CLEAR,
    IDC_SET_HOTKEY_RESET_ALL,

    IDC_SET_GAP_MARKERS = 5125,
    IDC_SET_STITCH_GAPS,
    IDC_SET_AXIS_X_LABEL_STATIC,
    IDC_SET_AXIS_X_LABEL_EDIT,
    IDC_SET_AXIS_Y_LABEL_STATIC,
    IDC_SET_AXIS_Y_LABEL_EDIT,
    IDC_SET_GROUP_GENERAL = 5150,
    IDC_SET_GROUP_HOTKEYS,

    IDW_TITLE = 5200,
    IDW_VERSION,
    IDW_ACTIONS_TITLE,
    IDW_ACTIONS_HINT,
    IDW_LANG_LABEL,
    // Used by the welcome screen; Settings has no separate theme label.
    IDW_THEME_LABEL,
    IDW_THEME_LIGHT,
    IDW_THEME_DARK,
    IDW_RECENT_FILES,
};

inline const int kTopBar = 72;

// two-row compact toolbar
inline const int kRightPanel = 312;

inline const int kBottomBar = 28;

// status-bar strip at the very bottom
inline const int kAxisBottom = 38;

// room under the plot for the X tick labels + title
inline const int kAxisLeft = 70;

inline const int IDC_SPEED_PROMPT_EDIT = 6200;

inline const int IDC_SPEED_PROMPT_OK = 6201;

inline const int IDC_SPEED_PROMPT_CANCEL = 6202;

inline const int IDC_RANGE_PROMPT_START_EDIT = 6203;

inline const int IDC_RANGE_PROMPT_END_EDIT = 6204;

inline const int IDC_RANGE_PROMPT_OK = 6205;

inline const int IDC_RANGE_PROMPT_CANCEL = 6206;

inline const int IDC_RANGE_PROMPT_AUTOFILL = 6210;

inline const int IDC_HOTKEYS_DIALOG_LIST = 6207;

inline const int IDC_HOTKEYS_DIALOG_CLOSE = 6208;

inline const int IDC_LOADING_CANCEL = 6209;

inline constexpr UINT WM_APP_ASYNC_SCAN_DONE = WM_APP + 1;

inline constexpr UINT WM_APP_ASYNC_LOAD_DONE = WM_APP + 2;

} // namespace gui
