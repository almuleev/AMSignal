#include "gui_disk_spectrum_render.hpp"
#include "disk_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_render.hpp"
#include "gui_theme.hpp"
#include "gui_processing.hpp"
#include "gui_text.hpp"
#include "gui_resources.hpp"

namespace gui {
void draw_disk_spectrum(HDC dc,const RECT& p) {
    const int width=p.right-p.left,height=p.bottom-p.top;
    if(width<=0 || height<=0)return;
    double left=g.freq_start,right=g.freq_end;
    if(right<=left){left=0;right=g.spec.nyquist;}
    auto range=g.spec.bin_range(left,right);
    if(range.first)--range.first;
    if(range.second<g.spec.bin_count())++range.second;
    std::shared_ptr<lvm::MemoryBudget::Reservation> guard;
    std::vector<std::vector<std::pair<std::size_t,double>>> curves(g.spec.names.size());double maximum=0;
    try {
        const auto pixels=std::min<std::size_t>(8192,width);
        auto budget=refresh_load_memory_budget();
        guard=budget->reserve(1024*1024+pixels*(sizeof(lvm::DiskSpectrum::Bucket)+64ULL*g.spec.names.size()));
        if(!guard)throw lvm::RecordingFailure(lvm::RecordingError::ResourceLimit,"No budget for spectrum display.");
        for(std::size_t j=0;j<curves.size();++j){const auto ci=g.spec_channel_indices.at(j);if(ci<0 || !g.visible[ci])continue;
            curves[j]=g.spec.disk->display_points(j,range.first,range.second-range.first,
                range.first==0 && range.second==g.spec.bin_count() ? std::min<std::size_t>(1024,pixels) : pixels);
            for(const auto& point:curves[j])maximum=std::max(maximum,point.second);}
    } catch(const std::exception& e) {g.vvalid=false;draw_axes(dc,p,left,right,0,1);RECT message=p;
        const auto text=to_w(e.what());DrawTextW(dc,text.c_str(),-1,&message,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);return;}
    double top=g.auto_y_amp ? (maximum>0 ? maximum*1.08 : 1) : g.y_amp_max;
    draw_axes(dc,p,left,right,0,top);
    const int saved=SaveDC(dc);IntersectClipRect(dc,p.left+1,p.top+1,p.right,p.bottom);
    for(std::size_t j=0;j<curves.size();++j){if(curves[j].empty())continue;const auto ci=g.spec_channel_indices.at(j);
        const bool dense=range.second-range.first>static_cast<std::size_t>(width)*2;
        HPEN pen=CreatePen(g.distinguish_curves && !dense ? curve_pen_style(ci) : PS_SOLID,1,channel_color(ci));auto old=SelectObject(dc,pen);bool first=true;
        for(const auto& point:curves[j]){const int x=p.left+static_cast<int>((g.spec.frequency(point.first)-left)/(right-left)*width);
            const int y=p.bottom-static_cast<int>(point.second/top*height);
            if(first){MoveToEx(dc,x,y,nullptr);first=false;}else LineTo(dc,x,y);}
        SelectObject(dc,old);DeleteObject(pen);}
    RestoreDC(dc,saved);draw_legend(dc,p);
    g.vx0=left;g.vx1=right;g.vy0=0;g.vy1=top;g.vrect=p;g.vvalid=true;
}
}
