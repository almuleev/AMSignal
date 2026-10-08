#pragma once
#include "gui_platform.hpp"

namespace gui {

enum class AnalysisMode;
struct PointGroup;

struct LegendItem { int channel; RECT rect; };

extern std::vector<LegendItem> g_legend_items;

extern RECT g_legend_box;
extern RECT g_legend_close_box;

void invalidate_plot();

void draw_text(HDC dc, int x, int y, const wchar_t* s, UINT align);

int curve_pen_style(std::size_t curve_index);

void draw_curve_symbol(HDC dc, int x, int y, std::size_t curve_index, COLORREF color, int radius = 4);

void draw_axes(HDC dc, const RECT& p, double x0, double x1, double y0, double y1,
               const wchar_t* xlabel);

// Editable names in separate outer rows, above Y and below the physical X caption.
void draw_user_axis_names(HDC dc, const RECT& p, AnalysisMode mode);

// Physical unit selected in Settings for amplitude scales.
std::wstring vertical_axis_unit();

// User-defined coordinate name for the requested graph mode.
std::wstring axis_label_for(AnalysisMode mode, bool x_axis);

// Localized label for amplitude scales; used by Time and FFT.
std::wstring amplitude_axis_label();

// Differences use the physical units of the group's axes. Coordinate distance
// is explicitly arbitrary; unlike dx/dy, it is not a physical measurement.
std::wstring point_difference_text(const PointGroup& group, double dx, double dy);

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
