// Render: native viewer implementation.
#include "gui_render.hpp"
#include "gui_frf_render.hpp"
#include "gui_analysis_source.hpp"
#include "gui_hotkeys.hpp"
#include "gui_gap_details.hpp"
#include "gui_controls.hpp"
#include "gui_ids.hpp"
#include "gui_layout.hpp"
#include "gui_processing.hpp"
#include "gui_render_data.hpp"
#include "gui_settings.hpp"
#include "gui_side_panel.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_state_history.hpp"
#include "gui_text.hpp"
#include "gui_theme.hpp"
#include "gui_time_axis.hpp"

namespace gui {

std::vector<LegendItem> g_legend_items;

RECT g_legend_box = {0,0,0,0};

namespace {
constexpr wchar_t kStatusAuthorCredit[] = L"AMSignal · Alexander Muleev · al.muleev@gmail.com";

int status_text_width(HDC dc, const std::wstring& value) {
    SIZE size{};
    GetTextExtentPoint32W(dc, value.c_str(), static_cast<int>(value.size()), &size);
    return size.cx;
}

void draw_status_text(HDC dc, int x, int y, int right, const std::wstring& value) {
    if (value.empty() || x >= right) return;
    RECT bounds = {x, y, right, y + kBottomBar - 5};
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &bounds,
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

std::vector<std::wstring> split_status_segments(const std::wstring& value) {
    std::vector<std::wstring> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find(L'|', begin);
        const std::wstring piece = value.substr(begin, end == std::wstring::npos ? end : end - begin);
        const std::size_t first = piece.find_first_not_of(L" \t");
        if (first != std::wstring::npos) {
            const std::size_t last = piece.find_last_not_of(L" \t");
            result.push_back(piece.substr(first, last - first + 1));
        }
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    return result;
}

struct StatusSegment {
    std::wstring text;
    COLORREF color;
    bool optional = false;
    bool active = false;
};

int status_segments_width(HDC dc, const std::vector<StatusSegment>& segments) {
    int width = 0;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i) width += status_text_width(dc, L" | ");
        width += status_text_width(dc, segments[i].text);
    }
    return width;
}

void draw_status_segments(HDC dc, int x, int y, int right, std::vector<StatusSegment> segments) {
    while (status_segments_width(dc, segments) > right - x) {
        auto remove = std::find_if(segments.rbegin(), segments.rend(), [](const StatusSegment& segment) {
            return segment.optional;
        });
        if (remove == segments.rend()) break;
        segments.erase(std::next(remove).base());
    }
    if (segments.size() > 1 && segments.back().active && status_segments_width(dc, segments) > right - x) {
        const StatusSegment& active = segments.back();
        const int separator_width = status_text_width(dc, L" | ");
        const int active_width = status_text_width(dc, active.text);
        if (active_width + separator_width >= right - x) {
            SetTextColor(dc, active.color);
            draw_status_text(dc, x, y, right, active.text);
            return;
        }
        const int active_x = right - active_width;
        SetTextColor(dc, segments.front().color);
        draw_status_text(dc, x, y, active_x - separator_width, segments.front().text);
        SetTextColor(dc, g_theme->text_secondary);
        draw_status_text(dc, active_x - separator_width, y, active_x, L" | ");
        SetTextColor(dc, active.color);
        draw_status_text(dc, active_x, y, right, active.text);
        return;
    }
    for (std::size_t i = 0; i < segments.size() && x < right; ++i) {
        if (i) {
            SetTextColor(dc, g_theme->text_secondary);
            const int separator_width = status_text_width(dc, L" | ");
            draw_status_text(dc, x, y, right, L" | ");
            x += separator_width;
        }
        SetTextColor(dc, segments[i].color);
        const int width = status_text_width(dc, segments[i].text);
        draw_status_text(dc, x, y, right, segments[i].text);
        x += width;
    }
}
} // namespace

void invalidate_plot() {
    RECT pr = plot_rect();
    RECT rc; GetClientRect(g.main, &rc);
    pr.bottom = rc.bottom; // include status bar
    InvalidateRect(g.main, &pr, FALSE);
}

// ---- drawing -------------------------------------------------------------

void draw_text(HDC dc, int x, int y, const wchar_t* s, UINT align) {
    SetTextAlign(dc, align);
    TextOutW(dc, x, y, s, lstrlenW(s));
}

std::wstring vertical_axis_unit() {
    return normalize_axis_label_text(g.axis_y_label, L"ед.");
}

std::wstring amplitude_axis_label() {
    return (g_str == &kEn ? L"Amplitude, " : L"Амплитуда, ") + vertical_axis_unit();
}

int curve_pen_style(std::size_t curve_index) {
    static constexpr int styles[] = {PS_SOLID, PS_DASH, PS_DOT, PS_DASHDOT, PS_DASHDOTDOT};
    return styles[curve_index % (sizeof(styles) / sizeof(styles[0]))];
}

void draw_curve_symbol(HDC dc, int x, int y, std::size_t curve_index, COLORREF color, int radius) {
    radius = std::max(2, radius);
    POINT previous_position{};
    const BOOL had_position=GetCurrentPositionEx(dc,&previous_position);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH brush = CreateSolidBrush(g_theme->bg_plot);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    switch (curve_index % 6) {
        case 0:
            Ellipse(dc, x-radius, y-radius, x+radius+1, y+radius+1);
            break;
        case 1:
            Rectangle(dc, x-radius, y-radius, x+radius+1, y+radius+1);
            break;
        case 2: {
            POINT points[3]={{x,y-radius-1},{x-radius,y+radius},{x+radius,y+radius}};
            Polygon(dc,points,3);
            break;
        }
        case 3: {
            POINT points[4]={{x,y-radius-1},{x-radius-1,y},{x,y+radius+1},{x+radius+1,y}};
            Polygon(dc,points,4);
            break;
        }
        case 4:
            MoveToEx(dc,x-radius,y-radius,nullptr); LineTo(dc,x+radius+1,y+radius+1);
            MoveToEx(dc,x-radius,y+radius,nullptr); LineTo(dc,x+radius+1,y-radius-1);
            break;
        default:
            MoveToEx(dc,x-radius,y,nullptr); LineTo(dc,x+radius+1,y);
            MoveToEx(dc,x,y-radius,nullptr); LineTo(dc,x,y+radius+1);
            break;
    }
    SelectObject(dc,old_brush);
    SelectObject(dc,old_pen);
    if (had_position) MoveToEx(dc,previous_position.x,previous_position.y,nullptr);
    DeleteObject(brush);
    DeleteObject(pen);
}

void draw_axes(HDC dc, const RECT& p, double x0, double x1, double y0, double y1,
               const wchar_t* xlabel) {
    HBRUSH plot_bg = CreateSolidBrush(g_theme->bg_plot);
    RECT fill_p = {p.left, p.top, p.right, p.bottom};
    FillRect(dc, &fill_p, plot_bg);
    DeleteObject(plot_bg);
    HPEN grid = CreatePen(PS_SOLID, 1, g_theme->grid);
    HPEN frame = CreatePen(PS_SOLID, 1, g_theme->frame);
    HGDIOBJ old = SelectObject(dc, grid);
    SetTextColor(dc, g_theme->axis_text);
    HFONT font = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    wchar_t buf[64];

    const int ticks = 6;
    for (int i = 0; i <= ticks; ++i) {
        const double fx = static_cast<double>(i) / ticks;
        const int px = p.left + static_cast<int>(fx * (p.right - p.left));
        SelectObject(dc, grid);
        MoveToEx(dc, px, p.top, nullptr);
        LineTo(dc, px, p.bottom);
        swprintf(buf, 64, L"%.4g", x0 + fx * (x1 - x0));
        SIZE xsz{};
        GetTextExtentPoint32W(dc, buf, lstrlenW(buf), &xsz);
        int tx = px - xsz.cx / 2;
        if (tx < p.left) tx = p.left;
        const int max_tx = p.right - xsz.cx;
        if (tx > max_tx) tx = max_tx;
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        TextOutW(dc, tx, p.bottom + 4, buf, lstrlenW(buf));

        const double fy = static_cast<double>(i) / ticks;
        const int py = p.bottom - static_cast<int>(fy * (p.bottom - p.top));
        MoveToEx(dc, p.left, py, nullptr);
        LineTo(dc, p.right, py);
        swprintf(buf, 64, L"%.4g", y0 + fy * (y1 - y0));
        SIZE ysz{};
        GetTextExtentPoint32W(dc, buf, lstrlenW(buf), &ysz);
        int ty = py - ysz.cy / 2;
        if (ty < p.top + 2) ty = p.top + 2;
        const int max_ty = p.bottom - ysz.cy - 2;
        if (ty > max_ty) ty = max_ty;
        SetTextAlign(dc, TA_RIGHT | TA_TOP);
        TextOutW(dc, p.left - 8, ty, buf, lstrlenW(buf));
    }

    HPEN minor = CreatePen(PS_SOLID, 1, g_theme->minor_grid);
    HGDIOBJ old_minor = SelectObject(dc, minor);
    for (int i = 0; i < ticks; ++i) {
        const double f0 = static_cast<double>(i) / ticks;
        const double f1 = static_cast<double>(i + 1) / ticks;
        for (int j = 1; j <= 4; ++j) {
            const double f = f0 + (f1 - f0) * j / 5.0;
            const int px = p.left + static_cast<int>(f * (p.right - p.left));
            const int py = p.bottom - static_cast<int>(f * (p.bottom - p.top));
            MoveToEx(dc, px, p.top, nullptr); LineTo(dc, px, p.bottom);
            MoveToEx(dc, p.left, py, nullptr); LineTo(dc, p.right, py);
        }
    }
    SelectObject(dc, old_minor);
    DeleteObject(minor);

    SelectObject(dc, frame);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, p.left, p.top, p.right, p.bottom);
    SelectObject(dc, old_brush);
    const std::wstring corner_x = normalize_axis_label_text(g.axis_x_label, L"X");
    const std::wstring corner_y = amplitude_axis_label();
    auto draw_corner_label = [&](const std::wstring& text, int x, int y, UINT align) {
        if (text.empty()) return;
        SIZE ts{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &ts);
        RECT br = {x - 3, y - 2, x + ts.cx + 3, y + ts.cy + 2};
        HBRUSH bg = CreateSolidBrush(g_theme->bg_plot);
        FillRect(dc, &br, bg);
        DeleteObject(bg);
        SetTextAlign(dc, align);
        TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
    };
    draw_corner_label(corner_y, p.left + 4, p.top + 4, TA_LEFT | TA_TOP);
    {
        SIZE ts{};
        GetTextExtentPoint32W(dc, corner_x.c_str(), static_cast<int>(corner_x.size()), &ts);
        draw_corner_label(corner_x, p.right - 4 - ts.cx, p.bottom - 4 - ts.cy, TA_LEFT | TA_TOP);
    }
    draw_text(dc, (p.left + p.right) / 2, p.bottom + 20, xlabel, TA_CENTER | TA_TOP);

    SelectObject(dc, old_font);
    SelectObject(dc, old);
    DeleteObject(grid);
    DeleteObject(frame);
}

