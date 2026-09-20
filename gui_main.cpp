// Main: native viewer implementation.
#include "gui_main.hpp"
#include "gui_hotkeys.hpp"
#include "gui_settings_window.hpp"
#include "gui_commands.hpp"
#include "gui_documents.hpp"
#include "gui_ids.hpp"
#include "gui_input.hpp"
#include "gui_loading.hpp"
#include "gui_playback.hpp"
#include "gui_settings.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"
#include "gui_welcome.hpp"
#include "gui_window.hpp"

namespace gui {

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
        case WM_SIZE:
        case WM_GETMINMAXINFO:
        case WM_ERASEBKGND:
        case WM_PARENTNOTIFY:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLOREDIT:
        case WM_MEASUREITEM:
        case WM_PAINT:
        case WM_DRAWITEM:
        case WM_DESTROY:
            return handle_window_message(hwnd, msg, wp, lp);
        case WM_CLOSE:
            request_application_close(hwnd);
            return 0;
        case WM_HSCROLL:
        case WM_COMMAND:
            return handle_commands_message(hwnd, msg, wp, lp);
        case WM_TIMER:
            return handle_playback_message(hwnd, msg, wp, lp);
        case WM_CANCELMODE:
        case WM_SETCURSOR:
        case WM_MOUSEWHEEL:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        case WM_KEYDOWN:
            return handle_input_message(hwnd, msg, wp, lp);
        case WM_APP_ASYNC_SCAN_DONE:
        case WM_APP_ASYNC_LOAD_DONE:
        case WM_DROPFILES:
            return handle_loading_message(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace gui

using namespace gui;


int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int show) {
    Gdiplus::GdiplusStartupInput gdi_in;
    Gdiplus::GdiplusStartup(&g_gdiplus_token, &gdi_in, nullptr);

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    load_app_settings();
    load_runtime_settings();
    register_project_file_association();
    load_program_logo();
    HICON class_icon = g_program_logo_icon ? g_program_logo_icon : LoadIcon(nullptr, IDI_APPLICATION);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"LvmViewerWnd";
    wc.hIcon = class_icon;
    wc.hIconSm = class_icon;
    RegisterClassExW(&wc);

    // Settings panel + welcome screen window classes.
    WNDCLASSEXW sc = {};
    sc.cbSize = sizeof(sc);
    sc.lpfnWndProc = SettingsProc;
    sc.hInstance = inst;
    sc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    sc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    sc.lpszClassName = L"LvmPtSettings";
    sc.hIcon = class_icon;
    sc.hIconSm = class_icon;
    RegisterClassExW(&sc);

    WNDCLASSEXW wcw = {};
    wcw.cbSize = sizeof(wcw);
    wcw.lpfnWndProc = WelcomeProc;
    wcw.hInstance = inst;
    wcw.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcw.hbrBackground = nullptr;
    wcw.lpszClassName = L"LvmWelcome";
    wcw.hIcon = class_icon;
    wcw.hIconSm = class_icon;
    RegisterClassExW(&wcw);

    g.main = CreateWindowExW(0, wc.lpszClassName, g_str->app_title,
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 720,
                             nullptr, nullptr, inst, nullptr);
    if (!g.main) return 1;
    SendMessageW(g.main, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(class_icon));
    SendMessageW(g.main, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(class_icon));
    ShowWindow(g.main, show);
    UpdateWindow(g.main);
    DragAcceptFiles(g.main, TRUE);

    if (cmd && *cmd) {
        std::wstring path = cmd;
        if (!path.empty() && path.front() == L'"') path = path.substr(1, path.find_last_of(L'"') - 1);
        if (!load_path_interactive(path) && !g.last_error.empty())
            MessageBoxW(g.main, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
    } else {
        show_welcome(inst);   // start screen when launched without a file
    }

    ensure_hotkeys_initialized();
    rebuild_accelerators();
    MSG m;
    while (GetMessage(&m, nullptr, 0, 0) > 0) {
        if (should_bypass_accelerators() || !g.accel || !TranslateAcceleratorW(g.main, g.accel, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    if (g.accel) DestroyAcceleratorTable(g.accel);
    unload_program_logo();
    Gdiplus::GdiplusShutdown(g_gdiplus_token);
    return 0;
}
