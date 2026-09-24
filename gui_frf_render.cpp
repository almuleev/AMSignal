#include "gui_frf_render.hpp"
#include "gui_frf.hpp"
#include "gui_input.hpp"
#include "gui_side_panel.hpp"
#include "gui_state_history.hpp"
#include "gui_state.hpp"
#include "gui_render.hpp"
#include "gui_layout.hpp"
#include "gui_navigation.hpp"
#include "gui_status.hpp"
#include "gui_menu.hpp"
#include "gui_ids.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"

namespace gui {
namespace {
bool g_dragging_reference_divider = false;
constexpr int kReferenceDividerGap = 26;
double dynamic_coefficient(const lvm::FrfResult& r, std::size_t k) {
    return lvm::frf_dynamic_coefficient(r, k);
}
std::vector<double> display_coefficients(const lvm::FrfResult& r) {
    const std::size_t n=r.frequencies.size();
    std::vector<double> values(n,std::numeric_limits<double>::quiet_NaN());
    if (n<2) return values;
    if (g.frf.display_smoothing_octaves<=0) {
        for (std::size_t k=1;k<n;++k) values[k]=dynamic_coefficient(r,k);
        return values;
    }
    std::vector<double> sums(n+1,0); std::vector<std::size_t> counts(n+1,0);
    for (std::size_t k=1;k<n;++k) {
        sums[k+1]=sums[k]; counts[k+1]=counts[k];
        const double value=dynamic_coefficient(r,k);
        if (std::isfinite(value)) { sums[k+1]+=value; ++counts[k+1]; }
    }
    const double span=std::pow(2.0,g.frf.display_smoothing_octaves/2);
    std::size_t lo=1,hi=1;
    for (std::size_t k=1;k<n;++k) {
        if (!std::isfinite(dynamic_coefficient(r,k))) continue;
        const double low=r.frequencies[k]/span, high=r.frequencies[k]*span;
        while (lo<n && r.frequencies[lo]<low) ++lo;
        while (hi<n && r.frequencies[hi]<=high) ++hi;
        const auto count=counts[hi]-counts[lo];
        if (count) values[k]=(sums[hi]-sums[lo])/count;
    }
    return values;
}
// Measurement points must snap to what is drawn, not to the unsmoothed source
// bin. Otherwise a click visibly lands away from its point when FRF display
// smoothing is enabled.
bool snap_to_displayed_frf_curve_impl(double& frequency, double& coefficient) {
    if (!g.frf.result.ok || !g.vvalid || g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return false;
    const RECT& p = g.vrect;
    const int width = p.right - p.left, height = p.bottom - p.top;
    if (width <= 0 || height <= 0) return false;
    // Use the same active linear/logarithmic mapping as draw_frf(). The
    // previous logarithmic-only conversion could pick a bin away from the
    // rendered curve while the linear frequency axis was active.
    const auto to_x = [&](double f) {
        return static_cast<double>(p.left) + frf_frequency_fraction(f) * width;
    };
    const auto to_y = [&](double kd) {
        return static_cast<double>(p.bottom) -
            (kd - g.vy0) / (g.vy1 - g.vy0) * height;
    };
    const double target_x = to_x(frequency), target_y = to_y(coefficient);
    double best_distance = std::numeric_limits<double>::infinity();
    double best_frequency = frequency, best_coefficient = coefficient;
    for (const auto& result : g.frf.result.responses) {
        if (!result.ok) continue;
        const auto values = display_coefficients(result);
        for (std::size_t k = 1; k < result.frequencies.size(); ++k) {
            const double f = result.frequencies[k];
            const double kd = values[k];
            if (!(f > 0) || !std::isfinite(kd)) continue;
            const double dx = to_x(f) - target_x;
            const double dy = to_y(kd) - target_y;
            const double distance = dx * dx + dy * dy;
            if (distance < best_distance) {
                best_distance = distance;
                best_frequency = f;
                best_coefficient = kd;
            }
        }
    }
    if (!std::isfinite(best_distance)) return false;
    frequency = best_frequency;
    coefficient = best_coefficient;
    return true;
}
void line(HDC dc, int x0, int y0, int x1, int y1) {
    MoveToEx(dc, x0, y0, nullptr); LineTo(dc, x1, y1);
}
void changed() {
    // Pan and zoom only change the graph and its axis labels. Do not invalidate
    // the bottom status bar: it contains text that does not change on zoom.
    RECT dirty=plot_rect();
    dirty.left=0; // include the physical captions and numeric Y-scale gutter
    dirty.bottom+=kAxisBottom;
    InvalidateRect(g.main,&dirty,FALSE);
}
}

double frf_frequency_at_fraction(double t) {
    if (g.frf.logarithmic_frequency_axis)
        return std::pow(10.0, g.frf.log_start + t * (g.frf.log_end - g.frf.log_start));
    return g.frf.frequency_start + t * (g.frf.frequency_end - g.frf.frequency_start);
}
double frf_frequency_fraction(double f) {
    if (!(f > 0)) return std::numeric_limits<double>::quiet_NaN();
    if (g.frf.logarithmic_frequency_axis) {
        if (!(g.frf.log_end > g.frf.log_start)) return std::numeric_limits<double>::quiet_NaN();
        return (std::log10(f) - g.frf.log_start) / (g.frf.log_end - g.frf.log_start);
    }
    if (!(g.frf.frequency_end > g.frf.frequency_start)) return std::numeric_limits<double>::quiet_NaN();
    return (f-g.frf.frequency_start)/(g.frf.frequency_end-g.frf.frequency_start);
}

bool snap_to_displayed_frf_curve(double& frequency, double& coefficient) {
    return snap_to_displayed_frf_curve_impl(frequency, coefficient);
}

double frf_reference_y_max() {
    const auto& common=g.frf.result.common();
    const double f0=frf_frequency_at_fraction(0), f1=frf_frequency_at_fraction(1);
    double high=0;
    for (std::size_t k=1;k<common.frequencies.size() && k<common.reference_amplitude.size();++k) {
        if (common.frequencies[k]<f0 || common.frequencies[k]>f1 ||
            k>=common.reference_amplitude_valid.size() || !common.reference_amplitude_valid[k]) continue;
        high=std::max(high,common.reference_amplitude[k]);
    }
    if (!(high>0) || !std::isfinite(high)) high=1;
    return g.frf.reference_auto_y ? high*1.08 : std::max(1e-12,g.frf.reference_y_max);
}
void frf_y_range(double& low, double& high) {
    // KD is a linear amplitude ratio. Keep its baseline at zero in automatic
    // and manual views; a shifted baseline exaggerates small differences and
    // makes the graph look logarithmic even though the values are not dB.
    low = 0.0;
    high = g.frf.y_max;
    if (!g.frf.auto_y) {
        if (!(high>0) || !std::isfinite(high)) high=1.0;
        return;
    }
    high = 0.0;
    const double a = frf_frequency_at_fraction(0), b = frf_frequency_at_fraction(1);
    for (const auto& r:g.frf.result.responses) {
        if (!r.ok) continue;
        const auto values=display_coefficients(r);
        for (std::size_t k = 1; k < r.frequencies.size(); ++k) {
            if (r.frequencies[k] < a || r.frequencies[k] > b) continue;
            const double value = values[k];
            if (!std::isfinite(value)) continue;
            high = std::max(high, value);
        }
    }
    if (!(high>0) || !std::isfinite(high)) { high = 1; return; }
    high *= 1.08;
}

namespace {
int frf_legend_height() {
    if (!g.distinguish_curves || g.frf.result.responses.size()<2) return 0;
    return 4+20*static_cast<int>((g.frf.result.responses.size()+2)/3);
}
}

RECT frf_coefficient_plot_rect(const RECT& full) {
    RECT coefficient=full;
    coefficient.top+=frf_legend_height();
    if (!g.frf.show_reference_amplitude) return coefficient;
    const int available=std::max(1L,coefficient.bottom-coefficient.top);
    const int gap=kReferenceDividerGap;
    int reference_height=static_cast<int>(std::lround(available*g.frf.reference_height_fraction));
    // Keep both plots usable, but do not cap the Reference graph at a fixed
    // pixel size: the divider must allow the two plots to be nearly equal.
    const int maximum_reference_height=std::max(60,available-70-gap);
    reference_height=std::clamp(reference_height,60,maximum_reference_height);
    coefficient.bottom=std::max(coefficient.top+1,coefficient.bottom-reference_height-gap);
    return coefficient;
}

RECT frf_reference_plot_rect(const RECT& full) {
    if (!g.frf.show_reference_amplitude) return RECT{full.left,full.bottom,full.right,full.bottom};
    const RECT coefficient=frf_coefficient_plot_rect(full);
    return RECT{full.left,coefficient.bottom+kReferenceDividerGap,full.right,full.bottom};
}

void draw_frf(HDC dc, const RECT& p) {
    g_legend_items.clear(); g_legend_box = {};
    g.visible_gap_markers.clear();
    HBRUSH bg = CreateSolidBrush(g_theme->bg_plot);
    FillRect(dc, &p, bg); DeleteObject(bg);
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, g_theme->axis_text);
    HFONT axis_font = g.axis_font ? g.axis_font :
        (g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    HGDIOBJ previous_font = SelectObject(dc, axis_font);
    if (!g.frf.result.ok || g.frf.pending) {
        RECT r = p; InflateRect(&r, -24, -24);
        std::wstring text = frf_status_text();
        if (text.empty()) text = g_str == &kEn ? L"Select Supports and Responses, then Calculate." : L"Выберите опоры и отклики и нажмите «Рассчитать».";
        DrawTextW(dc, text.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, previous_font); g.vvalid = false; return;
    }
    double low, high; frf_y_range(low, high);
    const RECT coefficient_plot=frf_coefficient_plot_rect(p);
    const RECT reference_plot=frf_reference_plot_rect(p);
    const int width = coefficient_plot.right - coefficient_plot.left;
    const int height = coefficient_plot.bottom - coefficient_plot.top;
    if (width <= 0 || height <= 0 || (g.frf.show_reference_amplitude && reference_plot.bottom<=reference_plot.top) || !(high > low)) {
        SelectObject(dc, previous_font); return;
    }
    const auto mapx = [&](double f) { return p.left + static_cast<int>(std::clamp(frf_frequency_fraction(f), -1.0, 2.0) * width); };
    const auto mapy = [&](double v) { return coefficient_plot.bottom - static_cast<int>(std::clamp((v - low) / (high - low), -1.0, 2.0) * height); };
    const auto& common=g.frf.result.common();
    const double f0 = frf_frequency_at_fraction(0), f1 = frf_frequency_at_fraction(1);
    const double reference_high=frf_reference_y_max();
    const int reference_height=std::max(1L,reference_plot.bottom-reference_plot.top);
    const auto map_reference_y=[&](double value) {
        return reference_plot.bottom-static_cast<int>(std::clamp(value/reference_high,-1.0,2.0)*reference_height);
    };

    // In grayscale mode the response-to-symbol mapping lives in its own band,
    // not over the data. Three compact columns keep the graph readable.
    if (g.distinguish_curves && g.frf.result.responses.size()>1) {
        const int columns=3;
        const int column_width=std::max(1,width/columns);
        for (std::size_t i=0;i<g.frf.result.responses.size();++i) {
            const int column=static_cast<int>(i%columns), row=static_cast<int>(i/columns);
            const int x=p.left+column*column_width+4, y=p.top+4+row*20;
            const COLORREF color=i<g.frf.outputs.size() ? channel_color(g.frf.outputs[i]) : g_theme->axis_text;
            HPEN sample_pen=CreatePen(curve_pen_style(i),1,color);
            HGDIOBJ old_sample_pen=SelectObject(dc,sample_pen);
            MoveToEx(dc,x,y+6,nullptr); LineTo(dc,x+22,y+6);
            SelectObject(dc,old_sample_pen); DeleteObject(sample_pen);
            draw_curve_symbol(dc,x+11,y+6,i,color,3);
            const std::wstring label_text=frf_curve_label(i);
            RECT label_rect{x+28,y,p.left+(column+1)*column_width-4,y+18};
            SetTextColor(dc,g_theme->axis_text);
            DrawTextW(dc,label_text.c_str(),-1,&label_rect,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
    }

    HPEN grid = CreatePen(PS_SOLID, 1, g_theme->grid);
    HGDIOBJ old_pen = SelectObject(dc, grid);
    // Log mode uses decades; linear mode uses evenly spaced physical frequency.
    int previous_label_x = -10000;
    const int tick_count=g.frf.logarithmic_frequency_axis ? 0 : 5;
    if (g.frf.logarithmic_frequency_axis) for (int decade=static_cast<int>(std::floor(g.frf.log_start)); decade<=static_cast<int>(std::ceil(g.frf.log_end)); ++decade) {
        const double base=std::pow(10.0,decade);
        for (double multiple:{1.0,2.0,5.0}) {
            const double f=base*multiple; if (f<f0 || f>f1) continue;
            const int x = mapx(f);
            line(dc, x, coefficient_plot.top, x, coefficient_plot.bottom);
            if (g.frf.show_reference_amplitude) line(dc, x, reference_plot.top, x, reference_plot.bottom);
            wchar_t text[48]; swprintf(text, 48, L"%.5g", f);
            RECT label{x-36, reference_plot.bottom+4, x+36, reference_plot.bottom+23};
            DrawTextW(dc, text, -1, &label, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            previous_label_x=x;
        }
    }
    if (!g.frf.logarithmic_frequency_axis || previous_label_x==-10000) for (int i=0;i<=(tick_count ? tick_count : 4);++i) {
        const int divisor=tick_count ? tick_count : 4;
        const double f=frf_frequency_at_fraction(static_cast<double>(i)/divisor);
        const int x=mapx(f);
        line(dc,x,coefficient_plot.top,x,coefficient_plot.bottom);
        if (g.frf.show_reference_amplitude) line(dc,x,reference_plot.top,x,reference_plot.bottom);
        wchar_t text[48]; swprintf(text,48,L"%.5g",f);
        RECT label{x-36,reference_plot.bottom+4,x+36,reference_plot.bottom+23};
        DrawTextW(dc,text,-1,&label,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    const double raw_step = (high - low) / 6;
    const double base = std::pow(10.0, std::floor(std::log10(raw_step)));
    const double ratio = raw_step / base;
    const double step = base * (ratio <= 1 ? 1 : ratio <= 2 ? 2 : ratio <= 5 ? 5 : 10);
    // The reference graph can be deliberately compact. Keep all grid lines,
    // but omit only numeric labels that would overlap a preceding label.
    int coefficient_last_label_top=coefficient_plot.bottom+3;
    int coefficient_label_count=0;
    for (double v = std::ceil(low / step) * step; v <= high; v += step) {
        const int y = mapy(v); line(dc, coefficient_plot.left, y, coefficient_plot.right, y);
        wchar_t text[48]; swprintf(text, 48, L"%.5g", v);
        SIZE size{}; GetTextExtentPoint32W(dc,text,lstrlenW(text),&size);
        const int label_y=std::clamp(y-size.cy/2,coefficient_plot.top+2,
            coefficient_plot.bottom-size.cy-2);
        if (label_y+size.cy+3>coefficient_last_label_top) continue;
        SetTextAlign(dc,TA_RIGHT|TA_TOP);
        TextOutW(dc,coefficient_plot.left-8,label_y,text,lstrlenW(text));
        coefficient_last_label_top=label_y;
        ++coefficient_label_count;
    }
    // A compact plot still needs a usable scale. If collision avoidance left
    // only its zero label, explicitly retain the upper range endpoint.
    wchar_t coefficient_high_text[48]; swprintf(coefficient_high_text,48,L"%.5g",high);
    SIZE coefficient_high_size{};
    GetTextExtentPoint32W(dc,coefficient_high_text,lstrlenW(coefficient_high_text),&coefficient_high_size);
    if (coefficient_label_count<2 && coefficient_plot.bottom-coefficient_plot.top>=coefficient_high_size.cy*2+6) {
        SetTextAlign(dc,TA_RIGHT|TA_TOP);
        TextOutW(dc,coefficient_plot.left-8,coefficient_plot.top+2,coefficient_high_text,lstrlenW(coefficient_high_text));
    }
    const double reference_raw_step=reference_high/3;
    const double reference_base=std::pow(10.0,std::floor(std::log10(reference_raw_step)));
    const double reference_ratio=reference_raw_step/reference_base;
    const double reference_step=reference_base*(reference_ratio<=1 ? 1 : reference_ratio<=2 ? 2 : reference_ratio<=5 ? 5 : 10);
    int reference_last_label_top=reference_plot.bottom+3;
    int reference_label_count=0;
    for (double value=0;g.frf.show_reference_amplitude && value<=reference_high;value+=reference_step) {
        const int y=map_reference_y(value);
        line(dc,reference_plot.left,y,reference_plot.right,y);
        wchar_t text[48]; swprintf(text,48,L"%.5g",value);
        SIZE size{}; GetTextExtentPoint32W(dc,text,lstrlenW(text),&size);
        const int label_y=std::clamp(y-size.cy/2,reference_plot.top+2,
            reference_plot.bottom-size.cy-2);
        if (label_y+size.cy+3>reference_last_label_top) continue;
        SetTextAlign(dc,TA_RIGHT|TA_TOP);
        TextOutW(dc,reference_plot.left-8,label_y,text,lstrlenW(text));
        reference_last_label_top=label_y;
        ++reference_label_count;
    }
    wchar_t reference_high_text[48]; swprintf(reference_high_text,48,L"%.5g",reference_high);
    SIZE reference_high_size{};
    GetTextExtentPoint32W(dc,reference_high_text,lstrlenW(reference_high_text),&reference_high_size);
    if (g.frf.show_reference_amplitude && reference_label_count<2 &&
        reference_plot.bottom-reference_plot.top>=reference_high_size.cy*2+6) {
        SetTextAlign(dc,TA_RIGHT|TA_TOP);
        TextOutW(dc,reference_plot.left-8,reference_plot.top+2,reference_high_text,lstrlenW(reference_high_text));
    }
    SelectObject(dc, old_pen); DeleteObject(grid);
    HPEN frame = CreatePen(PS_SOLID, 1, g_theme->frame);
    old_pen = SelectObject(dc, frame);
    HGDIOBJ old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    Rectangle(dc,coefficient_plot.left,coefficient_plot.top,coefficient_plot.right,coefficient_plot.bottom);
    if (g.frf.show_reference_amplitude)
        Rectangle(dc,reference_plot.left,reference_plot.top,reference_plot.right,reference_plot.bottom);
    SelectObject(dc,old_brush);
    SelectObject(dc, old_pen); DeleteObject(frame);

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, coefficient_plot.left+1, coefficient_plot.top+1,
                      coefficient_plot.right, coefficient_plot.bottom);
    for (std::size_t response=0;response<g.frf.result.responses.size();++response) {
        const auto& r=g.frf.result.responses[response];
        if (!r.ok) continue;
        const COLORREF color=channel_color(g.frf.outputs[response]);
        HPEN curve = CreatePen(g.distinguish_curves ? curve_pen_style(response) : PS_SOLID, 1, color);
        old_pen = SelectObject(dc, curve);
        const std::size_t begin = std::max<std::size_t>(1, static_cast<std::size_t>(
            std::lower_bound(r.frequencies.begin(), r.frequencies.end(), f0) - r.frequencies.begin()));
        const std::size_t end = static_cast<std::size_t>(
            std::upper_bound(r.frequencies.begin(), r.frequencies.end(), f1) - r.frequencies.begin());
        const auto values=display_coefficients(r);
        bool started = false;
        // Use one mean value per screen column instead of a min/max whisker.
        // Invalid bins still break the curve rather than being joined across.
        int column = -1; double sum_y=0; std::size_t count_y=0;
        int last_symbol_x=std::numeric_limits<int>::min()/2;
        auto flush = [&] {
            if (column < 0) return;
            const int y=static_cast<int>(std::lround(sum_y/count_y));
            if (started) LineTo(dc,column,y); else MoveToEx(dc,column,y,nullptr);
            if (g.distinguish_curves && column-last_symbol_x>=52) {
                draw_curve_symbol(dc,column,y,response,color);
                last_symbol_x=column;
            }
            started = true;
        };
        for (std::size_t k = begin; k < end; ++k) {
            const double v = values[k];
            if (!std::isfinite(v)) { flush(); column = -1; started = false; continue; }
            const int x = mapx(r.frequencies[k]), y = mapy(v);
            if (x != column) { flush(); column=x; sum_y=y; count_y=1; }
            else { sum_y+=y; ++count_y; }
        }
        flush();
        SelectObject(dc, old_pen); DeleteObject(curve);
    }
    RestoreDC(dc, saved);

    // The averaged reference is intentionally a separate plot: its physical
    // amplitude must never share the dimensionless KD scale.
    const int reference_saved=SaveDC(dc);
    if (g.frf.show_reference_amplitude) IntersectClipRect(dc,reference_plot.left+1,reference_plot.top+1,
                      reference_plot.right,reference_plot.bottom);
    const COLORREF reference_color=!g.frf.inputs.empty() ? channel_color(g.frf.inputs.front()) : g_theme->accent;
    const std::size_t reference_style=g.frf.result.responses.size();
    HPEN reference_pen=CreatePen(g.distinguish_curves ? curve_pen_style(reference_style) : PS_SOLID,1,reference_color);
    old_pen=SelectObject(dc,reference_pen);
    bool reference_started=false;
    int reference_column=-1,reference_last_symbol=std::numeric_limits<int>::min()/2;
    double reference_sum_y=0; std::size_t reference_count_y=0;
    auto flush_reference=[&] {
        if (reference_column<0) return;
        const int y=static_cast<int>(std::lround(reference_sum_y/reference_count_y));
        if (reference_started) LineTo(dc,reference_column,y); else MoveToEx(dc,reference_column,y,nullptr);
        if (g.distinguish_curves && reference_column-reference_last_symbol>=52) {
            draw_curve_symbol(dc,reference_column,y,reference_style,reference_color);
            reference_last_symbol=reference_column;
        }
        reference_started=true;
    };
    const std::size_t reference_begin=std::max<std::size_t>(1,static_cast<std::size_t>(
        std::lower_bound(common.frequencies.begin(),common.frequencies.end(),f0)-common.frequencies.begin()));
    const std::size_t reference_end=static_cast<std::size_t>(
        std::upper_bound(common.frequencies.begin(),common.frequencies.end(),f1)-common.frequencies.begin());
    for (std::size_t k=reference_begin;g.frf.show_reference_amplitude && k<reference_end;++k) {
        const bool valid=k<common.reference_amplitude_valid.size() && common.reference_amplitude_valid[k] &&
            k<common.reference_amplitude.size() && std::isfinite(common.reference_amplitude[k]);
        if (!valid) { flush_reference(); reference_column=-1; reference_started=false; continue; }
        const int x=mapx(common.frequencies[k]), y=map_reference_y(common.reference_amplitude[k]);
        if (x!=reference_column) {
            flush_reference(); reference_column=x; reference_sum_y=y; reference_count_y=1;
        } else {
            reference_sum_y+=y; ++reference_count_y;
        }
    }
    flush_reference();
    SelectObject(dc,old_pen); DeleteObject(reference_pen);
    RestoreDC(dc,reference_saved);

    RECT reference_title{reference_plot.left,coefficient_plot.bottom+4,reference_plot.right,reference_plot.top-3};
    const std::wstring reference_text=(g_str==&kEn ? L"Average Reference amplitude: " : L"Ср. опора: ")+g.frf.input_name;
    SetTextColor(dc,g_theme->axis_text);
    if (g.frf.show_reference_amplitude) DrawTextW(dc,reference_text.c_str(),-1,&reference_title,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    // A small familiar grip keeps the splitter discoverable without competing
    // with the Reference title. Its hit area remains the whole separator.
    if (g.frf.show_reference_amplitude) {
        const int center_x=(coefficient_plot.left+coefficient_plot.right)/2;
        const int center_y=(coefficient_plot.bottom+reference_plot.top)/2;
        HPEN grip=CreatePen(PS_SOLID,1,g_theme->grid);
        HGDIOBJ old_grip=SelectObject(dc,grip);
        for (int offset : {-4,0,4}) {
            MoveToEx(dc,center_x-8,center_y+offset,nullptr);
            LineTo(dc,center_x+8,center_y+offset);
        }
        SelectObject(dc,old_grip);
        DeleteObject(grip);
    }
    RECT xlabel{reference_plot.left, reference_plot.bottom+23, reference_plot.right, reference_plot.bottom+42};
    DrawTextW(dc, g.frf.logarithmic_frequency_axis ? (g_str==&kEn ? L"Frequency, Hz (log scale)" : L"Частота, Гц (лог.)") :
              (g_str==&kEn ? L"Frequency, Hz (linear scale)" : L"Частота, Гц (лин.)"),
              -1, &xlabel, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    // Guides and measurement annotations may select another font. All FRF
    // scale captions and editable X/Y names use the same axis font as Time
    // and FFT before they are rendered over those annotations.
    SelectObject(dc, axis_font);
    g.vx0 = g.frf.logarithmic_frequency_axis ? g.frf.log_start : g.frf.frequency_start;
    g.vx1 = g.frf.logarithmic_frequency_axis ? g.frf.log_end : g.frf.frequency_end;
    g.vy0 = low; g.vy1 = high; g.vrect = coefficient_plot; g.vvalid = true;
    draw_guides(dc);
    draw_markers(dc);
    draw_measure(dc);

    // Reference points have the same frequency axis as KD but a separate,
    // physical amplitude axis. Draw them here rather than through the common
    // overlay, whose cached Y mapping intentionally belongs to KD.
    if (g.frf.show_reference_amplitude) {
        const int saved_reference_points=SaveDC(dc);
        IntersectClipRect(dc,reference_plot.left+1,reference_plot.top+1,
                         reference_plot.right,reference_plot.bottom);
        HFONT point_font=g.axis_font ? g.axis_font : g.ui_font;
        HGDIOBJ old_font=SelectObject(dc,point_font);
        SetBkMode(dc,TRANSPARENT);
        for (const auto& group : g.point_groups) {
            if (!group.visible || group.points.empty() || group.mode != PointGroupMode::FRF ||
                !group.frf_reference_axis) continue;
            const auto point_x=[&](double frequency) { return mapx(frequency); };
            const auto point_y=[&](double amplitude) { return map_reference_y(amplitude); };
            HPEN segment=CreatePen(PS_DASH,1,group.color);
            HGDIOBJ old_segment=SelectObject(dc,segment);
            for (std::size_t i=1;i<group.points.size();++i) {
                MoveToEx(dc,point_x(group.points[i-1].first),point_y(group.points[i-1].second),nullptr);
                LineTo(dc,point_x(group.points[i].first),point_y(group.points[i].second));
            }
            SelectObject(dc,old_segment);
            DeleteObject(segment);
            HPEN point_pen=CreatePen(PS_SOLID,2,group.color);
            HGDIOBJ old_point_pen=SelectObject(dc,point_pen);
            for (std::size_t i=0;i<group.points.size();++i) {
                const int x=point_x(group.points[i].first), y=point_y(group.points[i].second);
                MoveToEx(dc,x-8,y,nullptr); LineTo(dc,x+9,y);
                MoveToEx(dc,x,y-8,nullptr); LineTo(dc,x,y+9);
                HBRUSH dot=CreateSolidBrush(group.color);
                HGDIOBJ old_brush=SelectObject(dc,dot);
                HGDIOBJ old_pen=SelectObject(dc,GetStockObject(NULL_PEN));
                Ellipse(dc,x-3,y-3,x+4,y+4);
                SelectObject(dc,old_pen);
                SelectObject(dc,old_brush);
                DeleteObject(dot);
                if (g.annotation_selection_kind==App::AnnotationSelectionKind::Point &&
                    g.annotation_selection_index==static_cast<int>(&group-&g.point_groups[0]) &&
                    g.annotation_selection_point_index==static_cast<int>(i)) {
                    HPEN selected=CreatePen(PS_SOLID,2,g_theme->accent);
                    HGDIOBJ old_selected=SelectObject(dc,selected);
                    HGDIOBJ old_selected_brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
                    Ellipse(dc,x-7,y-7,x+8,y+8);
                    SelectObject(dc,old_selected_brush);
                    SelectObject(dc,old_selected);
                    DeleteObject(selected);
                }
                if (group.display.number || group.display.x || group.display.y) {
                    wchar_t label[160]{};
                    std::wstring text;
                    if (group.display.number) { swprintf(label,160,L"#%zu ",i+1); text+=label; }
                    if (group.display.x) { swprintf(label,160,L"%ls=%.5g Hz ",
                        axis_label_for(AnalysisMode::FRF,true).c_str(),group.points[i].first); text+=label; }
                    if (group.display.y) { swprintf(label,160,L"%ls=%.5g",
                        vertical_axis_unit().c_str(),group.points[i].second); text+=label; }
                    if (!text.empty()) {
                        SetTextColor(dc,group.color); SetTextAlign(dc,TA_LEFT|TA_BOTTOM);
                        SIZE size{}; GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);
                        HBRUSH background=CreateSolidBrush(g_theme->bg_plot);
                        HGDIOBJ old_background=SelectObject(dc,background);
                        RoundRect(dc,x+6,y-4-size.cy,x+12+size.cx,y+2,3,3);
                        SelectObject(dc,old_background); DeleteObject(background);
                        TextOutW(dc,x+8,y-2,text.c_str(),static_cast<int>(text.size()));
                    }
                }
            }
            SelectObject(dc,old_point_pen);
            DeleteObject(point_pen);
        }
        SelectObject(dc,old_font);
        RestoreDC(dc,saved_reference_points);
    }

    // Render numeric Y scales last as well. Earlier drawing passes clip and
    // repaint the plots for curves/overlays; keeping the tick labels in this
    // dedicated gutter makes both FRF scales stable and always readable.
    const auto draw_y_scale = [&](const RECT& plot, double minimum, double maximum, int divisions) {
        if (!(maximum > minimum) || plot.bottom <= plot.top) return;
        const int gutter_left=std::max(0L,plot.left-58);
        RECT gutter{gutter_left,plot.top,plot.left-3,plot.bottom};
        HBRUSH gutter_brush=CreateSolidBrush(g_theme->bg_main);
        FillRect(dc,&gutter,gutter_brush);
        DeleteObject(gutter_brush);

        const double raw_step=(maximum-minimum)/std::max(1,divisions);
        const double base=std::pow(10.0,std::floor(std::log10(raw_step)));
        const double ratio=raw_step/base;
        const double step=base*(ratio<=1 ? 1 : ratio<=2 ? 2 : ratio<=5 ? 5 : 10);
        const auto y_at=[&](double value) {
            return plot.bottom-static_cast<int>(std::lround((value-minimum)/(maximum-minimum)*(plot.bottom-plot.top)));
        };
        int previous_top=plot.bottom+3, count=0;
        SetTextColor(dc,g_theme->axis_text);
        SetBkMode(dc,TRANSPARENT);
        SetTextAlign(dc,TA_RIGHT|TA_TOP);
        for (double value=std::ceil(minimum/step)*step; value<=maximum; value+=step) {
            wchar_t text[48]; swprintf(text,48,L"%.5g",value);
            SIZE size{}; GetTextExtentPoint32W(dc,text,lstrlenW(text),&size);
            const int y=std::clamp(y_at(value)-size.cy/2,plot.top+2,plot.bottom-size.cy-2);
            if (y+size.cy+3>previous_top) continue;
            TextOutW(dc,plot.left-8,y,text,lstrlenW(text));
            previous_top=y; ++count;
        }
        wchar_t top_text[48]; swprintf(top_text,48,L"%.5g",maximum);
        SIZE top_size{}; GetTextExtentPoint32W(dc,top_text,lstrlenW(top_text),&top_size);
        if (count<2 && plot.bottom-plot.top>=top_size.cy*2+6) {
            TextOutW(dc,plot.left-8,plot.top+2,top_text,lstrlenW(top_text));
        }
    };
    draw_y_scale(coefficient_plot,low,high,6);
    if (g.frf.show_reference_amplitude) draw_y_scale(reference_plot,0.0,reference_high,3);

    const auto draw_vertical_left_axis_label = [&](const RECT& plot, const std::wstring& text) {
        if (text.empty()) return;
        LOGFONTW lf{};
        if (GetObjectW(axis_font,sizeof(lf),&lf)!=sizeof(lf)) return;
        SIZE size{};
        GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);
        lf.lfEscapement=900;
        lf.lfOrientation=900;
        HFONT vertical_font=CreateFontIndirectW(&lf);
        if (!vertical_font) return;
        HGDIOBJ previous_font=SelectObject(dc,vertical_font);
        SetTextAlign(dc,TA_LEFT|TA_BASELINE);
        SetTextColor(dc,g_theme->axis_text);
        SetBkMode(dc,TRANSPARENT);
        TextOutW(dc,kVerticalAxisCaptionLeft,(plot.top+plot.bottom+size.cx)/2,
                 text.c_str(),static_cast<int>(text.size()));
        SelectObject(dc,previous_font);
        DeleteObject(vertical_font);
    };
    draw_vertical_left_axis_label(coefficient_plot,
                                  g_str==&kEn ? L"KD |H| (dimensionless)" : L"КД |H| (б/р)");
    if (g.frf.show_reference_amplitude) {
        draw_vertical_left_axis_label(reference_plot,
            (g_str==&kEn ? L"AVG Ref. amplitude, " : L"Ср. опора, ") + vertical_axis_unit());
    }
    // The user coordinate names stay separate from the physical KD/Reference
    // captions above, just as they do in Time and FFT.
    const auto draw_user_axis_name = [&](const RECT& plot, const std::wstring& name, bool x_axis) {
        SIZE size{};
        GetTextExtentPoint32W(dc,name.c_str(),static_cast<int>(name.size()),&size);
        const int x=x_axis ? plot.right-4-size.cx : plot.left+4;
        const int y=x_axis ? plot.bottom-4-size.cy : plot.top+3;
        HBRUSH brush=CreateSolidBrush(g_theme->bg_plot);
        RECT background{x-3,y-2,x+size.cx+3,y+size.cy+2};
        FillRect(dc,&background,brush); DeleteObject(brush);
        SetTextAlign(dc,TA_LEFT|TA_TOP); SetTextColor(dc,g_theme->axis_text);
        TextOutW(dc,x,y,name.c_str(),static_cast<int>(name.size()));
    };
    draw_user_axis_name(coefficient_plot,axis_label_for(AnalysisMode::FRF,false),false);
    draw_user_axis_name(g.frf.show_reference_amplitude ? reference_plot : coefficient_plot,
                        axis_label_for(AnalysisMode::FRF,true),true);

    // Scale gutters and captions are deliberately rendered after the curves.
    // Restore every edge last, using the last in-bounds pixel for right/bottom
    // so a frame remains visible when a plot touches the client boundary.
    const auto redraw_plot_frame = [&](const RECT& plot) {
        if (plot.right <= plot.left || plot.bottom <= plot.top) return;
        HPEN edge = CreatePen(PS_SOLID, 1, g_theme->frame);
        HGDIOBJ old_edge = SelectObject(dc, edge);
        const int right = plot.right - 1, bottom = plot.bottom - 1;
        line(dc, plot.left, plot.top, right, plot.top);
        line(dc, plot.left, bottom, right, bottom);
        line(dc, plot.left, plot.top, plot.left, bottom);
        line(dc, right, plot.top, right, bottom);
        SelectObject(dc, old_edge);
        DeleteObject(edge);
    };
    redraw_plot_frame(coefficient_plot);
    if (g.frf.show_reference_amplitude) redraw_plot_frame(reference_plot);
    SelectObject(dc, previous_font);
}

LRESULT handle_frf_input(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const RECT p = frf_coefficient_plot_rect(plot_rect());
    const RECT reference = frf_reference_plot_rect(plot_rect());
    const auto inside = [&](int x, int y) { return x >= p.left && x <= p.right && y >= p.top && y <= p.bottom; };
    const auto inside_reference = [&](int x, int y) { return g.frf.show_reference_amplitude && x >= reference.left && x <= reference.right && y >= reference.top && y <= reference.bottom; };
    const auto on_reference_divider = [&](int x, int y) {
        return g.frf.show_reference_amplitude && x >= p.left && x <= p.right && y >= p.bottom && y < reference.top;
    };
    const auto reference_px_to_data = [&](int x, int y, double& frequency, double& amplitude) {
        if (!inside_reference(x,y)) return false;
        const int width=reference.right-reference.left, height=reference.bottom-reference.top;
        const double maximum=frf_reference_y_max();
        if (width<=0 || height<=0 || !(maximum>0)) return false;
        frequency=frf_frequency_at_fraction(static_cast<double>(x-reference.left)/width);
        amplitude=std::clamp(static_cast<double>(reference.bottom-y)/height*maximum,0.0,maximum);
        return std::isfinite(frequency) && std::isfinite(amplitude);
    };
    const auto snap_to_reference_curve = [&](double& frequency, double& amplitude) {
        const auto& common=g.frf.result.common();
        if (common.frequencies.empty()) return false;
        auto it=std::lower_bound(common.frequencies.begin(),common.frequencies.end(),frequency);
        std::size_t index=it==common.frequencies.end() ? common.frequencies.size()-1 :
            static_cast<std::size_t>(it-common.frequencies.begin());
        if (index>0 && (index>=common.frequencies.size() ||
            frequency-common.frequencies[index-1] < common.frequencies[index]-frequency)) --index;
        if (index>=common.reference_amplitude.size() || index>=common.reference_amplitude_valid.size() ||
            !common.reference_amplitude_valid[index] || !std::isfinite(common.reference_amplitude[index])) return false;
        frequency=common.frequencies[index]; amplitude=common.reference_amplitude[index];
        return true;
    };
    const auto begin_reference_point_drag = [&](int x, int y) {
        if (g.annotations_locked || !inside_reference(x,y)) return false;
        const double maximum=frf_reference_y_max();
        if (!(maximum>0)) return false;
        const int width=reference.right-reference.left, height=reference.bottom-reference.top;
        int best_distance=10, group_index=-1, point_index=-1;
        for (std::size_t group=0;group<g.point_groups.size();++group) {
            const auto& points=g.point_groups[group];
            if (!points.visible || points.mode!=PointGroupMode::FRF || !points.frf_reference_axis) continue;
            for (std::size_t point=0;point<points.points.size();++point) {
                const int point_x=reference.left+static_cast<int>(frf_frequency_fraction(points.points[point].first)*width);
                const int point_y=reference.bottom-static_cast<int>(points.points[point].second/maximum*height);
                const int distance=std::max(std::abs(x-point_x),std::abs(y-point_y));
                if (distance<best_distance) { best_distance=distance; group_index=static_cast<int>(group); point_index=static_cast<int>(point); }
            }
        }
        if (group_index<0) return false;
        g.annotation_selection_kind=App::AnnotationSelectionKind::Point;
        g.annotation_selection_index=group_index;
        g.annotation_selection_point_index=point_index;
        g.annotation_drag_kind=App::AnnotationDragKind::Point;
        g.annotation_drag_reference_axis=true;
        g.annotation_drag_index=group_index; g.annotation_drag_point_index=point_index;
        g.annotation_drag_point_before=g.point_groups[static_cast<std::size_t>(group_index)].points[static_cast<std::size_t>(point_index)];
        SetCapture(hwnd); return true;
    };
    switch (msg) {
        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(hwnd, &pt);
            if ((!inside(pt.x, pt.y) && !inside_reference(pt.x, pt.y)) || !g.frf.result.ok) return 0;
            const bool up = GET_WHEEL_DELTA_WPARAM(wp) > 0;
            if (inside_reference(pt.x,pt.y) && (GetKeyState(VK_CONTROL) & 0x8000)) {
                const double current=frf_reference_y_max();
                g.frf.reference_y_max=std::clamp(current*(up ? .85 : 1/.85),1e-12,1e100);
                g.frf.reference_auto_y=false;
                set_status(); invalidate_plot(); return 0;
            }
            if (GetKeyState(VK_SHIFT) & 0x8000) pan_by(up ? -.1 : .1);
            else if (inside(pt.x,pt.y) && (GetKeyState(VK_CONTROL) & 0x8000)) zoom_y_at(
                static_cast<double>(p.bottom-pt.y)/(p.bottom-p.top), up ? .85 : 1/.85);
            else if (inside(pt.x,pt.y) && (GetKeyState(VK_MENU) & 0x8000)) pan_y_by(up ? -.1 : .1);
            else zoom_at(static_cast<double>(pt.x-p.left)/(p.right-p.left), up ? .8 : 1.25);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            // See the common graph handler: annotation keyboard commands must
            // regain focus when the user clicks an FRF plot.
            if (GetFocus()!=hwnd) SetFocus(hwnd);
            if (on_reference_divider(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)) && g.frf.result.ok) {
                g_dragging_reference_divider=true; SetCapture(hwnd); return 0;
            }
            const bool placing_annotation=g.measure_mode || g.pending_line || g.pending_marker;
            if (!placing_annotation && (begin_reference_point_drag(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)) ||
                (inside(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)) && begin_annotation_drag(hwnd,GET_X_LPARAM(lp),GET_Y_LPARAM(lp))))) return 0;
            clear_annotation_selection();
            const bool lower_reference=inside_reference(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
            if (!g.annotations_locked && (inside(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)) || (g.measure_mode && lower_reference)) &&
                (g.measure_mode || g.pending_line || g.pending_marker) && g.vvalid &&
                prepare_plot_drag(GET_X_LPARAM(lp),GET_Y_LPARAM(lp))) {
                g.point_click_pending=true;
                g.point_click_reference_axis=lower_reference;
                g.point_click_x=GET_X_LPARAM(lp);
                g.point_click_y=GET_Y_LPARAM(lp);
                SetCapture(hwnd);
                return 0;
            }
            if (inside(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)) && g.frf.result.ok &&
                prepare_plot_drag(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) { g.dragging = true; SetCapture(hwnd); }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_dragging_reference_divider) {
                const RECT full=plot_rect();
                const int available=std::max(1L,full.bottom-full.top-frf_legend_height());
                g.frf.reference_height_fraction=std::clamp(
                    static_cast<double>(full.bottom-GET_Y_LPARAM(lp))/available,.12,.55);
                invalidate_plot();
            } else if (g.annotation_drag_kind!=App::AnnotationDragKind::None) {
                if (g.annotation_drag_reference_axis) {
                    double frequency=0, amplitude=0;
                    if (reference_px_to_data(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),frequency,amplitude)) {
                        if (g.snap_to_data) snap_to_reference_curve(frequency,amplitude);
                        const int group=g.annotation_drag_index, point=g.annotation_drag_point_index;
                        if (group>=0 && point>=0 && static_cast<std::size_t>(group)<g.point_groups.size() &&
                            static_cast<std::size_t>(point)<g.point_groups[static_cast<std::size_t>(group)].points.size())
                            g.point_groups[static_cast<std::size_t>(group)].points[static_cast<std::size_t>(point)]={frequency,amplitude};
                    }
                } else update_annotation_drag(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
                invalidate_plot();
                return 0;
            } else if (g.point_click_pending) {
                const int dx=GET_X_LPARAM(lp)-g.point_click_x;
                const int dy=GET_Y_LPARAM(lp)-g.point_click_y;
                if (std::abs(dx)<4 && std::abs(dy)<4) return 0;
                g.point_click_pending=false;
                g.point_click_reference_axis=false;
                g.dragging=true;
            }
            if (g.dragging) {
                double *lo, *hi, minb, maxb, minw;
                if (!active_axis(lo, hi, minb, maxb, minw)) return 0;
                const double shift = static_cast<double>(GET_X_LPARAM(lp)-g.drag_x)/(p.right-p.left)*(g.drag_hi-g.drag_lo);
                *lo = g.drag_lo-shift; *hi = g.drag_hi-shift;
                clamp_range(*lo, *hi, minb, maxb, minw);
                sync_frf_frequency_limits();
                if (g.vertical_pan) {
                    const double dy = static_cast<double>(GET_Y_LPARAM(lp)-g.drag_y)/(p.bottom-p.top)*(g.drag_y_hi-g.drag_y_lo);
                    g.frf.y_min = 0.0; g.frf.y_max = std::max(1e-6,g.drag_y_hi+dy); g.frf.auto_y = false;
                }
                changed();
            } else if (g.frf.result.ok && inside(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
                const double f = frf_frequency_at_fraction(static_cast<double>(GET_X_LPARAM(lp)-p.left)/(p.right-p.left));
                const auto& fs = g.frf.result.common().frequencies;
                auto it = std::lower_bound(fs.begin(), fs.end(), f);
                std::size_t k = it == fs.end() ? fs.size()-1 : static_cast<std::size_t>(it-fs.begin());
                if (k > 1 && f-fs[k-1] < fs[k]-f) --k;
                double low,high; frf_y_range(low,high);
                std::vector<std::vector<double>> values;
                for (const auto& result:g.frf.result.responses) values.push_back(display_coefficients(result));
                std::size_t nearest=g.frf.result.responses.size();
                double distance=std::numeric_limits<double>::infinity();
                for (std::size_t i=0;i<g.frf.result.responses.size();++i) {
                    const double kd=k<values[i].size() ? values[i][k] : std::numeric_limits<double>::quiet_NaN();
                    if (!std::isfinite(kd)) continue;
                    const double py=p.bottom-(kd-low)/(high-low)*(p.bottom-p.top);
                    const double d=std::fabs(py-GET_Y_LPARAM(lp));
                    if (d<distance) { distance=d; nearest=i; }
                }
                wchar_t text[160]{};
                if (nearest<g.frf.result.responses.size()) {
                    const auto& result=g.frf.result.responses[nearest];
                    const wchar_t* suffix=g.frf.display_smoothing_octaves>0 ? L" (сгл.)" : L"";
                    swprintf(text,160,L" | f = %.6g Hz | КД%s = %.6g",fs[k],suffix,values[nearest][k]);
                    g.status_detail_text=frf_curve_label(nearest)+text;
                    if (k<result.coherence_valid.size() && result.coherence_valid[k]) {
                        swprintf(text,160,L" | Coherence = %.4f",result.coherence[k]);
                        g.status_detail_text+=text;
                    } else g.status_detail_text+=L" | Coherence: —";
                } else {
                    swprintf(text,160,L"f = %.6g Hz | КД: —",fs[k]);
                    g.status_detail_text=text;
                }
                RECT status{0, p.bottom+kAxisBottom, 0, 0};
                RECT client; GetClientRect(hwnd, &client); status.right=client.right; status.bottom=client.bottom;
                InvalidateRect(hwnd, &status, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (g.annotation_drag_kind!=App::AnnotationDragKind::None) {
                finish_annotation_drag(true);
                g.annotation_drag_reference_axis=false;
                if (GetCapture()==hwnd) ReleaseCapture();
                set_status(); invalidate_plot(); return 0;
            }
            if (g.point_click_pending) {
                const int point_x=g.point_click_x, point_y=g.point_click_y;
                const bool reference_axis=g.point_click_reference_axis;
                g.point_click_pending=false;
                g.point_click_reference_axis=false;
                if (GetCapture()==hwnd) ReleaseCapture();
                double frequency=0, coefficient=0;
                const bool mapped=reference_axis
                    ? reference_px_to_data(point_x,point_y,frequency,coefficient)
                    : px_to_data(point_x,point_y,frequency,coefficient);
                if (mapped && g.measure_mode) {
                    if (g.snap_to_data) {
                        if (reference_axis) snap_to_reference_curve(frequency,coefficient);
                        else snap_to_displayed_frf_curve(frequency,coefficient);
                    }
                    bool created=false;
                    const int group=ensure_point_group_for_measurement((GetKeyState(VK_CONTROL)&0x8000)!=0,&created,reference_axis);
                    if (group>=0 && !point_group_contains_point(group,frequency,coefficient)) {
                        g.point_groups[static_cast<std::size_t>(group)].points.push_back({frequency,coefficient});
                        UndoAction action; action.type=UndoAction::ADD_POINT; action.point={frequency,coefficient};
                        action.point_group_index=group; action.point_group_created=created;
                        action.point_group_state=g.point_groups[static_cast<std::size_t>(group)]; action.point_group_state.points.clear();
                        push_undo(action); refresh_side_panel_controls();
                    }
                    set_status(); sync_menu(); invalidate_plot();
                } else if (mapped && g.pending_line) {
                    if (g.snap_to_data) {
                        if (reference_axis) snap_to_reference_curve(frequency,coefficient);
                        else snap_to_displayed_frf_curve(frequency,coefficient);
                    }
                    GuideLine line;
                    line.vertical=g.pending_line==1;
                    line.value=line.vertical ? frequency : coefficient;
                    line.mode=AnalysisMode::FRF;
                    if (!guide_exists_at_current_position(line.vertical,line.value)) {
                        g.guides.push_back(line);
                        UndoAction action; action.type=UndoAction::ADD_LINE; action.line=line; push_undo(action);
                    }
                    set_status(); sync_menu(); invalidate_plot();
                } else if (mapped && g.pending_marker) {
                    App::Marker marker;
                    int channel=-1;
                    const bool snapped=g.snap_to_data && snap_to_nearest_target(frequency,coefficient,&channel);
                    marker.x=frequency; marker.y=coefficient; marker.freq=true;
                    marker.mode=AnalysisMode::FRF; marker.snapped=snapped; marker.channel=snapped ? channel : -1;
                    wchar_t label[16]{}; swprintf(label,16,L"M%zu",g.markers.size()+1);
                    marker.label=label;
                    if (!marker_exists_at_current_position(marker.x,marker.y)) {
                        g.markers.push_back(marker);
                        g.active_marker=static_cast<int>(g.markers.size())-1;
                        UndoAction action; action.type=UndoAction::ADD_MARKER; action.marker=marker; push_undo(action);
                    }
                    set_status(); sync_menu(); invalidate_plot();
                }
                return 0;
            }
            [[fallthrough]];
        case WM_CANCELMODE:
            if (g.annotation_drag_kind!=App::AnnotationDragKind::None) finish_annotation_drag(false);
            g.dragging = false; g.point_click_pending=false; g.point_click_reference_axis=false; g_dragging_reference_divider=false;
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_KEYDOWN:
            if (wp==VK_DELETE || wp==VK_BACK) {
                if (delete_selected_annotation()) {
                    set_status();
                    invalidate_plot();
                }
                return 0;
            }
            if (wp == VK_ESCAPE) {
                if (g.annotation_drag_kind!=App::AnnotationDragKind::None) {
                    finish_annotation_drag(false);
                    g.annotation_drag_reference_axis=false;
                    if (GetCapture()==hwnd) ReleaseCapture();
                    invalidate_plot(); return 0;
                }
                g.dragging = false;
                g.point_click_pending = false;
                g.point_click_reference_axis = false;
                g_dragging_reference_divider = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                if (g.pending_line || g.pending_marker) {
                    g.pending_line = 0;
                    g.pending_marker = false;
                    set_status();
                    sync_menu();
                    invalidate_plot();
                }
            }
            return 0;
        case WM_RBUTTONDOWN:
            return 0;
        case WM_SETCURSOR:
            if (reinterpret_cast<HWND>(wp) == hwnd && LOWORD(lp) == HTCLIENT) {
                POINT pt{}; GetCursorPos(&pt); ScreenToClient(hwnd,&pt);
                SetCursor(LoadCursor(nullptr, on_reference_divider(pt.x,pt.y) ? IDC_SIZENS :
                    ((g.pending_line || g.pending_marker || g.measure_mode) ? IDC_CROSS : IDC_HAND))); return TRUE;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace gui