void draw_legend(HDC dc, const RECT& p) {
    int item_h = 16;
    int pad = 6;
    int max_width = 0;
    int ch_count = static_cast<int>(g.ds.channel_count());
    if (ch_count == 0) return;

    HFONT leg_font = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ old_font = SelectObject(dc, leg_font);
    SetBkMode(dc, TRANSPARENT);

    for (int i = 0; i < ch_count; ++i) {
        std::wstring nm = channel_display_label(static_cast<std::size_t>(i));
        SIZE sz;
        GetTextExtentPoint32W(dc, nm.c_str(), static_cast<int>(nm.size()), &sz);
        if (sz.cx > max_width) max_width = sz.cx;
    }

    int box_w = 14 + 4 + max_width + pad * 2;
    int box_h = ch_count * item_h + pad * 2;
    int box_x = p.right - box_w - 14;
    int box_y = p.bottom - box_h - 14;

    g_legend_box = {box_x, box_y, box_x + box_w, box_y + box_h};
    g_legend_items.clear();

    // Draw themed rounded background with border
    HBRUSH bg = CreateSolidBrush(g_theme->bg_plot);
    HPEN border = CreatePen(PS_SOLID, 1, g_theme->frame);
    HGDIOBJ old_brush = SelectObject(dc, bg);
    HGDIOBJ old_pen = SelectObject(dc, border);
    RoundRect(dc, box_x, box_y, box_x + box_w, box_y + box_h, 4, 4);
    SelectObject(dc, old_pen);
    DeleteObject(border);
    SelectObject(dc, old_brush);
    DeleteObject(bg);

    int y = box_y + pad;
    for (int i = 0; i < ch_count; ++i) {
        bool vis = g.visible[i];
        COLORREF col = channel_color(i);
        if (g.distinguish_curves) {
            HPEN sample_pen=CreatePen(curve_pen_style(static_cast<std::size_t>(i)),1,col);
            HGDIOBJ old_sample_pen=SelectObject(dc,sample_pen);
            MoveToEx(dc,box_x+pad,y+8,nullptr); LineTo(dc,box_x+pad+14,y+8);
            SelectObject(dc,old_sample_pen); DeleteObject(sample_pen);
            draw_curve_symbol(dc,box_x+pad+7,y+8,static_cast<std::size_t>(i),col,3);
        } else {
            HBRUSH cb = CreateSolidBrush(col);
            HGDIOBJ old_b = SelectObject(dc, cb);
            HGDIOBJ old_p = SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, box_x + pad, y + 6, box_x + pad + 14, y + 6 + 3, 2, 2);
            SelectObject(dc, old_p);
            SelectObject(dc, old_b);
            DeleteObject(cb);
        }

        SetTextColor(dc, vis ? g_theme->text_primary : g_theme->text_secondary);
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        std::wstring nm = channel_display_label(static_cast<std::size_t>(i));
        const int text_x = box_x + pad + 18;
        TextOutW(dc, text_x, y, nm.c_str(), static_cast<int>(nm.size()));
        if (!vis) {
            SIZE text_size{};
            GetTextExtentPoint32W(dc, nm.c_str(), static_cast<int>(nm.size()), &text_size);
            HPEN line = CreatePen(PS_SOLID, 1, g_theme->text_secondary);
            HGDIOBJ old_lp = SelectObject(dc, line);
            const int line_y = y + max(6, item_h / 2);
            MoveToEx(dc, text_x, line_y, nullptr);
            LineTo(dc, text_x + text_size.cx, line_y);
            SelectObject(dc, old_lp);
            DeleteObject(line);
        }

        // Store hit-test rect for this item (full row, for easier clicking)
        g_legend_items.push_back({i, {box_x, y, box_x + box_w, y + item_h}});

        y += item_h;
    }
    SelectObject(dc, old_font);
}

