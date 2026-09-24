#pragma once
#include "gui_platform.hpp"
namespace gui {
double frf_frequency_at_fraction(double fraction);
double frf_frequency_fraction(double frequency);
bool snap_to_displayed_frf_curve(double& frequency, double& coefficient);
double frf_reference_y_max();
void frf_y_range(double& low, double& high);
RECT frf_coefficient_plot_rect(const RECT& full_plot);
RECT frf_reference_plot_rect(const RECT& full_plot);
void draw_frf(HDC dc, const RECT& plot);
LRESULT handle_frf_input(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
} // namespace gui
