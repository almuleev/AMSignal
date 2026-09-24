// Loading drop: native viewer implementation.
#include "gui_loading_drop.hpp"
#include "gui_controls.hpp"
#include "gui_dialogs.hpp"
#include "gui_ids.hpp"
#include "gui_loading.hpp"
#include "gui_documents.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"
#include "gui_welcome.hpp"

namespace gui {

HWND g_loading_wnd = nullptr;

HWND g_loading_cancel_btn = nullptr;

std::wstring g_loading_text;

bool g_loading_cancellable = false;

LRESULT CALLBACK LoadingProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_LOADING_CANCEL && HIWORD(wp) == BN_CLICKED) {
                request_async_load_cancel();
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);

            HBRUSH outer = CreateSolidBrush(g_theme->bg_main);
            FillRect(dc, &rc, outer);
            DeleteObject(outer);

            RECT card = rc;
            InflateRect(&card, -2, -2);
            fill_rounded_rect(dc, card, g_theme->bg_panel, g_theme->separator, 12);

            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ old_font = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);

            RECT text_rect = { card.left + 18, card.top + 18, card.right - 18, card.bottom - 18 };
            if (g_loading_cancellable) text_rect.bottom -= 42;
            DrawTextW(dc, g_loading_text.c_str(), -1, &text_rect,
                      DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);

            SelectObject(dc, old_font);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void show_loading(const std::wstring& msg, bool cancellable) {
    if (g_loading_wnd) { DestroyWindow(g_loading_wnd); g_loading_wnd = nullptr; }
    g_loading_cancel_btn = nullptr;
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
    std::wstring display = msg;
    for (std::size_t i = 0; i < display.size(); ++i) {
        if (display[i] == L'\n' && (i == 0 || display[i - 1] != L'\r')) {
            display.insert(i, 1, L'\r');
            ++i;
        }
    }
    g_loading_text = display;
    g_loading_cancellable = cancellable;

    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = LoadingProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_WAIT);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"LvmLoadingOverlay";
        atom = RegisterClassExW(&wc);
    }

    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HDC screen = GetDC(nullptr);
    HGDIOBJ old_font = SelectObject(screen, font);
    RECT text_rect = { 0, 0, 280, 0 };
    DrawTextW(screen, g_loading_text.c_str(), -1, &text_rect,
              DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(screen, old_font);
    ReleaseDC(nullptr, screen);

    const int text_width = static_cast<int>(text_rect.right - text_rect.left);
    const int text_height = static_cast<int>(text_rect.bottom - text_rect.top);
    const int width = std::clamp(text_width + 52, 236, 340);
    const int button_extra = cancellable ? 44 : 0;
    const int height = std::clamp(text_height + 42 + button_extra, 76, 164);
    g_loading_wnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"LvmLoadingOverlay", L"",
        WS_POPUP | WS_VISIBLE,
        0, 0, width, height, g.main, nullptr, inst, nullptr);
    if (!g_loading_wnd) return;
    RECT mr, wr;
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_loading_wnd, &wr);
    SetWindowPos(g_loading_wnd, HWND_TOPMOST,
                 mr.left + ((mr.right - mr.left) - (wr.right - wr.left)) / 2,
                 mr.top + ((mr.bottom - mr.top) - (wr.bottom - wr.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    if (cancellable) {
        g_loading_cancel_btn = CreateWindowExW(
            0, L"BUTTON", speed_prompt_cancel_text(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 92, 28, g_loading_wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOADING_CANCEL)),
            inst, nullptr);
        if (g_loading_cancel_btn) {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            SendMessageW(g_loading_cancel_btn, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            MoveWindow(g_loading_cancel_btn, (width - 92) / 2, height - 38, 92, 26, FALSE);
        }
    }
    if (g.main && IsWindow(g.main)) EnableWindow(g.main, FALSE);
    UpdateWindow(g_loading_wnd);
}

void hide_loading() {
    g_loading_cancel_btn = nullptr;
    g_loading_cancellable = false;
    if (g_loading_wnd) { DestroyWindow(g_loading_wnd); g_loading_wnd = nullptr; }
    if (g.main && IsWindow(g.main)) EnableWindow(g.main, TRUE);
    raise_main_window();
}

LRESULT CALLBACK drop_forward_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR, DWORD_PTR) {
    if (msg == WM_DROPFILES) {
        HWND parent = GetParent(hwnd);
        if (parent) SendMessageW(parent, WM_DROPFILES, wp, lp);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, drop_forward_subclass_proc, kDropForwardSubclassId);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

BOOL CALLBACK enable_file_drop_child_proc(HWND child, LPARAM) {
    if (!child || !IsWindow(child)) return TRUE;
    DragAcceptFiles(child, TRUE);
    if (GetParent(child)) {
        SetWindowSubclass(child, drop_forward_subclass_proc, kDropForwardSubclassId, 0);
    }
    return TRUE;
}

void enable_file_drop_support(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    DragAcceptFiles(hwnd, TRUE);
    EnumChildWindows(hwnd, enable_file_drop_child_proc, 0);
}

void handle_file_drop([[maybe_unused]] HWND hwnd, HDROP hDrop) {
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    if (count > 0) {
        std::vector<std::wstring> paths;
        paths.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            const UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
            if (len == 0) continue;
            std::vector<wchar_t> path(static_cast<std::size_t>(len) + 1, L'\0');
            if (DragQueryFileW(hDrop, i, path.data(), static_cast<UINT>(path.size()))) {
                paths.emplace_back(path.data());
            }
        }
        queue_open_paths(std::move(paths));
    }
    DragFinish(hDrop);
}

} // namespace gui
