#include "gui_analysis_range.hpp"
#include "gui_analysis_source.hpp"
#include "gui_controls.hpp"
#include "gui_layout.hpp"
#include "gui_input.hpp"
#include "gui_overview.hpp"
#include "gui_state.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"
#include "gui_time_axis.hpp"

namespace gui {
namespace {
HWND hover_owner=nullptr;
POINT hover_position{-1,-1};
int hover_edge=0;
bool range_cursor_target(int x,int y,double& time,double& value) {
    const RECT plot=plot_rect();
    if(plot.right<=plot.left)return false;
    const int clamped_x=std::clamp<int>(x,plot.left,plot.right);
    const double fraction=static_cast<double>(clamped_x-plot.left)/(plot.right-plot.left);
    time=raw_time_from_stitched(stitched_time_from_raw(g.win_start)+fraction*
        (stitched_time_from_raw(g.win_end)-stitched_time_from_raw(g.win_start)));
    value=std::numeric_limits<double>::quiet_NaN();
    double unused=0;
    if(g.vvalid && g.vrect.bottom>g.vrect.top)
        px_to_data(clamped_x,std::clamp<int>(y,g.vrect.top,g.vrect.bottom),unused,value);
    return std::isfinite(time);
}
void commit_range_edge(double anchor,double time) {
    const double a=std::min(anchor,time),b=std::max(anchor,time);
    if(b>a && (a!=g.fft_window_start || b!=g.fft_window_end))
        change_analysis_range(lvm::AnalysisRange::selected(a,b));
    set_status();
}
}

int analysis_range_edge_x(double time,const RECT& plot) {
    if(time<g.win_start || time>g.win_end || plot.right-plot.left<12)return -1;
    const double start=stitched_time_from_raw(g.win_start),end=stitched_time_from_raw(g.win_end);
    if(!(end>start))return -1;
    const double fraction=(stitched_time_from_raw(time)-start)/(end-start);
    if(!std::isfinite(fraction))return -1;
    return std::clamp(static_cast<int>(plot.left)+static_cast<int>(std::clamp(fraction,0.0,1.0)*(plot.right-plot.left)),
        static_cast<int>(plot.left)+4,static_cast<int>(plot.right)-4);
}

int hit_test_analysis_range_edge(int x,int y) {
    if(g.mode!=AnalysisMode::Time || !has_fft_window())return 0;
    const RECT plot=plot_rect();
    if(x<plot.left || x>plot.right || y<plot.top || y>plot.bottom)return 0;
    const int left=analysis_range_edge_x(g.fft_window_start,plot);
    const int right=analysis_range_edge_x(g.fft_window_end,plot);
    const int dl=left>=0 ? std::abs(x-left) : 100000;
    const int dr=right>=0 ? std::abs(x-right) : 100000;
    if(std::min(dl,dr)>7)return 0;
    return dl<=dr ? 1 : 2;
}

void update_analysis_range_hover(HWND hwnd,int x,int y) {
    const int edge=(!g.pending_line && !g.pending_marker && !g.dragging) ? hit_test_analysis_range_edge(x,y) : 0;
    hover_owner=hwnd;hover_position={x,y};
    if(edge!=hover_edge) {hover_edge=edge;InvalidateRect(hwnd,nullptr,FALSE);}
    if(edge) {TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);}
}

std::wstring analysis_range_caption_text() {
    if(g.mode!=AnalysisMode::Time || (!g.fft_selecting && !has_fft_window()))return {};
    const double a=g.fft_selecting ? std::min(g.fft_select_anchor_t,g.fft_select_current_t) : g.fft_window_start;
    const double b=g.fft_selecting ? std::max(g.fft_select_anchor_t,g.fft_select_current_t) : g.fft_window_end;
    wchar_t value[192]{};
    swprintf(value,192,g_str==&kEn ? L"Analysis interval: %.6g–%.6g s · Δt %.6g s" :
        L"Участок анализа: %.6g–%.6g с · Δt %.6g с",a,b,b-a);
    return std::wstring(value)+(g.stitch_time_gaps ? (g_str==&kEn ? L" (source)" : L" (исх.)") : L"");
}

