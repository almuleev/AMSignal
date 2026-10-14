// Playback: native viewer implementation.
#include "gui_playback.hpp"
#include "gui_frf.hpp"
#include "gui_controls.hpp"
#include "gui_documents.hpp"
#include "gui_ids.hpp"
#include "gui_layout.hpp"
#include "gui_menu.hpp"
#include "gui_time_axis.hpp"
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
    mark_active_document_dirty();
    save_runtime_settings();
    set_status();
}

// ---- playback ------------------------------------------------------------

void stop_play() {
    g.playing = false;
    const bool captured=g.playhead_dragging;
    g.playhead_dragging=false;g.playhead_selected=false;
    if(captured && GetCapture()==g.main)ReleaseCapture();
    KillTimer(g.main, 1);
    if (g.play) SetWindowTextW(g.play, g_str->btn_play);
    redraw_button(g.play);
    sync_menu();
}

void reset_playback() {
    stop_play();g.playhead_active=false;g.playhead=g.data_t0;
    set_status();invalidate_plot();sync_menu();
}

void seek_playhead(double time) {
    if(!has_data() || g.mode!=AnalysisMode::Time || !std::isfinite(time))return;
    g.playhead=std::clamp(time,g.data_t0,g.data_t1);g.playhead_active=true;
    g.play_anchor_data=g.playhead;QueryPerformanceCounter(&g.play_anchor_qpc);
    set_status();invalidate_plot();
}

bool hit_test_playhead(int x,int y) {
    if(g.mode!=AnalysisMode::Time || !g.playhead_active || !g.vvalid ||
       g.playhead<g.win_start || g.playhead>g.win_end || g.vx1<=g.vx0 ||
       x<g.vrect.left || x>g.vrect.right || y<g.vrect.top || y>g.vrect.bottom)return false;
    const auto fraction=(stitched_time_from_raw(g.playhead)-g.vx0)/(g.vx1-g.vx0);
    return std::fabs(x-(g.vrect.left+fraction*(g.vrect.right-g.vrect.left)))<=6;
}

void start_play() {
    if (!has_data() || g.mode != AnalysisMode::Time || !g.main) return;
    if(g.playhead_dragging)stop_play();
    g.playhead_selected=false;
    if(g.playhead>=g.data_t1) {
        const auto span=g.win_end-g.win_start;
        g.playhead=g.data_t0;g.win_start=g.data_t0;
        g.win_end=std::min(g.data_t1,g.data_t0+span);
    } else if(!g.playhead_active) {
        g.playhead=g.win_start;
    } else if(g.playhead<g.win_start || g.playhead>g.win_end) {
        const auto span=g.win_end-g.win_start;
        g.win_start=std::clamp(g.playhead-span*.5,g.data_t0,std::max(g.data_t0,g.data_t1-span));
        g.win_end=std::min(g.data_t1,g.win_start+span);
    }
    g.playing = true;
    g.playhead_active = true;
    // Anchor the playhead to wall-clock time so playback runs at 1 s of signal
    // per 1 s of real time, independent of timer jitter.
    g.play_anchor_data = g.playhead;
    QueryPerformanceCounter(&g.play_anchor_qpc);
    if(!SetTimer(g.main,1,16,nullptr))g.playing=false;
    if (g.play) SetWindowTextW(g.play, g.playing ? g_str->btn_pause : g_str->btn_play);
    redraw_button(g.play);
    sync_menu();set_status();invalidate_plot();
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
            if (wp==1 && g.playing && (g.mode == AnalysisMode::Time) && has_data()) {
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
