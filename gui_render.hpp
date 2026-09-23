#pragma once
#include "gui_platform.hpp"

namespace gui {

struct LegendItem { int channel; RECT rect; };

extern std::vector<LegendItem> g_legend_items;

extern RECT g_legend_box;

void invalidate_plot();

void draw_text(HDC dc, int x, int y, const wchar_t* s, UINT align);

int curve_pen_style(std::size_t curve_index);

void draw_curve_symbol(HDC dc, int x, int y, std::size_t curve_index, COLORREF color, int radius = 4);

void draw_axes(HDC dc, const RECT& p, double x0, double x1, double y0, double y1,
               const wchar_t* xlabel);

// Physical unit selected in Settings for amplitude scales.
std::wstring vertical_axis_unit();

// Localized label for amplitude scales; used by Time and FFT.
std::wstring amplitude_axis_label();

void draw_legend(HDC dc, const RECT& p);

void draw_guides(HDC dc);

void draw_markers(HDC dc);

void draw_measure(HDC dc);

int catmull_rom_segment_steps(const POINT& a, const POINT& b);

bool should_render_smoothed_polyline(std::size_t point_count, int pixel_width);

void draw_catmull_rom(HDC dc, const std::vector<POINT>& pts);

struct TimeStepEstimate {
    double step = 0.0;
    std::size_t count = 0;
};

TimeStepEstimate estimate_time_step(const std::vector<double>& time, std::size_t lo, std::size_t hi, std::size_t max_diffs);

double effective_time_gap_step(const std::vector<double>& time, std::size_t lo, std::size_t hi);

void draw_time(HDC dc, const RECT& p);

void draw_freq(HDC dc, const RECT& p);

void draw_chart(HDC dc, const RECT& p);

void release_backbuffer();

bool ensure_backbuffer(HDC hdc, int width, int height);

void on_paint(HDC hdc);

} // namespace gui
