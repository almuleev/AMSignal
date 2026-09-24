// Keyboard shortcut editor controls in the settings window.
#include "gui_settings_hotkeys.hpp"
#include "gui_hotkeys.hpp"
#include "gui_controls.hpp"
#include "gui_menu.hpp"
#include "gui_settings.hpp"
#include "gui_status.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "gui_ids.hpp"

namespace gui {

void hotkey_combo_add(HWND combo, const wchar_t* text, WORD key) {
    int idx = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text)));
    SendMessageW(combo, CB_SETITEMDATA, idx, key);
}

void hotkey_combo_add_key(HWND combo, WORD key) {
    std::wstring text = key_name(key);
    if (text.empty()) {
        wchar_t buf[16]{};
        swprintf(buf, 16, L"VK_%04X", static_cast<unsigned int>(key & 0xFFFFu));
        text = buf;
    }
    hotkey_combo_add(combo, text.c_str(), key);
}

void populate_hotkey_key_combo(HWND combo) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    hotkey_combo_add(combo, g_str == &kEn ? L"None" : L"Нет", 0);
    for (wchar_t ch = L'A'; ch <= L'Z'; ++ch) {
        hotkey_combo_add_key(combo, static_cast<WORD>(ch));
    }
    for (wchar_t ch = L'0'; ch <= L'9'; ++ch) {
        hotkey_combo_add_key(combo, static_cast<WORD>(ch));
    }
    for (int i = 1; i <= 24; ++i) {
        hotkey_combo_add_key(combo, static_cast<WORD>(VK_F1 + i - 1));
    }
    hotkey_combo_add_key(combo, VK_TAB);
    hotkey_combo_add_key(combo, VK_BACK);
    hotkey_combo_add_key(combo, VK_RETURN);
    hotkey_combo_add_key(combo, VK_INSERT);
    hotkey_combo_add_key(combo, VK_HOME);
    hotkey_combo_add_key(combo, VK_PRIOR);
    hotkey_combo_add_key(combo, VK_NEXT);
    hotkey_combo_add_key(combo, VK_END);
    hotkey_combo_add_key(combo, VK_DELETE);
    hotkey_combo_add_key(combo, VK_ESCAPE);
    hotkey_combo_add_key(combo, VK_PAUSE);
    hotkey_combo_add_key(combo, VK_CAPITAL);
    hotkey_combo_add_key(combo, VK_NUMLOCK);
    hotkey_combo_add_key(combo, VK_SCROLL);
    hotkey_combo_add_key(combo, VK_SNAPSHOT);
    hotkey_combo_add_key(combo, VK_LEFT);
    hotkey_combo_add_key(combo, VK_RIGHT);
    hotkey_combo_add_key(combo, VK_UP);
    hotkey_combo_add_key(combo, VK_DOWN);
    hotkey_combo_add_key(combo, VK_SPACE);
    hotkey_combo_add_key(combo, VK_OEM_PLUS);
    hotkey_combo_add_key(combo, VK_OEM_MINUS);
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
    InvalidateRect(combo, nullptr, TRUE);
}

int combo_index_by_key(HWND combo, WORD key) {
    int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i)
        if (static_cast<WORD>(SendMessageW(combo, CB_GETITEMDATA, i, 0)) == key) return i;
    return -1;
}

int settings_selected_hotkey_command(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_SET_HOTKEY_LIST);
    if (!list) return 0;
    int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return 0;
    return static_cast<int>(SendMessageW(list, LB_GETITEMDATA, sel, 0));
}

void load_selected_hotkey_controls(HWND hwnd) {
    int command = settings_selected_hotkey_command(hwnd);
    const HotkeyBinding* hk = find_hotkey_binding(command);
    BYTE fvirt = hk ? hk->fvirt : FVIRTKEY;
    WORD key = hk ? hk->key : 0;
    set_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_CTRL), (fvirt & FCONTROL) != 0);
    set_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_SHIFT), (fvirt & FSHIFT) != 0);
    set_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_ALT), (fvirt & FALT) != 0);
    HWND combo = GetDlgItem(hwnd, IDC_SET_HOTKEY_KEY);
    if (combo) {
        int idx = combo_index_by_key(combo, key);
        if (idx < 0 && key != 0) {
            std::wstring custom = key_name(key);
            if (custom.empty()) {
                wchar_t buf[16]{};
                swprintf(buf, 16, L"VK_%04X", static_cast<unsigned int>(key & 0xFFFFu));
                custom = buf;
            }
            idx = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(custom.c_str())));
            if (idx >= 0) SendMessageW(combo, CB_SETITEMDATA, idx, key);
        }
        SendMessageW(combo, CB_SETCURSEL, (idx >= 0) ? idx : 0, 0);
        InvalidateRect(combo, nullptr, TRUE);
    }
}

void reset_all_hotkeys_to_defaults(HWND hwnd) {
    int selected_command = settings_selected_hotkey_command(hwnd);
    g.hotkeys = default_hotkeys();
    rebuild_accelerators();
    rebuild_menu_bar();
    if (g_hotkeys_dialog.list && IsWindow(g_hotkeys_dialog.list)) {
        populate_hotkeys_dialog_list(g_hotkeys_dialog.list);
    }
    populate_hotkey_list(hwnd);
    load_selected_hotkey_controls(hwnd);
    save_runtime_settings();
    set_status();
    if (g.main && IsWindow(g.main)) InvalidateRect(g.main, nullptr, TRUE);
    if (selected_command) {
        HWND list = GetDlgItem(hwnd, IDC_SET_HOTKEY_LIST);
        if (list) {
            int count = static_cast<int>(SendMessageW(list, LB_GETCOUNT, 0, 0));
            for (int i = 0; i < count; ++i) {
                if (static_cast<int>(SendMessageW(list, LB_GETITEMDATA, i, 0)) == selected_command) {
                    SendMessageW(list, LB_SETCURSEL, i, 0);
                    load_selected_hotkey_controls(hwnd);
                    break;
                }
            }
        }
    }
}

void populate_hotkey_list(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_SET_HOTKEY_LIST);
    if (!list) return;
    // Refreshing the editor is also a recovery point for a corrupted all-zero
    // table, so the user never sees every shortcut as "Not assigned".
    ensure_hotkeys_initialized();
    int selected_command = settings_selected_hotkey_command(hwnd);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int selected_index = 0;
    const auto order = hotkey_command_order();
    for (std::size_t i = 0; i < order.size(); ++i) {
        std::wstring item = hotkey_list_item_text(order[i]);
        int idx = static_cast<int>(SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str())));
        SendMessageW(list, LB_SETITEMDATA, idx, order[i]);
        if (order[i] == selected_command) selected_index = idx;
    }
    SendMessageW(list, LB_SETCURSEL, selected_index, 0);
    InvalidateRect(list, nullptr, TRUE);
}

} // namespace gui
