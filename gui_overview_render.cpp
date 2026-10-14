#include "gui_overview_render.hpp"
#include "gui_overview.hpp"
#include "gui_render.hpp"
#include "gui_processing.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"
#include "gui_time_axis.hpp"
#include "gui_analysis_source.hpp"
#include "gap_details.hpp"
#include "gui_controls.hpp"
#include <cmath>
#include <limits>

namespace gui {
namespace {
std::wstring pending_signal_message(bool refining) {
    const bool en=g_str==&kEn;
    auto message=g.noise_threshold_enabled
        ? (en ? L"Applying filter and preparing signal view…" : L"Применение фильтра и подготовка графика…")
        : (refining ? (en ? L"Refining signal view…" : L"Уточнение графика сигнала…")
                    : (en ? L"Preparing signal view…" : L"Подготовка графика сигнала…"));
    std::wstring text=message;
    if(g.overview.progress_percent>=0)text+=L" "+std::to_wstring(g.overview.progress_percent)+L"%";
    return text;
}
}
void visit_overview_curve(std::size_t channel,double threshold,
                          const std::function<void(OverviewCurvePoint)>& consume) {
    if(!consume || !overview_view_can_render() ||
       channel>=g.visible.size() || !g.visible[channel])return;
    const auto at=std::find(g.overview.channels.begin(),g.overview.channels.end(),channel);
    if(at==g.overview.channels.end())return;
    const auto c=static_cast<std::size_t>(at-g.overview.channels.begin());
    const bool affine=g.overview.source->processing_identity().empty() && g.channel_transform_kind[channel]==TransformRuntimeKind::Affine;
    const double mul=affine ? g.channel_transform_mul[channel] : 1,add=affine ? g.channel_transform_add[channel] : 0;
    bool previous_safe=false;double previous_time=0;std::uint64_t previous_end=0;
    for(const auto& bucket:g.overview.view->buckets) {
        const auto& trace=bucket.channels.at(c);
        if(affine && trace.finite_count &&
           (!std::isfinite(trace.minimum.value*mul+add) || !std::isfinite(trace.maximum.value*mul+add))) {
            previous_safe=false;continue;
        }
        const bool safe=!bucket.discontinuous(c,threshold);
        const bool join=previous_safe && safe && previous_end==bucket.first_sample &&
                        bucket.first_time-previous_time<=threshold;
        const auto points=trace.ordered_points();bool first=true;
        for(std::size_t k=0;k<points.second;++k) {
            const auto& point=points.first[k];const double value=affine ? point.value*mul+add : point.value;
            if(!std::isfinite(value)){first=true;previous_safe=false;continue;}
            consume({point.time,value,safe && (first ? join : true),point.sample});first=false;
        }
        previous_safe=safe && !first;
        previous_time=bucket.last_time;previous_end=bucket.first_sample+bucket.sample_count;
    }
}
void draw_overview_time(HDC dc,const RECT& p) {
    g.visible_gap_markers.clear();g.vvalid=false;
    g_legend_items.clear();g_legend_box={};g_legend_close_box={};
    const int pw=p.right-p.left,ph=p.bottom-p.top;
    if(pw<=0 || ph<=0 || g.win_end<=g.win_start || !std::isfinite(g.win_end-g.win_start))return;
    synchronize_overview_viewport(static_cast<std::size_t>(pw));
    double low=-1,high=1;
    const bool ready=overview_time_yrange(low,high);
    if(!g.auto_y && std::isfinite(g.y_lock_min) && std::isfinite(g.y_lock_max) &&
       std::isfinite(g.y_lock_max-g.y_lock_min) && g.y_lock_max>g.y_lock_min) {
        low=g.y_lock_min;high=g.y_lock_max;
    }
    const int saved=SaveDC(dc);
    const auto displayed_start=stitched_time_from_raw(g.win_start),displayed_end=stitched_time_from_raw(g.win_end);
    draw_axes(dc,p,displayed_start,displayed_end,low,high);
    if(!ready) {
        std::wstring message;
        if(g.overview.error==lvm::RecordingError::SourceChanged)message=g_str==&kEn ? L"The source changed. Reopen the recording."
                                                                                     : L"Источник изменился. Откройте запись заново.";
        else if(g.overview.error==lvm::RecordingError::ResourceLimit)message=g_str==&kEn ? L"Insufficient resources for this overview."
                                                                                     : L"Недостаточно ресурсов для этого обзора.";
        else if(g.overview.error)message=g_str==&kEn ? L"The overview could not be read." : L"Не удалось прочитать обзор.";
        else if(g.overview.pending)message=pending_signal_message(false);
        else message=g_str==&kEn ? L"No overview available for this range." : L"Обзор этого участка недоступен.";
        RECT label=p;InflateRect(&label,-24,-24);
        SetTextAlign(dc,TA_LEFT|TA_TOP);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,g_theme->text_primary);
        DrawTextW(dc,message.c_str(),-1,&label,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
        RestoreDC(dc,saved);return;
    }
    const auto mapx=[&](double t) {
        const double fraction=(stitched_time_from_raw(t)-displayed_start)/(displayed_end-displayed_start);
        return p.left+static_cast<int>(std::clamp(fraction*pw,-16384.0,16384.0));
    };
    const auto sample_x=[&](std::uint64_t sample,double raw) {
        const auto time=g.stitch_time_gaps ? g.overview.index->stitched_sample_time(sample) : raw;
        return p.left+static_cast<int>(std::clamp((time-displayed_start)/(displayed_end-displayed_start)*pw,-16384.0,16384.0));
    };
    const auto mapy=[&](double v) {
        const long double fraction=(static_cast<long double>(v)-low)/(static_cast<long double>(high)-low);
        return p.bottom-static_cast<int>(std::clamp(fraction*ph,-16384.0L,16384.0L));
    };
    IntersectClipRect(dc,p.left+1,p.top+1,p.right,p.bottom);
    ensure_global_gap_step_ready();
    const double threshold=!g.stitch_time_gaps && g.cached_global_gap_step>0 ? g.cached_global_gap_step*64 : std::numeric_limits<double>::infinity();
    if(has_fft_window() || g.fft_selecting) {
        const auto a=g.fft_selecting ? std::min(g.fft_select_anchor_t,g.fft_select_current_t) : g.fft_window_start;
        const auto b=g.fft_selecting ? std::max(g.fft_select_anchor_t,g.fft_select_current_t) : g.fft_window_end;
        RECT selected={std::max(p.left,mapx(a)),p.top+1,std::min(p.right,mapx(b)),p.bottom-1};
        if(selected.right>selected.left) {
            auto brush=CreateSolidBrush(mix_color(g_theme->bg_plot,g_theme->accent,24));FillRect(dc,&selected,brush);DeleteObject(brush);
        }
    }
    if(g.show_gap_markers && !g.stitch_time_gaps) {
      const auto brush=CreateSolidBrush(mix_color(g_theme->bg_plot,RGB(245,140,32),32));
      for(auto gap:g.overview.gaps) {
        RECT rect={std::clamp(mapx(gap.left),p.left+1,p.right-1),p.top+1,
                   std::clamp(mapx(gap.right),p.left+1,p.right-1),p.bottom-1};
        if(rect.right-rect.left<3)rect.top=p.bottom-8;
        if(rect.right<=rect.left)rect.right=std::min(p.right-1,rect.left+1);
        if(rect.right<=rect.left)continue;
        FillRect(dc,&rect,brush);
        App::GapMarkerVisual marker;marker.rect=rect;marker.duration=gap.right-gap.left;
        marker.estimated_missing_samples=gap_estimated_missing_samples(marker.duration,g.cached_global_gap_step);
        g.visible_gap_markers.push_back(marker);
      }
      DeleteObject(brush);
    }
    const auto& view=*g.overview.view;
    const bool original=std::all_of(view.buckets.begin(),view.buckets.end(),[](const auto& bucket){return !bucket.aggregated();});
    const bool smooth=original && should_render_smoothed_polyline(view.buckets.size(),pw);
    for(std::size_t c=0;c<g.visible.size();++c) {
        if(!g.visible[c])continue;
        const COLORREF color=channel_color(c);
        HPEN pen=CreatePen(original && g.distinguish_curves ? curve_pen_style(c) : PS_SOLID,1,color);
        const auto old=SelectObject(dc,pen);int last_symbol=-100000;
        std::array<DWORD,1024> lengths;lengths.fill(2);
        std::vector<POINT> pixels;pixels.reserve(2048);
        const auto flush_pixels=[&]{if(!pixels.empty()) {
            const auto old_pixel_pen=SelectObject(dc,GetStockObject(DC_PEN));SetDCPenColor(dc,color);
            PolyPolyline(dc,pixels.data(),lengths.data(),static_cast<DWORD>(pixels.size()/2));
            SelectObject(dc,old_pixel_pen);pixels.clear();
        }};
        const auto pixel=[&](POINT at) {pixels.push_back(at);pixels.push_back({at.x,at.y+1});if(pixels.size()==2048)flush_pixels();};
        std::vector<POINT> run;run.reserve(smooth ? view.buckets.size() : 2049);
        const auto flush=[&]{if(!run.empty()){
            if(smooth)draw_catmull_rom(dc,run);
            else if(run.size()>1){Polyline(dc,run.data(),static_cast<int>(run.size()));pixel(run.back());}
            else pixel(run[0]);
            run.clear();}};
        std::vector<POINT> extrema;extrema.reserve(2048);
        const auto flush_extrema=[&]{if(!extrema.empty()){
            PolyPolyline(dc,extrema.data(),lengths.data(),static_cast<DWORD>(extrema.size()/2));extrema.clear();}};
        // A dense bucket is an amplitude envelope, not four isolated samples.
        // Subpixel gaps/NaNs forbid joining neighbouring curves, but must not
        // erase the range of the finite original samples from an overview.
        const auto channel=std::find(g.overview.channels.begin(),g.overview.channels.end(),c);
        if(channel!=g.overview.channels.end())for(const auto& bucket:view.buckets) {
            if(!bucket.aggregated() || bucket.last_time<g.win_start || bucket.first_time>g.win_end)continue;
            const auto& trace=bucket.channels[channel-g.overview.channels.begin()];if(!trace.finite_count)continue;
            const auto x0=sample_x(bucket.first_sample,bucket.first_time),x1=sample_x(bucket.first_sample+bucket.sample_count-1,bucket.last_time);
            if(x1-x0>2)continue; // Never bridge a resolved, visible outage.
            const bool affine=g.overview.source->processing_identity().empty() && g.channel_transform_kind[c]==TransformRuntimeKind::Affine;
            const auto a=affine ? trace.minimum.value*g.channel_transform_mul[c]+g.channel_transform_add[c] : trace.minimum.value;
            const auto b=affine ? trace.maximum.value*g.channel_transform_mul[c]+g.channel_transform_add[c] : trace.maximum.value;
            if(!std::isfinite(a) || !std::isfinite(b))continue;
            const auto x=x0+(x1-x0)/2,y0=mapy(a),y1=mapy(b);
            extrema.push_back({x,std::min(y0,y1)});extrema.push_back({x,std::max(y0,y1)+1});
            if(extrema.size()==2048)flush_extrema();
        }
        flush_extrema();
        visit_overview_curve(c,threshold,[&](OverviewCurvePoint point) {
            const int x=sample_x(point.sample,point.time),y=mapy(point.value);
            if(smooth) {if(!point.connect)flush();run.push_back({x,y});SetPixelV(dc,x,y,color);}
            else {if(!point.connect)flush();run.push_back({x,y});
                if(run.size()==2049){const auto last=run.back();flush();run.push_back(last);}}
            if(g.distinguish_curves && x-last_symbol>=52){draw_curve_symbol(dc,x,y,c,color);last_symbol=x;}
            else if(smooth && view.sample_count<=400) {
                SetPixelV(dc,x,y,color);MoveToEx(dc,x-2,y,nullptr);LineTo(dc,x+3,y);
                MoveToEx(dc,x,y-2,nullptr);LineTo(dc,x,y+3);
            }
        });flush();flush_pixels();SelectObject(dc,old);DeleteObject(pen);
    }
    if(g.playhead_active && g.playhead>=g.win_start && g.playhead<=g.win_end) {
        HPEN pen=CreatePen(PS_SOLID,2,g_theme->playhead);const auto old=SelectObject(dc,pen);
        MoveToEx(dc,mapx(g.playhead),p.top,nullptr);LineTo(dc,mapx(g.playhead),p.bottom);SelectObject(dc,old);DeleteObject(pen);
    }
    RestoreDC(dc,saved);draw_legend(dc,p);
    if(g.overview.snap_pending || g.overview.snap_error || g.overview.pending || g.overview.error) {
        const std::wstring message=g.overview.error ?
            (g_str==&kEn ? L"Could not refine this view. Showing the previous projection." : L"Не удалось уточнить график. Показан предыдущий обзор.") : g.overview.pending ?
            pending_signal_message(true) : g.overview.snap_pending ?
            (g_str==&kEn ? L"Reading original sample…" : L"Чтение исходного отсчёта…") :
            (g_str==&kEn ? L"Original sample could not be read. Annotation unchanged." : L"Не удалось прочитать исходный отсчёт. Разметка не изменена.");
        const int notice=SaveDC(dc);RECT label=p;label.left+=8;label.top+=8;label.right-=8;label.bottom=label.top+22;
        SetBkMode(dc,OPAQUE);SetBkColor(dc,g_theme->bg_plot);SetTextColor(dc,g_theme->text_primary);
        SetTextAlign(dc,TA_LEFT|TA_TOP);DrawTextW(dc,message.c_str(),-1,&label,DT_RIGHT|DT_SINGLELINE|DT_NOPREFIX);
        RestoreDC(dc,notice);
    }
    g.vx0=displayed_start;g.vx1=displayed_end;g.vy0=low;g.vy1=high;g.vrect=p;g.vvalid=true;
}
} // namespace gui