// Vertical / horizontal reference lines, using the cached mapping.
void draw_guides(HDC dc) {
    if (g.guides.empty() || !g.vvalid) return;
    const RECT& p = g.vrect;
    if (g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return;
    auto mx = [&](double dx) -> int {
        if (g.mode == AnalysisMode::FRF && !(dx > 0)) return std::numeric_limits<int>::min();
        const double displayed_x = (g.mode == AnalysisMode::FRF) ? (g.vx0+frf_frequency_fraction(dx)*(g.vx1-g.vx0)) :
            (g.mode == AnalysisMode::FFT) ? dx : stitched_time_from_raw(dx);
        return p.left + static_cast<int>((displayed_x - g.vx0) / (g.vx1 - g.vx0) * (p.right - p.left));
    };
    auto my = [&](double dy) {
        return p.bottom - static_cast<int>((dy - g.vy0) / (g.vy1 - g.vy0) * (p.bottom - p.top));
    };

    HRGN clip = CreateRectRgn(p.left, p.top, p.right + 1, p.bottom + 1);
    SelectClipRgn(dc, clip);
    HPEN vpen = CreatePen(PS_DASH, 2, RGB(0, 150, 60));
    HPEN hpen = CreatePen(PS_DASH, 1, RGB(0, 150, 60));
    HGDIOBJ old = SelectObject(dc, vpen);
    SetTextColor(dc, RGB(0, 110, 45));
    HFONT lf = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ oldf = SelectObject(dc, lf);
    SetBkMode(dc, TRANSPARENT);
    wchar_t b[48];
    HBRUSH wb = CreateSolidBrush(g_theme->bg_plot);
    for (const auto& gl : g.guides) {
        if (gl.mode != g.mode) continue;
        if (gl.vertical) {
            SelectObject(dc, vpen);
            const int X = mx(gl.value);
            if (X < p.left || X > p.right) continue;
            MoveToEx(dc, X, p.top, nullptr); LineTo(dc, X, p.bottom);
            swprintf(b, 48, (g.mode == AnalysisMode::FFT || g.mode == AnalysisMode::FRF) ? g_str->fmt_hz : g_str->fmt_sec, gl.value);
            SetTextAlign(dc, TA_LEFT | TA_TOP);
            SIZE ts;
            GetTextExtentPoint32W(dc, b, lstrlenW(b), &ts);
            RECT br = {X + 3, p.top + 2, X + 3 + ts.cx + 2, p.top + 2 + ts.cy};
            FillRect(dc, &br, wb);
            TextOutW(dc, X + 3, p.top + 2, b, lstrlenW(b));
        } else {
            SelectObject(dc, hpen);
            const int Y = my(gl.value);
            if (Y < p.top || Y > p.bottom) continue;
            MoveToEx(dc, p.left, Y, nullptr); LineTo(dc, p.right, Y);
            if (g.mode==AnalysisMode::FRF) swprintf(b,48,L"КД=%.5g",gl.value);
            else swprintf(b, 48, g_str->fmt_y, gl.value);
            SetTextAlign(dc, TA_LEFT | TA_BOTTOM);
            SIZE ts;
            GetTextExtentPoint32W(dc, b, lstrlenW(b), &ts);
            RECT br = {p.left + 4, Y - 2 - ts.cy, p.left + 4 + ts.cx + 2, Y - 2};
            FillRect(dc, &br, wb);
            TextOutW(dc, p.left + 4, Y - 2, b, lstrlenW(b));
        }
    }
    DeleteObject(wb);
    SelectObject(dc, oldf);
    SelectObject(dc, old);
    DeleteObject(vpen);
    DeleteObject(hpen);
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

// Markers (bookmarks) with labels.
void draw_markers(HDC dc) {
    if (g.markers.empty() || !g.vvalid) return;
    const RECT& p = g.vrect;
    if (g.vx1 <= g.vx0) return;
    auto mx = [&](double dx) -> int {
        if (g.mode == AnalysisMode::FRF && !(dx > 0)) return std::numeric_limits<int>::min();
        const double displayed_x = (g.mode == AnalysisMode::FRF) ? (g.vx0+frf_frequency_fraction(dx)*(g.vx1-g.vx0)) :
            (g.mode == AnalysisMode::FFT) ? dx : stitched_time_from_raw(dx);
        return p.left + static_cast<int>((displayed_x - g.vx0) / (g.vx1 - g.vx0) * (p.right - p.left));
    };
    auto my = [&](double dy) {
        return p.bottom - static_cast<int>((dy - g.vy0) / (g.vy1 - g.vy0) * (p.bottom - p.top));
    };
    HRGN clip = CreateRectRgn(p.left, p.top, p.right + 1, p.bottom + 1);
    SelectClipRgn(dc, clip);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(180, 0, 180));
    HGDIOBJ old = SelectObject(dc, pen);
    SetTextColor(dc, RGB(140, 0, 140));
    HFONT mf = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ oldf = SelectObject(dc, mf);
    SetBkMode(dc, TRANSPARENT);
    HBRUSH wb = CreateSolidBrush(g_theme->bg_plot);
    HPEN bp = CreatePen(PS_SOLID, 1, g_theme->frame);
    HGDIOBJ prev_pen = SelectObject(dc, bp);
    HGDIOBJ prev_brush = SelectObject(dc, wb);
    for (const auto& m : g.markers) {
        if (m.mode != g.mode) continue;
        const int X = mx(m.x);
        if (X < p.left || X > p.right) continue;
        MoveToEx(dc, X, p.top, nullptr); LineTo(dc, X, p.bottom);
        const wchar_t* txt = nullptr;
        int tlen = 0;
        wchar_t b[48];
        if (!m.label.empty()) {
            txt = m.label.c_str();
            tlen = static_cast<int>(m.label.size());
        } else {
            swprintf(b, 48, (g.mode == AnalysisMode::FFT || g.mode == AnalysisMode::FRF) ? g_str->fmt_hz : g_str->fmt_sec, m.x);
            txt = b;
            tlen = lstrlenW(b);
        }
        SIZE ts;
        GetTextExtentPoint32W(dc, txt, tlen, &ts);
        int tx = X + 3;
        int ty = p.top + 4;
        RoundRect(dc, tx - 2, ty - 1, tx + ts.cx + 4, ty + ts.cy + 2, 3, 3);
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        TextOutW(dc, tx, ty, txt, tlen);

        if (m.snapped && m.channel >= 0) {
            const int Y = my(m.y);
            if (Y >= p.top && Y <= p.bottom) {
                HBRUSH dot = CreateSolidBrush(channel_color(static_cast<std::size_t>(m.channel)));
                HGDIOBJ old_dot_br = SelectObject(dc, dot);
                HGDIOBJ old_dot_pen = SelectObject(dc, GetStockObject(NULL_PEN));
                Ellipse(dc, X - 4, Y - 4, X + 5, Y + 5);
                SelectObject(dc, old_dot_pen);
                SelectObject(dc, old_dot_br);
                DeleteObject(dot);
            }
        }
    }
    SelectObject(dc, prev_pen);
    DeleteObject(bp);
    SelectObject(dc, prev_brush);
    DeleteObject(wb);
    SelectObject(dc, oldf);
    SelectObject(dc, old);
    DeleteObject(pen);
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

// Draw measurement points / segments using the cached mapping. Which read-outs
// appear is controlled by g.pdisp (the "point display" settings menu).
void draw_measure(HDC dc) {
    if (!has_measure_points() || !g.vvalid) return;
    const RECT& p = g.vrect;
    if (g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return;
    auto mx = [&](double dx) -> int {
        if (g.mode == AnalysisMode::FRF && !(dx > 0)) return std::numeric_limits<int>::min();
        const double displayed_x = (g.mode == AnalysisMode::FRF) ? (g.vx0+frf_frequency_fraction(dx)*(g.vx1-g.vx0)) :
            (g.mode == AnalysisMode::FFT) ? dx : stitched_time_from_raw(dx);
        return p.left + static_cast<int>((displayed_x - g.vx0) / (g.vx1 - g.vx0) * (p.right - p.left));
    };
    auto my = [&](double dy) {
        return p.bottom - static_cast<int>((dy - g.vy0) / (g.vy1 - g.vy0) * (p.bottom - p.top));
    };
    const wchar_t* xunit = (g.mode == AnalysisMode::FFT || g.mode == AnalysisMode::FRF) ? g_str->unit_hz : g_str->unit_sec;

    HRGN clip = CreateRectRgn(p.left, p.top, p.right + 1, p.bottom + 1);
    SelectClipRgn(dc, clip);

    HFONT mf = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ oldf = SelectObject(dc, mf);
    SetBkMode(dc, TRANSPARENT);
    wchar_t b[96];
    HBRUSH wb = CreateSolidBrush(g_theme->bg_plot);
    HPEN bp = CreatePen(PS_SOLID, 1, g_theme->frame);
    HGDIOBJ old = SelectObject(dc, bp);
    HGDIOBJ prev_brush = SelectObject(dc, wb);
    for (const auto& group : g.point_groups) {
        if (!group.visible || group.points.empty() || !point_group_matches_mode(group, current_point_group_mode())) continue;
        const std::wstring x_axis_label = g.mode == AnalysisMode::FRF ? L"f" :
            (g.mode == AnalysisMode::FFT ? L"f" : normalize_axis_label_text(g.axis_x_label,L"X"));
        const std::wstring y_axis_label = g.mode == AnalysisMode::FRF ? L"КД" :
            (g.mode == AnalysisMode::FFT ? (g_str == &kEn ? L"Amplitude" : L"Амплитуда") :
             normalize_axis_label_text(g.axis_y_label,L"Y"));

        HPEN seg = CreatePen(PS_DASH, 1, group.color);
        HGDIOBJ old_seg = SelectObject(dc, seg);
        for (std::size_t i = 1; i < group.points.size(); ++i) {
            MoveToEx(dc, mx(group.points[i - 1].first), my(group.points[i - 1].second), nullptr);
            LineTo(dc, mx(group.points[i].first), my(group.points[i].second));
        }
        SelectObject(dc, old_seg);
        DeleteObject(seg);

        HPEN pp = CreatePen(PS_SOLID, 2, group.color);
        HGDIOBJ old_point_pen = SelectObject(dc, pp);
        for (std::size_t i = 0; i < group.points.size(); ++i) {
            const int X = mx(group.points[i].first), Y = my(group.points[i].second);
            MoveToEx(dc, X - 8, Y, nullptr); LineTo(dc, X + 9, Y);
            MoveToEx(dc, X, Y - 8, nullptr); LineTo(dc, X, Y + 9);

            HBRUSH dot_brush = CreateSolidBrush(group.color);
            HGDIOBJ old_br = SelectObject(dc, dot_brush);
            HGDIOBJ old_pn = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, X - 3, Y - 3, X + 4, Y + 4);
            SelectObject(dc, old_pn);
            SelectObject(dc, old_br);
            DeleteObject(dot_brush);

            std::wstring lab;
            if (group.display.number) { swprintf(b, 96, L"#%zu ", i + 1); lab += b; }
            if (group.display.x) {
                swprintf(b, 96, L"%ls=", x_axis_label.c_str());
                lab += b;
                swprintf(b, 96, L"%.5g", group.points[i].first);
                lab += b;
                lab += xunit;
                lab += L" ";
            }
            if (group.display.y) {
                swprintf(b, 96, L"%ls=", y_axis_label.c_str());
                lab += b;
                swprintf(b, 96, L"%.5g", group.points[i].second);
                lab += b;
            }
            if (!lab.empty()) {
                SetTextColor(dc, group.color);
                SetTextAlign(dc, TA_LEFT | TA_BOTTOM);
                SIZE ts;
                GetTextExtentPoint32W(dc, lab.c_str(), static_cast<int>(lab.size()), &ts);
                RoundRect(dc, X + 6, Y - 4 - ts.cy, X + 12 + ts.cx, Y + 2, 3, 3);
                TextOutW(dc, X + 8, Y - 2, lab.c_str(), static_cast<int>(lab.size()));
            }

            if (i >= 1) {
                const double dx = group.points[i].first - group.points[i - 1].first;
                const double dy = group.points[i].second - group.points[i - 1].second;
                std::wstring dl;
                if (group.display.dx) { swprintf(b, 96, g_str->fmt_pt_dx, dx); dl += b; dl += xunit; dl += L" "; }
                if (group.display.dy) {
                    if (g.mode==AnalysisMode::FRF) swprintf(b,96,L"ΔКД=%.5g",dy);
                    else swprintf(b, 96, g_str->fmt_pt_dy, dy);
                    dl += b;
                }
                if (group.display.inv_dt) {
                    const double inv = (dx != 0.0) ? 1.0 / dx : 0.0;
                    if ((g.mode == AnalysisMode::FFT) || (g.mode == AnalysisMode::FRF)) {
                        swprintf(b, 96, g_str == &kEn ? L"1/Δf=%.5g Hz" : L"1/Δf=%.5g Гц", inv);
                    } else {
                        swprintf(b, 96, g_str->fmt_pt_invdt, inv);
                    }
                    dl += b;
                    dl += L" ";
                }
                if (group.display.dist) {
                    swprintf(b, 96, g_str->fmt_pt_dist, std::sqrt(dx * dx + dy * dy)); dl += b;
                }
                if (!dl.empty()) {
                    const int mxp = (mx(group.points[i].first) + mx(group.points[i - 1].first)) / 2;
                    const int myp = (my(group.points[i].second) + my(group.points[i - 1].second)) / 2;
                    SetTextColor(dc, group.color);
                    SetTextAlign(dc, TA_CENTER | TA_BOTTOM);
                    SIZE ts;
                    GetTextExtentPoint32W(dc, dl.c_str(), static_cast<int>(dl.size()), &ts);
                    RoundRect(dc, mxp - ts.cx/2 - 4, myp - 4 - ts.cy, mxp + ts.cx/2 + 6, myp + 2, 3, 3);
                    TextOutW(dc, mxp, myp - 2, dl.c_str(), static_cast<int>(dl.size()));
                }
            }
        }
        SelectObject(dc, old_point_pen);
        DeleteObject(pp);
    }
    SelectObject(dc, old);
    DeleteObject(bp);
    SelectObject(dc, prev_brush);
    DeleteObject(wb);
    SelectObject(dc, oldf);

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

// Render a polyline as a smooth Catmull-Rom spline through the given pixel
// points. Purely visual: it curves *between* samples but never moves them.
int catmull_rom_segment_steps(const POINT& a, const POINT& b) {
    const int dist = max(std::abs(b.x - a.x), std::abs(b.y - a.y));
    if (dist <= 2) return 1;
    const int max_steps = g.light_mode ? 5 : 8;
    return std::clamp(dist / 12 + 1, 1, max_steps);
}

bool should_render_smoothed_polyline(std::size_t point_count, int pixel_width) {
    if (!g.visual_smooth || point_count < 2) return false;
    const std::size_t soft_limit = g.light_mode
        ? std::max<std::size_t>(static_cast<std::size_t>(pixel_width) * 3, 600)
        : std::max<std::size_t>(static_cast<std::size_t>(pixel_width) * 6, 1800);
    return point_count <= soft_limit;
}

void draw_catmull_rom(HDC dc, const std::vector<POINT>& pts) {
    if (pts.size() < 2) return;
    if (pts.size() == 2) {
        MoveToEx(dc, pts[0].x, pts[0].y, nullptr);
        LineTo(dc, pts[1].x, pts[1].y);
        return;
    }
    MoveToEx(dc, pts[0].x, pts[0].y, nullptr);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const POINT& p0 = pts[i == 0 ? 0 : i - 1];
        const POINT& p1 = pts[i];
        const POINT& p2 = pts[i + 1];
        const POINT& p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
        const int steps = catmull_rom_segment_steps(p1, p2);
        for (int s = 1; s <= steps; ++s) {
            const double t = static_cast<double>(s) / steps;
            const double t2 = t * t, t3 = t2 * t;
            const double x = 0.5 * (2.0 * p1.x + (-p0.x + p2.x) * t +
                                    (2.0 * p0.x - 5.0 * p1.x + 4.0 * p2.x - p3.x) * t2 +
                                    (-p0.x + 3.0 * p1.x - 3.0 * p2.x + p3.x) * t3);
            const double y = 0.5 * (2.0 * p1.y + (-p0.y + p2.y) * t +
                                    (2.0 * p0.y - 5.0 * p1.y + 4.0 * p2.y - p3.y) * t2 +
                                    (-p0.y + 3.0 * p1.y - 3.0 * p2.y + p3.y) * t3);
            LineTo(dc, static_cast<int>(x + 0.5), static_cast<int>(y + 0.5));
        }
    }
}

TimeStepEstimate estimate_time_step(const std::vector<double>& time, std::size_t lo, std::size_t hi, std::size_t max_diffs) {
    if (hi <= lo + 1 || max_diffs == 0) return {};
    std::vector<double> diffs;
    diffs.reserve(max_diffs);
    const std::size_t span = hi - lo;
    const std::size_t stride = std::max<std::size_t>(1, span / std::max<std::size_t>(max_diffs, 1));
    for (std::size_t i = lo + 1; i < hi && diffs.size() < max_diffs; i += stride) {
        const double d = time[i] - time[i - 1];
        if (std::isfinite(d) && d > 0.0) diffs.push_back(d);
    }
    if (diffs.empty()) return {};

    std::sort(diffs.begin(), diffs.end());
    const std::size_t mid = diffs.size() / 2;
    double median = (diffs.size() % 2 == 0)
        ? 0.5 * (diffs[mid - 1] + diffs[mid])
        : diffs[mid];
    if (!(median > 0.0) || !std::isfinite(median)) return {};
    return { median, diffs.size() };
}

double effective_time_gap_step(const std::vector<double>& time, std::size_t lo, std::size_t hi) {
    const TimeStepEstimate local = estimate_time_step(time, lo, hi, 512);
    ensure_global_gap_step_ready();
    const double global_step = g.cached_global_gap_step;

    if (local.step > 0.0 && global_step > 0.0) {
        if (local.count < 8) return global_step;
        if (local.step > global_step * 8.0) return global_step;
        return std::min(local.step, global_step * 4.0);
    }
    if (local.step > 0.0) return local.step;
    if (global_step > 0.0) return global_step;
    return 0.0;
}

void draw_time(HDC dc, const RECT& p) {
    g.visible_gap_markers.clear();
    const std::vector<double>& t = g.ds.time;
    const std::size_t n = t.size();
    std::size_t lo = static_cast<std::size_t>(
        std::lower_bound(t.begin(), t.end(), g.win_start) - t.begin());
    std::size_t hi = static_cast<std::size_t>(
        std::upper_bound(t.begin(), t.end(), g.win_end) - t.begin());
    if (lo > 0) --lo;
    if (hi < n) ++hi;
    if (hi <= lo) return;

    // Vertical range: auto-fit to the visible window, or frozen when locked.
    double ymin, ymax;
    current_time_yrange_window(lo, hi, ymin, ymax);
    if (!g.auto_y) { ymin = g.y_lock_min; ymax = g.y_lock_max; }

    const double displayed_start = stitched_time_from_raw(g.win_start);
    const double displayed_end = stitched_time_from_raw(g.win_end);
    draw_axes(dc, p, displayed_start, displayed_end, ymin, ymax, g_str->plot_xlabel_time);

    const int pw = p.right - p.left, ph = p.bottom - p.top;
    const double xspan = displayed_end - displayed_start;
    auto mapx = [&](double tt) { return p.left + static_cast<int>((stitched_time_from_raw(tt) - displayed_start) / xspan * pw); };
    auto mapy = [&](double yy) { return p.bottom - static_cast<int>((yy - ymin) / (ymax - ymin) * ph); };

    HRGN clip = CreateRectRgn(p.left + 1, p.top + 1, p.right, p.bottom);
    SelectClipRgn(dc, clip);

    double fft0 = 0.0, fft1 = 0.0;
    bool show_fft_window = false;
    bool has_visible_fft_window = false;
    int fft_left_px = 0, fft_right_px = 0;
    if (g.fft_selecting) {
        fft0 = g.fft_select_anchor_t;
        fft1 = g.fft_select_current_t;
        show_fft_window = true;
    } else if (has_fft_window()) {
        fft0 = g.fft_window_start;
        fft1 = g.fft_window_end;
        show_fft_window = true;
    }
    if (show_fft_window) {
        double sel0 = std::min(fft0, fft1);
        double sel1 = std::max(fft0, fft1);
        clamp_time_window(sel0, sel1);
        fft0 = sel0;
        fft1 = sel1;
        const double vis0 = std::max(sel0, g.win_start);
        const double vis1 = std::min(sel1, g.win_end);
        if (vis1 > vis0) {
            has_visible_fft_window = true;
            fft_left_px = mapx(vis0);
            fft_right_px = mapx(vis1);
            if (fft_right_px <= fft_left_px) fft_right_px = fft_left_px + 1;
        }
    }

    static std::vector<double> cmin, cmax;
    const bool visible_channels = any_visible_channel();
    // Keep the sparse path for genuinely small windows only; it is more expensive
    // per sample than the min/max projection used for denser views.
    const bool sparse = (hi - lo) <= static_cast<std::size_t>(pw) * 2;
    const bool allow_smoothing = should_render_smoothed_polyline(hi - lo, pw);
    const std::size_t light_mode_gap_budget = std::max<std::size_t>(static_cast<std::size_t>(pw) * 48, 120000);
    const std::size_t normal_gap_budget = std::max<std::size_t>(static_cast<std::size_t>(pw) * 96, 220000);
    const bool enable_gap_scan =
        g.show_gap_markers && !g.stitch_time_gaps &&
        (!g.light_mode || visible_channels) &&
        (hi - lo <= (g.light_mode ? light_mode_gap_budget : normal_gap_budget));
    const double gap_step = enable_gap_scan ? effective_time_gap_step(t, lo, hi) : 0.0;
    const double gap_threshold = (gap_step > 0.0)
        ? (gap_step * 64.0)
        : std::numeric_limits<double>::infinity();

    struct TimeGapAnnotation {
        int left_px = 0;
        int right_px = 0;
        double duration = 0.0;
        long long estimated_missing_samples = 0;
    };
    std::vector<TimeGapAnnotation> gap_annotations;
    if (enable_gap_scan && std::isfinite(gap_threshold)) {
        for (std::size_t i = std::max<std::size_t>(lo + 1, 1); i < hi; ++i) {
            const double dt = t[i] - t[i - 1];
            if (!std::isfinite(dt) || dt <= gap_threshold) continue;

            int left_px = mapx(t[i - 1]);
            int right_px = mapx(t[i]);
            if (right_px < left_px) std::swap(left_px, right_px);
            left_px = std::clamp(left_px, static_cast<int>(p.left) + 1, static_cast<int>(p.right) - 1);
            right_px = std::clamp(right_px, static_cast<int>(p.left) + 1, static_cast<int>(p.right) - 1);
            if (right_px <= left_px) right_px = std::min(static_cast<int>(p.right) - 1, left_px + 1);

            long long estimated_missing_samples = 0;
            if (gap_step > 0.0) {
                estimated_missing_samples = gap_estimated_missing_samples(dt, gap_step);
            }

            gap_annotations.push_back(TimeGapAnnotation{
                left_px,
                right_px,
                dt,
                estimated_missing_samples
            });
        }
    }

    if (g.show_gap_markers && !gap_annotations.empty()) {
        const COLORREF gap_orange = RGB(245, 140, 32);
        const COLORREF gap_fill = (g_theme == &kDarkTheme)
            ? mix_color(g_theme->bg_plot, gap_orange, 44)
            : mix_color(g_theme->bg_plot, gap_orange, 32);
        HBRUSH gap_brush = CreateSolidBrush(gap_fill);
        HPEN gap_pen = CreatePen(PS_DOT, 1, gap_orange);
        HGDIOBJ old_gap_brush = SelectObject(dc, gap_brush);
        HGDIOBJ old_gap_pen = SelectObject(dc, gap_pen);

        for (const auto& gap : gap_annotations) {
            RECT gap_rect = { gap.left_px, p.top + 1, gap.right_px, p.bottom - 1 };
            if (gap_rect.right <= gap_rect.left) continue;
            Rectangle(dc, gap_rect.left, gap_rect.top, gap_rect.right, gap_rect.bottom);
            App::GapMarkerVisual visual;
            visual.rect = gap_rect;
            visual.duration = gap.duration;
            visual.estimated_missing_samples = gap.estimated_missing_samples;
            g.visible_gap_markers.push_back(visual);
        }

        SelectObject(dc, old_gap_pen);
        SelectObject(dc, old_gap_brush);
        DeleteObject(gap_pen);
        DeleteObject(gap_brush);
    }

    if (g.light_mode && !visible_channels) {
        SelectClipRgn(dc, nullptr);
        DeleteObject(clip);
        g_legend_items.clear();
        g_legend_box = {0, 0, 0, 0};
        g.vx0 = displayed_start; g.vx1 = displayed_end; g.vy0 = ymin; g.vy1 = ymax;
        g.vrect = p; g.vvalid = true;
        return;
    }

    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        const ChannelRenderView view = make_channel_render_view(c);
        HPEN pen = CreatePen(g.distinguish_curves ? curve_pen_style(c) : PS_SOLID, 1, channel_color(c));
        HGDIOBJ old = SelectObject(dc, pen);
        int last_symbol_x=std::numeric_limits<int>::min()/2;

        if (sparse) {
            // Collect contiguous (non-NaN) runs, then draw each as straight
            // segments or a visual spline. NaN gaps break the line.
            std::vector<POINT> run;
            run.reserve(std::min<std::size_t>(hi - lo, 2048));
            auto flush = [&]() {
                if (allow_smoothing) {
                    draw_catmull_rom(dc, run);
                } else {
                    for (std::size_t k = 0; k < run.size(); ++k) {
                        if (k == 0) MoveToEx(dc, run[k].x, run[k].y, nullptr);
                        else LineTo(dc, run[k].x, run[k].y);
                    }
                }
                if (g.distinguish_curves) {
                    for (const auto& point:run) {
                        if (point.x-last_symbol_x<52) continue;
                        draw_curve_symbol(dc,point.x,point.y,c,channel_color(c));
                        last_symbol_x=point.x;
                    }
                }
                run.clear();
            };
            for (std::size_t i = lo; i < hi; ++i) {
                const double v = channel_render_value(view, i);
                if (std::isnan(v)) { flush(); continue; }
                if (!run.empty() && i > lo) {
                    const double dt = t[i] - t[i - 1];
                    if (!g.stitch_time_gaps && std::isfinite(dt) && dt > gap_threshold) flush();
                }
                run.push_back(POINT{mapx(t[i]), mapy(v)});
            }
            flush();

            // With visual smoothing on and few points in view, mark the real
            // samples so it's clear the curve only interpolates between them.
            if (!g.distinguish_curves && allow_smoothing && (hi - lo) <= 400) {
                HBRUSH dot = CreateSolidBrush(channel_color(c));
                HGDIOBJ ob = SelectObject(dc, dot);
                HGDIOBJ opn = SelectObject(dc, GetStockObject(NULL_PEN));
                for (std::size_t i = lo; i < hi; ++i) {
                    const double v = channel_render_value(view, i);
                    if (std::isnan(v)) continue;
                    const int px = mapx(t[i]), py = mapy(v);
                    Ellipse(dc, px - 2, py - 2, px + 3, py + 3);
                }
                SelectObject(dc, opn);
                SelectObject(dc, ob);
                DeleteObject(dot);
            }
        } else {
            cmin.resize(pw, std::numeric_limits<double>::infinity());
            cmax.resize(pw, -std::numeric_limits<double>::infinity());
            std::fill(cmin.begin(), cmin.end(), std::numeric_limits<double>::infinity());
            std::fill(cmax.begin(), cmax.end(), -std::numeric_limits<double>::infinity());
            const auto sample = [&](std::size_t i) { return channel_render_value(view, i); };
            const auto& envelope = channel_envelope(c, view);
            std::size_t begin = lo;
            for (int pixel = 0; pixel < pw; ++pixel) {
                const double right_time = raw_time_from_stitched(displayed_start + xspan * (pixel + 1) / pw);
                const std::size_t end = pixel + 1 == pw ? hi : static_cast<std::size_t>(
                    std::lower_bound(t.begin() + static_cast<std::ptrdiff_t>(begin), t.begin() + static_cast<std::ptrdiff_t>(hi), right_time) - t.begin());
                const auto range = envelope.query(begin, end, sample);
                if (std::isfinite(range.first) && std::isfinite(range.second)) {
                    cmin[pixel] = range.first;
                    cmax[pixel] = range.second;
                }
                begin = end;
            }
            int prev_x = -1, prev_y = 0;
            for (int cxp = 0; cxp < pw; ++cxp) {
                if (!std::isfinite(cmax[cxp])) continue;
                const int x = p.left + cxp;
                const int yhi = mapy(cmax[cxp]);
                const int ylo = mapy(cmin[cxp]);
                MoveToEx(dc, x, ylo, nullptr);
                LineTo(dc, x, yhi - 1);
                if (prev_x >= 0 && x - prev_x <= 2) {
                    MoveToEx(dc, prev_x, prev_y, nullptr);
                    LineTo(dc, x, (yhi + ylo) / 2);
                }
                if (g.distinguish_curves && x-last_symbol_x>=52) {
                    draw_curve_symbol(dc,x,(yhi+ylo)/2,c,channel_color(c));
                    last_symbol_x=x;
                }
                prev_x = x; prev_y = (yhi + ylo) / 2;
            }
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }

    // Playhead.
    if (g.playhead_active && g.playhead >= g.win_start && g.playhead <= g.win_end) {
        HPEN ph_pen = CreatePen(PS_SOLID, 2, g_theme->playhead);
        HGDIOBJ old = SelectObject(dc, ph_pen);
        const int X = mapx(g.playhead);
        MoveToEx(dc, X, p.top, nullptr);
        LineTo(dc, X, p.bottom);
        SelectObject(dc, old);
        DeleteObject(ph_pen);
    }

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);

    if (show_fft_window) {
        HRGN overlay_clip = CreateRectRgn(p.left, p.top, p.right + 1, p.bottom + 1);
        SelectClipRgn(dc, overlay_clip);

        HPEN tick_pen = CreatePen(PS_SOLID, 2, g_theme->accent);
        HGDIOBJ old_tick_pen = SelectObject(dc, tick_pen);
        const int tick = 7;
        const int top_y = p.top + 2;
        const int bottom_y = p.bottom - 2;
        const int handle_margin = 9;
        const int tri_w = 6;
        const int tri_h = 7;
        const COLORREF handle_outline = g_theme->btn_border;
        const COLORREF handle_fill = g_theme->accent;
        const int min_x = static_cast<int>(p.left) + handle_margin;
        const int max_x = static_cast<int>(p.right) - handle_margin;
        auto clamp_handle_x = [&](int x) {
            if (min_x > max_x) return (static_cast<int>(p.left) + static_cast<int>(p.right)) / 2;
            return std::clamp(x, min_x, max_x);
        };
        const int left_x = clamp_handle_x(mapx(fft0));
        const int right_x = clamp_handle_x(mapx(fft1));
        {
            const int sel_left = has_visible_fft_window
                ? std::clamp(std::min(fft_left_px, fft_right_px), static_cast<int>(p.left) + 1, static_cast<int>(p.right) - 1)
                : static_cast<int>(p.right) - 1;
            const int sel_right = has_visible_fft_window
                ? std::clamp(std::max(fft_left_px, fft_right_px), static_cast<int>(p.left) + 1, static_cast<int>(p.right) - 1)
                : static_cast<int>(p.left) + 1;
            const int shade_top = static_cast<int>(p.top) + 1;
            const int shade_bottom = static_cast<int>(p.bottom) - 1;
            const int shade_h = shade_bottom - shade_top;
            if (shade_h > 0) {
                Gdiplus::Graphics gfx(dc);
                gfx.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
                const bool dark = (g_theme == &kDarkTheme);
                const Gdiplus::Color outer_fill_color(dark ? 48 : 34,
                    GetRValue(g_theme->text_secondary),
                    GetGValue(g_theme->text_secondary),
                    GetBValue(g_theme->text_secondary));
                const Gdiplus::Color outer_hatch_color(dark ? 92 : 76,
                    GetRValue(g_theme->accent),
                    GetGValue(g_theme->accent),
                    GetBValue(g_theme->accent));
                const Gdiplus::Color selected_fill_color(dark ? 14 : 10,
                    GetRValue(g_theme->accent),
                    GetGValue(g_theme->accent),
                    GetBValue(g_theme->accent));
                const Gdiplus::Color selected_edge_color(dark ? 140 : 120,
                    GetRValue(g_theme->accent),
                    GetGValue(g_theme->accent),
                    GetBValue(g_theme->accent));
                const Gdiplus::Color selected_edge_soft(dark ? 70 : 58,
                    GetRValue(g_theme->text_secondary),
                    GetGValue(g_theme->text_secondary),
                    GetBValue(g_theme->text_secondary));
                const Gdiplus::Color clear_color(0, 0, 0, 0);

                auto fill_rect = [&](int x0, int x1, const Gdiplus::Color& fill, const Gdiplus::Color& hatch_color, Gdiplus::HatchStyle hatch_style) {
                    if (x1 <= x0) return;
                    const int w = x1 - x0;
                    Gdiplus::SolidBrush fill_brush(fill);
                    gfx.FillRectangle(
                        &fill_brush,
                        static_cast<Gdiplus::REAL>(x0),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(w),
                        static_cast<Gdiplus::REAL>(shade_h));
                    Gdiplus::HatchBrush hatch(
                        hatch_style,
                        hatch_color,
                        clear_color);
                    gfx.FillRectangle(
                        &hatch,
                        static_cast<Gdiplus::REAL>(x0),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(w),
                        static_cast<Gdiplus::REAL>(shade_h));
                };

                auto tint_rect = [&](int x0, int x1, const Gdiplus::Color& fill) {
                    if (x1 <= x0) return;
                    const int w = x1 - x0;
                    Gdiplus::SolidBrush fill_brush(fill);
                    gfx.FillRectangle(
                        &fill_brush,
                        static_cast<Gdiplus::REAL>(x0),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(w),
                        static_cast<Gdiplus::REAL>(shade_h));
                };

                fill_rect(static_cast<int>(p.left) + 1, sel_left, outer_fill_color, outer_hatch_color, Gdiplus::HatchStyleWideDownwardDiagonal);
                fill_rect(sel_right, static_cast<int>(p.right), outer_fill_color, outer_hatch_color, Gdiplus::HatchStyleWideDownwardDiagonal);
                if (has_visible_fft_window && sel_right > sel_left) {
                    tint_rect(sel_left, sel_right, selected_fill_color);
                }

                if (has_visible_fft_window && sel_right > sel_left) {
                    HPEN sel_pen = CreatePen(PS_DOT, 1, g_theme->accent);
                    HGDIOBJ old_pen = SelectObject(dc, sel_pen);
                    MoveToEx(dc, sel_left, p.top, nullptr); LineTo(dc, sel_left, p.bottom);
                    MoveToEx(dc, sel_right, p.top, nullptr); LineTo(dc, sel_right, p.bottom);
                    SelectObject(dc, old_pen);
                    DeleteObject(sel_pen);

                    Gdiplus::Pen edge_pen(selected_edge_color, 1.0f);
                    gfx.DrawLine(&edge_pen,
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_bottom));
                    gfx.DrawLine(&edge_pen,
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_bottom));
                    gfx.DrawLine(&edge_pen,
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_top));
                    gfx.DrawLine(&edge_pen,
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_bottom),
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_bottom));

                    Gdiplus::Pen soft_pen(selected_edge_soft, 1.0f);
                    gfx.DrawLine(&soft_pen,
                        static_cast<Gdiplus::REAL>(sel_left + 1),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(sel_left + 1),
                        static_cast<Gdiplus::REAL>(shade_bottom));
                    gfx.DrawLine(&soft_pen,
                        static_cast<Gdiplus::REAL>(sel_right - 1),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(sel_right - 1),
                        static_cast<Gdiplus::REAL>(shade_bottom));
                    gfx.DrawLine(&soft_pen,
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_top + 1),
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_top + 1));
                    gfx.DrawLine(&soft_pen,
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_bottom - 1),
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_bottom - 1));
                }
            }
        }

        auto draw_handle = [&](int x) {
            MoveToEx(dc, x, top_y, nullptr); LineTo(dc, x + tick, top_y);
            MoveToEx(dc, x - tick, bottom_y, nullptr); LineTo(dc, x, bottom_y);

            POINT top_tri[3] = {
                {x, top_y + tri_h},
                {x - tri_w, top_y},
                {x + tri_w, top_y},
            };
            HBRUSH tri_brush = CreateSolidBrush(handle_fill);
            HPEN tri_pen = CreatePen(PS_SOLID, 1, handle_outline);
            HGDIOBJ old_brush = SelectObject(dc, tri_brush);
            HGDIOBJ old_pen2 = SelectObject(dc, tri_pen);
            Polygon(dc, top_tri, 3);
            SelectObject(dc, old_pen2);
            SelectObject(dc, old_brush);
            DeleteObject(tri_pen);
            DeleteObject(tri_brush);

            const int dot_r = 4;
            const int dot_y = bottom_y - 1;
            HBRUSH dot_brush = CreateSolidBrush(handle_fill);
            HPEN dot_pen = CreatePen(PS_SOLID, 1, handle_outline);
            old_brush = SelectObject(dc, dot_brush);
            old_pen2 = SelectObject(dc, dot_pen);
            Ellipse(dc, x - dot_r, dot_y - dot_r, x + dot_r + 1, dot_y + dot_r + 1);
            SelectObject(dc, old_pen2);
            SelectObject(dc, old_brush);
            DeleteObject(dot_pen);
            DeleteObject(dot_brush);
        };

        if (has_visible_fft_window) {
            draw_handle(left_x);
            draw_handle(right_x);
        }

        SelectObject(dc, old_tick_pen);
        DeleteObject(tick_pen);

        SelectClipRgn(dc, nullptr);
        DeleteObject(overlay_clip);
    }
    draw_legend(dc, p);

    g.vx0 = displayed_start; g.vx1 = displayed_end; g.vy0 = ymin; g.vy1 = ymax;
    g.vrect = p; g.vvalid = true;
}

