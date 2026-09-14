// Playback: native viewer implementation.
#include "gui_playback.hpp"
#include "gui_frf.hpp"
#include "gui_controls.hpp"
#include "gui_ids.hpp"
#include "gui_layout.hpp"
#include "gui_render.hpp"
#include "gui_settings.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"

namespace gui {

const wchar_t* speed_menu_text() {
    return (g_str == &kEn) ? L"Playback speed…" : L"Скорость воспроизведения…";
}

void set_play_speed(double speed) {
    if (!(speed > 0.0) || !std::isfinite(speed)) return;
    if (g.playing) {
        LARGE_INTEGER now{}, freq{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        const double elapsed = static_cast<double>(now.QuadPart - g.play_anchor_qpc.QuadPart) /
                               static_cast<double>(freq.QuadPart);
        g.playhead = g.play_anchor_data + elapsed * g.play_speed;
        if (has_data()) {
            if (g.playhead < g.data_t0) g.playhead = g.data_t0;
            if (g.playhead > g.data_t1) g.playhead = g.data_t1;
        }
        g.play_anchor_data = g.playhead;
        g.play_anchor_qpc = now;
    }
    g.play_speed = speed;
    save_runtime_settings();
    set_status();
}

// ---- playback ------------------------------------------------------------

void stop_play() {
    g.playing = false;
    KillTimer(g.main, 1);
    if (g.play) SetWindowTextW(g.play, g_str->btn_play);
    redraw_button(g.play);
}

void start_play() {
    if (!has_data() || g.mode != AnalysisMode::Time) return;
    g.playing = true;
    g.playhead_active = true;
    if (g.playhead < g.win_start || g.playhead >= g.data_t1) g.playhead = g.win_start;
    // Anchor the playhead to wall-clock time so playback runs at 1 s of signal
    // per 1 s of real time, independent of timer jitter.
    g.play_anchor_data = g.playhead;
    QueryPerformanceCounter(&g.play_anchor_qpc);
    SetTimer(g.main, 1, 16, nullptr);   // ~60 fps for smooth scrolling
    if (g.play) SetWindowTextW(g.play, g_str->btn_pause);
    redraw_button(g.play);
}

void toggle_play() {
    if (g.playing) stop_play();
    else start_play();
}

LRESULT handle_playback_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (LOWORD(wp) == 3) {
                KillTimer(hwnd, 3);
                if (g_settings_dirty) { save_runtime_settings_now(); g_settings_dirty = false; }
                return 0;
            }
            if (LOWORD(wp) == 2) {
                poll_frf_result();
                if (auto result = g_spectrum_worker.take_result()) {
                    if (result->generation == g.spec_generation) {
                        apply_spectrum_result(std::move(result->spectrum));
                        set_status(); invalidate_plot();
                    }
                }
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                HWND new_hover = nullptr;
                for (HWND h : g.buttons) {
                    if (!IsWindowVisible(h)) continue;
                    RECT r;
                    GetWindowRect(h, &r);
                    MapWindowPoints(nullptr, hwnd, (LPPOINT)&r, 2);
                    if (PtInRect(&r, pt)) { new_hover = h; break; }
                }
                if (new_hover != g.hovered_btn) {
                    if (g.hovered_btn) InvalidateRect(g.hovered_btn, nullptr, FALSE);
                    if (new_hover) InvalidateRect(new_hover, nullptr, FALSE);
                    g.hovered_btn = new_hover;
                    // Update hover status description and invalidate status bar
                    g.hover_status_text = toolbar_hover_text(new_hover);
                    RECT rc; GetClientRect(hwnd, &rc);
                    RECT sr = {0, rc.bottom - kBottomBar, rc.right, rc.bottom};
                    InvalidateRect(hwnd, &sr, FALSE);
                }
                return 0;
            }
            if (g.playing && (g.mode == AnalysisMode::Time) && has_data()) {
                // Real-time playhead: 1 s of signal per 1 s of wall-clock time.
                LARGE_INTEGER now, freq;
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&freq);
                const double elapsed =
                    static_cast<double>(now.QuadPart - g.play_anchor_qpc.QuadPart) /
                    static_cast<double>(freq.QuadPart);
                g.playhead = g.play_anchor_data + elapsed * g.play_speed;
                if (g.playhead >= g.data_t1) {
                    g.playhead = g.data_t1;
                    stop_play();
                } else {
                    // Once the playhead passes the middle, keep it centred by
                    // scrolling a little each frame � smooth, no big jumps.
                    const double span = g.win_end - g.win_start;
                    if (g.playhead > g.win_start + span * 0.5) {
                        g.win_start = g.playhead - span * 0.5;
                        g.win_end = g.win_start + span;
                        if (g.win_end > g.data_t1) { g.win_end = g.data_t1; g.win_start = g.win_end - span; }
                        if (g.win_start < g.data_t0) { g.win_start = g.data_t0; g.win_end = g.win_start + span; }
                    }
                }
                set_status();
                RECT pr = plot_rect();
                RECT rc; GetClientRect(hwnd, &rc);
                pr.bottom = rc.bottom; // include status bar
                InvalidateRect(hwnd, &pr, FALSE);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace gui
