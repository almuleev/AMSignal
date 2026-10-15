#include "gui_marker_values.hpp"
#include "gui_commands.hpp"
#include "gui_controls.hpp"
#include "gui_frf_render.hpp"
#include "gui_ids.hpp"
#include "gui_input.hpp"
#include "gui_processing.hpp"
#include "gui_render.hpp"
#include "gui_state.hpp"
#include "gui_status.hpp"
#include "gui_text.hpp"
#include "gui_time_axis.hpp"

namespace gui {
namespace {
std::wstring coordinate(const wchar_t* label,double value,const std::wstring& unit) {
    wchar_t number[64]{};swprintf(number,64,L"%.9g",value==0 ? 0.0 : value);
    return std::wstring(label)+L" = "+number+(unit.empty() ? L"" : L" "+unit);
}
HFONT panel_font() {
    return g.axis_font ? g.axis_font : g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}
}

bool marker_value_panel(HDC dc,MarkerValuePanel& panel) {
    panel={};
    if(!g.show_marker_values || !g.vvalid || g.vx1<=g.vx0 || g.vy1<=g.vy0)return false;
    int index=g.active_marker;
    const auto valid=[&](int i){return i>=0 && static_cast<std::size_t>(i)<g.markers.size() && g.markers[i].mode==g.mode;};
    if(!valid(index))for(std::size_t i=g.markers.size();i>0;--i)if(valid(static_cast<int>(i-1))) {index=static_cast<int>(i-1);break;}
    if(!valid(index))return false;
    const auto& marker=g.markers[index];
    if(!std::isfinite(marker.x) || !std::isfinite(marker.y))return false;
    const double fraction=g.mode==AnalysisMode::FRF ? frf_frequency_fraction(marker.x) :
        ((g.mode==AnalysisMode::Time ? stitched_time_from_raw(marker.x) : marker.x)-g.vx0)/(g.vx1-g.vx0);
    if(!std::isfinite(fraction) || fraction<0 || fraction>1)return false;
    const RECT plot=g.vrect;const int width=plot.right-plot.left,height=plot.bottom-plot.top;
    panel.marker=index;
    panel.anchor={plot.left+static_cast<int>(fraction*width),
        plot.bottom-static_cast<int>(std::clamp((marker.y-g.vy0)/(g.vy1-g.vy0),0.0,1.0)*height)};
    panel.title=marker.label.empty() ? L"M"+std::to_wstring(index+1) : marker.label;
    if(marker.snapped && marker.channel>=0)panel.title+=L" · "+channel_display_label(marker.channel);
    for(auto& ch:panel.title)if(ch==L'\n' || ch==L'\r' || ch==L'\t')ch=L' ';
    const bool en=g_str==&kEn;
    panel.x=g.mode==AnalysisMode::Time
        ? coordinate(g.stitch_time_gaps ? (en ? L"t (source)" : L"t (исх.)") : L"t",marker.x,en ? L"s" : L"с")
        : coordinate(L"f",marker.x,en ? L"Hz" : L"Гц");
    panel.y=coordinate(g.mode==AnalysisMode::FRF ? L"|H|" : g.mode==AnalysisMode::FFT ? L"A" : L"Y",
        marker.y,g.mode==AnalysisMode::FRF ? L"" : vertical_axis_unit());
    const auto old=SelectObject(dc,panel_font());TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
    int text_width=0;
    for(const auto* line:{&panel.title,&panel.x,&panel.y}) {
        SIZE size{};GetTextExtentPoint32W(dc,line->c_str(),static_cast<int>(line->size()),&size);
        text_width=std::max(text_width,static_cast<int>(size.cx)+(line==&panel.title ? 22 : 0));
    }
    SelectObject(dc,old);
    const int card_width=std::min({std::max(156,text_width+20),320,width-16});
    const int card_height=3*(metrics.tmHeight+3)+16;
    if(card_width<120 || height<card_height+16)return false;
    int x=panel.anchor.x+14,y=panel.anchor.y-card_height-12;
    if(x+card_width>plot.right-8)x=panel.anchor.x-card_width-14;
    if(y<plot.top+8)y=panel.anchor.y+12;
    x=std::clamp(x,static_cast<int>(plot.left)+8,static_cast<int>(plot.right)-card_width-8);
    y=std::clamp(y,static_cast<int>(plot.top)+8,static_cast<int>(plot.bottom)-card_height-8);
    panel.bounds={x,y,x+card_width,y+card_height};panel.close={x+card_width-24,y+4,x+card_width-4,y+24};
    return true;
}

void draw_marker_values(HDC dc) {
    g.marker_values_rect={};g.marker_values_close_rect={};
    MarkerValuePanel panel;if(!marker_value_panel(dc,panel))return;
    const int saved=SaveDC(dc);IntersectClipRect(dc,g.vrect.left+1,g.vrect.top+1,g.vrect.right,g.vrect.bottom);
    fill_rounded_rect(dc,panel.bounds,g_theme->bg_panel,g_theme->btn_border,6);
    SelectObject(dc,panel_font());SetBkMode(dc,TRANSPARENT);SetTextAlign(dc,TA_LEFT|TA_TOP);
    TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);int top=panel.bounds.top+8;
    for(const auto* line:{&panel.title,&panel.x,&panel.y}) {
        RECT text{panel.bounds.left+10,top,panel.bounds.right-(line==&panel.title ? 28 : 10),top+metrics.tmHeight+2};
        SetTextColor(dc,line==&panel.title ? g_theme->text_secondary : g_theme->text_primary);
        DrawTextW(dc,line->c_str(),-1,&text,DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);top+=metrics.tmHeight+3;
    }
    const int x=(panel.close.left+panel.close.right)/2,y=(panel.close.top+panel.close.bottom)/2;
    HPEN pen=CreatePen(PS_SOLID,1,g_theme->text_secondary);const auto old=SelectObject(dc,pen);
    MoveToEx(dc,x-3,y-3,nullptr);LineTo(dc,x+4,y+4);MoveToEx(dc,x+3,y-3,nullptr);LineTo(dc,x-4,y+4);
    SelectObject(dc,old);DeleteObject(pen);RestoreDC(dc,saved);
    g.marker_values_rect=panel.bounds;g.marker_values_close_rect=panel.close;
}

bool marker_values_hit_test(int x,int y) {
    return g.show_marker_values && g.vvalid && PtInRect(&g.marker_values_rect,POINT{x,y});
}
bool handle_marker_values_click(HWND hwnd,int x,int y) {
    if(!marker_values_hit_test(x,y))return false;
    if(PtInRect(&g.marker_values_close_rect,POINT{x,y}))handle_commands_message(hwnd,WM_COMMAND,IDM_MARKER_VALUES,0);
    return true;
}
void update_marker_values_hover(HWND hwnd,int x,int y) {
    if(marker_values_hit_test(x,y))return;
    const int marker=hit_test_marker(x,y);
    if(marker>=0 && marker!=g.active_marker) {
        g.active_marker=marker;set_status();InvalidateRect(hwnd,nullptr,FALSE);
    }
}
}