void draw_freq(HDC dc, const RECT& p) {
    if (!ensure_current_spectrum() || g.spec.freqs.size() < 2) {
        draw_axes(dc, p, 0, 1, 0, 1, g_str->plot_xlabel_freq);
        RECT message_rect = p;
        InflateRect(&message_rect, -24, -24);
        const std::wstring message = g.spec_pending
            ? (g_str == &kEn ? L"Calculating spectrum…" : L"Вычисление спектра…")
            : (!g.spec.error.empty() ? to_w(g.spec.error) : (g_str == &kEn ? L"No spectrum available." : L"Спектр недоступен."));
        SetTextColor(dc, g_theme->text_primary);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, message.c_str(), -1, &message_rect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        return;
    }
    const auto& f = g.spec.freqs;
    double f0 = g.freq_start, f1 = g.freq_end;
    if (f1 <= f0) { f0 = 0.0; f1 = g.spec.nyquist; }

    std::size_t klo = static_cast<std::size_t>(std::lower_bound(f.begin(), f.end(), f0) - f.begin());
    std::size_t khi = static_cast<std::size_t>(std::upper_bound(f.begin(), f.end(), f1) - f.begin());
    if (klo > 0) --klo;
    if (khi < f.size()) ++khi;

    double ymax = 0.0;
    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
        const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
        if (ci < 0 || !g.visible[ci]) continue;
        for (std::size_t k = klo; k < khi; ++k)
            if (g.spec.amp[j][k] > ymax) ymax = g.spec.amp[j][k];
    }
    if (ymax <= 0) ymax = 1;
    double ytop = ymax * 1.08;
    if (!g.auto_y_amp) ytop = g.y_amp_max;

    draw_axes(dc, p, f0, f1, 0, ytop, g_str->plot_xlabel_freq);

    const int pw = p.right - p.left, ph = p.bottom - p.top;
    const double fspan = f1 - f0;
    auto mapx = [&](double ff) { return p.left + static_cast<int>((ff - f0) / fspan * pw); };
    auto mapy = [&](double yy) { return p.bottom - static_cast<int>(yy / ytop * ph); };
    const std::size_t visible_bins = khi > klo ? (khi - klo) : 0;
    const bool sparse = visible_bins <= static_cast<std::size_t>(pw) * 2;
    const bool allow_smoothing = should_render_smoothed_polyline(visible_bins, pw);

    HRGN clip = CreateRectRgn(p.left + 1, p.top + 1, p.right, p.bottom);
    SelectClipRgn(dc, clip);

    static std::vector<double> cmin, cmax;
    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
        const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
        if (ci < 0 || !g.visible[ci]) continue;
        HPEN pen = CreatePen(g.distinguish_curves ? curve_pen_style(static_cast<std::size_t>(ci)) : PS_SOLID, 1, channel_color(ci));
        HGDIOBJ old = SelectObject(dc, pen);
        const auto& a = g.spec.amp[j];
        int last_symbol_x=std::numeric_limits<int>::min()/2;
        if (sparse) {
            if (allow_smoothing) {
                std::vector<POINT> pts;
                pts.reserve(visible_bins);
                for (std::size_t k = klo; k < khi; ++k) {
                    pts.push_back(POINT{mapx(f[k]), mapy(a[k])});
                }
                draw_catmull_rom(dc, pts);
                // Mark real samples when few are visible.
                if (!g.distinguish_curves && pts.size() <= 400) {
                    HBRUSH dot = CreateSolidBrush(channel_color(ci));
                    HGDIOBJ ob = SelectObject(dc, dot);
                    HGDIOBJ opn = SelectObject(dc, GetStockObject(NULL_PEN));
                    for (const auto& pt : pts)
                        Ellipse(dc, pt.x - 2, pt.y - 2, pt.x + 3, pt.y + 3);
                    SelectObject(dc, opn);
                    SelectObject(dc, ob);
                    DeleteObject(dot);
                }
            } else {
                bool started = false;
                for (std::size_t k = klo; k < khi; ++k) {
                    const int x = mapx(f[k]);
                    const int y = mapy(a[k]);
                    if (!started) {
                        MoveToEx(dc, x, y, nullptr);
                        started = true;
                    } else {
                        LineTo(dc, x, y);
                    }
                }
            }
            if (g.distinguish_curves) {
                for (std::size_t k=klo;k<khi;++k) {
                    const int x=mapx(f[k]);
                    if (x-last_symbol_x<52) continue;
                    draw_curve_symbol(dc,x,mapy(a[k]),static_cast<std::size_t>(ci),channel_color(ci));
                    last_symbol_x=x;
                }
            }
        } else {
            cmin.resize(pw, std::numeric_limits<double>::infinity());
            cmax.resize(pw, -std::numeric_limits<double>::infinity());
            std::fill(cmin.begin(), cmin.end(), std::numeric_limits<double>::infinity());
            std::fill(cmax.begin(), cmax.end(), -std::numeric_limits<double>::infinity());
            for (std::size_t k = klo; k < khi; ++k) {
                const double v = a[k];
                if (!std::isfinite(v)) continue;
                int cxp = mapx(f[k]) - p.left;
                if (cxp < 0 || cxp >= pw) continue;
                if (v < cmin[cxp]) cmin[cxp] = v;
                if (v > cmax[cxp]) cmax[cxp] = v;
            }
            int prev_x = -1;
            int prev_y = 0;
            for (int cxp = 0; cxp < pw; ++cxp) {
                if (!std::isfinite(cmax[cxp])) continue;
                const int x = p.left + cxp;
                const int yhi = mapy(cmax[cxp]);
                const int ylo = mapy(std::max(0.0, cmin[cxp]));
                MoveToEx(dc, x, ylo, nullptr);
                LineTo(dc, x, yhi - 1);
                if (prev_x >= 0 && x - prev_x <= 2) {
                    MoveToEx(dc, prev_x, prev_y, nullptr);
                    LineTo(dc, x, (yhi + ylo) / 2);
                }
                if (g.distinguish_curves && x-last_symbol_x>=52) {
                    draw_curve_symbol(dc,x,(yhi+ylo)/2,static_cast<std::size_t>(ci),channel_color(ci));
                    last_symbol_x=x;
                }
                prev_x = x;
                prev_y = (yhi + ylo) / 2;
            }
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
    draw_legend(dc, p);

    g.vx0 = f0; g.vx1 = f1; g.vy0 = 0; g.vy1 = ytop;
    g.vrect = p; g.vvalid = true;
}