void draw_analysis_range_selection(HDC dc,const RECT& plot) {
    if(g.mode!=AnalysisMode::Time || (!g.fft_selecting && !has_fft_window()))return;
    const double a=g.fft_selecting ? std::min(g.fft_select_anchor_t,g.fft_select_current_t) : g.fft_window_start;
    const double b=g.fft_selecting ? std::max(g.fft_select_anchor_t,g.fft_select_current_t) : g.fft_window_end;
    if(plot.right-plot.left<12 || plot.bottom-plot.top<20)return;
    const double start=stitched_time_from_raw(g.win_start),end=stitched_time_from_raw(g.win_end);
    if(!(end>start))return;
    const int saved=SaveDC(dc);IntersectClipRect(dc,plot.left+1,plot.top+1,plot.right,plot.bottom);
    const auto map=[&](double time) {
        return plot.left+static_cast<int>(std::clamp((stitched_time_from_raw(time)-start)/(end-start),0.0,1.0)*(plot.right-plot.left));
    };
    // Mark only the excluded area; never fill or tint the working interval.
    HPEN hatch_pen=CreatePen(PS_SOLID,1,g_theme==&kDarkTheme ? RGB(92,92,92) : RGB(180,180,180));
    const auto old_hatch=SelectObject(dc,hatch_pen);
    const auto hatch=[&](int left,int right) {
        if(right<=left)return;
        const int band=SaveDC(dc);IntersectClipRect(dc,left,plot.top+1,right,plot.bottom);
        {
            Gdiplus::Graphics graphics(dc);
            Gdiplus::SolidBrush shade(Gdiplus::Color(g_theme==&kDarkTheme ? 44 : 20,0,0,0));
            graphics.FillRectangle(&shade,left,static_cast<int>(plot.top)+1,right-left,static_cast<int>(plot.bottom-plot.top)-1);
        }
        const int height=plot.bottom-plot.top;
        for(int x=plot.left-height;x<plot.right;x+=18) {
            MoveToEx(dc,x,plot.bottom,nullptr);LineTo(dc,x+height,plot.top);
        }
        RestoreDC(dc,band);
    };
    hatch(plot.left+1,map(a));
    hatch(map(b)+1,plot.right);
    SelectObject(dc,old_hatch);DeleteObject(hatch_pen);
    int index=0;
    for(double edge:{a,b}) {
        ++index;
        const int x=analysis_range_edge_x(edge,plot);if(x<0)continue;
        const bool active=g.fft_resize_edge ? edge==g.fft_select_current_t :
            hover_owner==g.main && hover_edge==index && !g.pending_line && !g.pending_marker &&
            hit_test_analysis_range_edge(hover_position.x,hover_position.y)==index;
        for(int layer=0;layer<2;++layer) {
            HPEN pen=CreatePen(PS_SOLID,(active ? 3 : 2)+(layer==0 ? 2 : 0),layer==0 ? RGB(255,255,255) : g_theme->accent);
            const auto old=SelectObject(dc,pen);
            MoveToEx(dc,x,plot.top+2,nullptr);LineTo(dc,x,plot.bottom-2);
            SelectObject(dc,old);DeleteObject(pen);
        }
        for(int y:{static_cast<int>(plot.top)+3,static_cast<int>(plot.bottom)-19}) {
            RECT handle{x-5,y,x+6,y+16};
            fill_rounded_rect(dc,handle,g_theme->accent,RGB(255,255,255),4);
            HPEN grip=CreatePen(PS_SOLID,1,RGB(255,255,255));const auto old=SelectObject(dc,grip);
            for(int dx:{-1,2}) {MoveToEx(dc,x+dx,y+5,nullptr);LineTo(dc,x+dx,y+11);}
            SelectObject(dc,old);DeleteObject(grip);
        }
    }
    RestoreDC(dc,saved);
}

bool begin_analysis_range_edge_drag(HWND hwnd,int x,int y) {
    const int edge=hit_test_analysis_range_edge(x,y);if(!edge)return false;
    cancel_overview_snap();
    g.fft_resize_edge=edge;g.fft_selecting=true;
    g.fft_select_anchor_t=edge==1 ? g.fft_window_end : g.fft_window_start;
    g.fft_select_current_t=edge==1 ? g.fft_window_start : g.fft_window_end;
    g.fft_select_anchor_x=g.fft_select_current_x=x;
    g.drag_y=y;
    g.dragging=false;g.point_click_pending=false;g.gap_click_pending=false;
    SetCapture(hwnd);SetCursor(LoadCursor(nullptr,IDC_SIZEWE));
    InvalidateRect(hwnd,nullptr,FALSE);return true;
}

void update_analysis_range_edge_drag(HWND hwnd,int x,int y) {
    double time=0,value=0;if(!range_cursor_target(x,y,time,value))return;
    const RECT plot=plot_rect();g.fft_select_current_x=std::clamp<int>(x,plot.left,plot.right);
    g.fft_select_current_t=time;
    if(g.snap_to_data && std::isfinite(value) && std::any_of(g.visible.begin(),g.visible.end(),[](char v){return v!=0;})) {
        if(g.overview.render_enabled) {
            const auto anchor=g.fft_select_anchor_t;const int edge=g.fft_resize_edge;
            request_overview_snap(time,value,[hwnd,anchor,edge,time](const auto& point) {
                if(g.fft_selecting && g.fft_resize_edge==edge && g.fft_select_anchor_t==anchor) {
                    g.fft_select_current_t=point.found ? point.time : time;
                    InvalidateRect(hwnd,nullptr,FALSE);
                }
            },true);
        } else if(snap_to_nearest_target(time,value))g.fft_select_current_t=time;
    }
    InvalidateRect(hwnd,nullptr,FALSE);
}

void finish_analysis_range_edge_drag(HWND hwnd,int x,int y) {
    const double anchor=g.fft_select_anchor_t;
    const bool moved=x!=g.fft_select_anchor_x || y!=g.drag_y;
    cancel_overview_snap();
    g.fft_selecting=false;g.fft_resize_edge=0;
    if(GetCapture()==hwnd)ReleaseCapture();
    double time=0,value=0;
    if(moved && range_cursor_target(x,y,time,value)) {
        if(g.snap_to_data && std::isfinite(value) && g.overview.render_enabled &&
            std::any_of(g.visible.begin(),g.visible.end(),[](char v){return v!=0;})) {
            // Commit only the final exact original-source result, never a display bucket.
            request_overview_snap(time,value,[anchor,time](const auto& point) {
                if(!g.fft_selecting)commit_range_edge(anchor,point.found ? point.time : time);
            },true);
        } else {
            if(g.snap_to_data && std::isfinite(value))snap_to_nearest_target(time,value);
            g.fft_select_current_t=time;commit_range_edge(anchor,time);
        }
    }
    set_status();InvalidateRect(hwnd,nullptr,FALSE);
}

void cancel_analysis_range_drag(HWND hwnd) {
    cancel_overview_snap();
    g.fft_selecting=false;g.fft_resize_edge=0;
    if(GetCapture()==hwnd)ReleaseCapture();
    InvalidateRect(hwnd,nullptr,FALSE);
}
}
