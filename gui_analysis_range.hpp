#pragma once
#include "gui_platform.hpp"

namespace gui {
// -1 if the real boundary is outside the current viewport.
int analysis_range_edge_x(double time,const RECT& plot);
// 0 none, 1 left, 2 right. Only real, visible time-range boundaries are targets.
int hit_test_analysis_range_edge(int x,int y);
void draw_analysis_range_selection(HDC dc,const RECT& plot);
std::wstring analysis_range_caption_text();
void update_analysis_range_hover(HWND hwnd,int x,int y);
bool begin_analysis_range_edge_drag(HWND hwnd,int x,int y);
void update_analysis_range_edge_drag(HWND hwnd,int x,int y);
void finish_analysis_range_edge_drag(HWND hwnd,int x,int y);
void cancel_analysis_range_drag(HWND hwnd);
}