void draw_chart(HDC dc, const RECT& p) {
    if (g.mode == AnalysisMode::FRF) { draw_frf(dc, p); return; }
    if (!has_data()) {
        g_legend_items.clear();
        g_legend_box = {0, 0, 0, 0};
        g.visible_gap_markers.clear();
        SetTextAlign(dc, TA_CENTER | TA_BASELINE);
        SetTextColor(dc, g_theme->text_secondary);
        std::wstring msg = (g_str == &kEn)
            ? L"Open a .lvm, .txt, or .csv file (" + hotkey_text_for_command(IDC_OPEN) + L")"
            : L"Откройте файл .lvm, .txt или .csv (" + hotkey_text_for_command(IDC_OPEN) + L")";
        TextOutW(dc, (p.left + p.right) / 2, (p.top + p.bottom) / 2, msg.c_str(), static_cast<int>(msg.size()));
        g.vvalid = false;
        return;
    }
    if ((g.mode == AnalysisMode::FFT)) {
        g.visible_gap_markers.clear();
        draw_freq(dc, p);
    } else {
        draw_time(dc, p);
    }
    draw_guides(dc);
    draw_markers(dc);
    draw_measure(dc);
}

void release_backbuffer() {
    if (g.backbuffer_dc && g.backbuffer_prev_bmp) {
        SelectObject(g.backbuffer_dc, g.backbuffer_prev_bmp);
        g.backbuffer_prev_bmp = nullptr;
    }
    if (g.backbuffer_bmp) {
        DeleteObject(g.backbuffer_bmp);
        g.backbuffer_bmp = nullptr;
    }
    if (g.backbuffer_dc) {
        DeleteDC(g.backbuffer_dc);
        g.backbuffer_dc = nullptr;
    }
    g.backbuffer_w = 0;
    g.backbuffer_h = 0;
}

bool ensure_backbuffer(HDC hdc, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (g.backbuffer_dc && g.backbuffer_bmp &&
        g.backbuffer_w == width && g.backbuffer_h == height) {
        return true;
    }

    release_backbuffer();

    g.backbuffer_dc = CreateCompatibleDC(hdc);
    if (!g.backbuffer_dc) return false;

    g.backbuffer_bmp = CreateCompatibleBitmap(hdc, width, height);
    if (!g.backbuffer_bmp) {
        release_backbuffer();
        return false;
    }

    g.backbuffer_prev_bmp = static_cast<HBITMAP>(SelectObject(g.backbuffer_dc, g.backbuffer_bmp));
    g.backbuffer_w = width;
    g.backbuffer_h = height;
    return true;
}

void on_paint(HDC hdc) {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int cw = rc.right, ch = rc.bottom;

    HDC mem = nullptr;
    HBITMAP temp_bmp = nullptr;
    HBITMAP temp_prev_bmp = nullptr;
    const bool using_persistent_backbuffer = ensure_backbuffer(hdc, cw, ch);
    if (using_persistent_backbuffer) {
        mem = g.backbuffer_dc;
    } else {
        mem = CreateCompatibleDC(hdc);
        if (!mem) return;
        temp_bmp = CreateCompatibleBitmap(hdc, cw, ch);
        if (!temp_bmp) {
            DeleteDC(mem);
            return;
        }
        temp_prev_bmp = static_cast<HBITMAP>(SelectObject(mem, temp_bmp));
    }

    HBRUSH bg = CreateSolidBrush(g_theme->bg_main);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    if (welcome_visible()) {
        BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
        if (!using_persistent_backbuffer) {
            SelectObject(mem, temp_prev_bmp);
            DeleteObject(temp_bmp);
            DeleteDC(mem);
        }
        return;
    }

    // Toolbar band across the top.
    RECT topbar = {0, 0, cw, kTopBar};
    HBRUSH tbb = CreateSolidBrush(g_theme->bg_toolbar);
    FillRect(mem, &topbar, tbb);
    DeleteObject(tbb);

    const int panel_w = side_panel_width();
    const int panel_left = cw - panel_w;
    if (panel_w > 0) {
        RECT panel = {panel_left, kTopBar, cw, ch - kBottomBar};
        HBRUSH pbg = CreateSolidBrush(g_theme->bg_panel);
        FillRect(mem, &panel, pbg);
        DeleteObject(pbg);
    }

    // Hairline separators (under the toolbar, left of the panel, above status bar).
    HPEN sep = CreatePen(PS_SOLID, 1, g_theme->separator);
    HGDIOBJ oldpen = SelectObject(mem, sep);
    MoveToEx(mem, 0, kTopBar - 1, nullptr); LineTo(mem, cw, kTopBar - 1);
    if (panel_w > 0) {
        MoveToEx(mem, panel_left, kTopBar, nullptr); LineTo(mem, panel_left, ch - kBottomBar);
    }
    MoveToEx(mem, 0, ch - kBottomBar, nullptr); LineTo(mem, cw, ch - kBottomBar);
    SelectObject(mem, oldpen);
    DeleteObject(sep);

    // Toolbar vertical separators (two compact rows)
    HPEN vsep = CreatePen(PS_SOLID, 1, g_theme->separator);
    oldpen = SelectObject(mem, vsep);
    for (int sx : g.toolbar_seps) {
        MoveToEx(mem, sx, 8, nullptr); LineTo(mem, sx, kTopBar - 4);
    }
    SelectObject(mem, oldpen);
    DeleteObject(vsep);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, g_theme->text_primary);
    SetTextAlign(mem, TA_LEFT | TA_TOP);
    SelectObject(mem, g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    // Draw colored channel indicators next to checkboxes
    if (panel_w > 0 && g.side_panel_tab == 0) {
        for (std::size_t i = 0; i < g.checks.size(); ++i) {
            HWND c = g.checks[i];
            if (!IsWindowVisible(c)) continue;
            RECT cr;
            GetWindowRect(c, &cr);
            MapWindowPoints(nullptr, g.main, (LPPOINT)&cr, 2);
            if (static_cast<int>(i) == g.side_selected_channel && i < g.check_labels.size() && g.check_labels[i]) {
                RECT lr;
                GetWindowRect(g.check_labels[i], &lr);
                MapWindowPoints(nullptr, g.main, reinterpret_cast<LPPOINT>(&lr), 2);
                RECT hi = {cr.left - 22, lr.top - 2, lr.right + 4, lr.bottom + 2};
                fill_rounded_rect(mem, hi,
                    mix_color(g_theme->bg_panel, g_theme->accent, (g_theme == &kDarkTheme) ? 20 : 12),
                    mix_color(g_theme->btn_border, g_theme->accent, (g_theme == &kDarkTheme) ? 52 : 44), 8);
            }
            int sq_y = cr.top + (cr.bottom - cr.top - 12) / 2;
            int sq_x = cr.left - 18;
            HBRUSH sq = CreateSolidBrush(channel_color(i));
            RECT sr = {sq_x, sq_y, sq_x + 12, sq_y + 12};
            FillRect(mem, &sr, sq);
            DeleteObject(sq);
            HPEN border = CreatePen(PS_SOLID, 1, g_theme->btn_border);
            HGDIOBJ old_sq_pen = SelectObject(mem, border);
            HGDIOBJ old_sq_brush = SelectObject(mem, GetStockObject(HOLLOW_BRUSH));
            Rectangle(mem, sr.left, sr.top, sr.right, sr.bottom);
            SelectObject(mem, old_sq_brush);
            SelectObject(mem, old_sq_pen);
            DeleteObject(border);
        }
    }

    SelectObject(mem, g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));

    const RECT plot = plot_rect();
    draw_chart(mem, plot);
    draw_gap_details_card(mem, plot);

    // Status bar (owner-drawn)
    RECT sb = {0, ch - kBottomBar, cw, ch};
    HBRUSH sbb = CreateSolidBrush(g_theme->bg_status);
    FillRect(mem, &sb, sbb);
    DeleteObject(sbb);
    HPEN sb_top = CreatePen(PS_SOLID, 1, g_theme->separator);
    oldpen = SelectObject(mem, sb_top);
    MoveToEx(mem, 0, ch - kBottomBar, nullptr); LineTo(mem, cw, ch - kBottomBar);
    SelectObject(mem, oldpen);
    DeleteObject(sb_top);
    const HFONT status_font = g.axis_font ? g.axis_font :
        (g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    HGDIOBJ previous_font = SelectObject(mem, status_font);
    const std::vector<std::wstring> status_parts = split_status_segments(g.status_text);
    std::vector<StatusSegment> status_segments;
    status_segments.reserve(status_parts.size() + (g.status_detail_text.empty() ? 0 : 1));
    for (std::size_t i = 0; i < status_parts.size(); ++i)
        status_segments.push_back({status_parts[i], g_theme->text_secondary, i != 0, false});
    if (!g.status_detail_text.empty())
        status_segments.push_back({g.status_detail_text, g.status_detail_color, false, true});
    std::vector<StatusSegment> required_status_segments = status_segments;
    required_status_segments.erase(std::remove_if(required_status_segments.begin(), required_status_segments.end(),
        [](const StatusSegment& segment) { return segment.optional; }), required_status_segments.end());
    const int required_status_width = status_segments_width(mem, required_status_segments);
    const int credit_width = status_text_width(mem, kStatusAuthorCredit);
    const bool show_credit = cw - 24 >= required_status_width + credit_width + 20;
    const int credit_x = show_credit ? cw - 12 - credit_width : cw;
    const int status_right = show_credit ? credit_x - 12 : cw - 12;

    SetTextColor(mem, g_theme->text_secondary);
    SetTextAlign(mem, TA_LEFT | TA_TOP);
    if (!g.hover_status_text.empty()) {
        SetTextColor(mem, g_theme->accent);
        draw_status_text(mem, 12, ch - kBottomBar + 7, status_right, g.hover_status_text);
    } else if (!status_segments.empty()) {
        draw_status_segments(mem, 12, ch - kBottomBar + 7, status_right, std::move(status_segments));
    }
    if (show_credit) {
        SetTextColor(mem, g_theme->text_secondary);
        TextOutW(mem, credit_x, ch - kBottomBar + 7, kStatusAuthorCredit, lstrlenW(kStatusAuthorCredit));
    }
    SelectObject(mem, previous_font);

    BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
    if (!using_persistent_backbuffer) {
        SelectObject(mem, temp_prev_bmp);
        DeleteObject(temp_bmp);
        DeleteDC(mem);
    }
}

} // namespace gui
