#pragma once
#include "gui_platform.hpp"

namespace gui {
struct MarkerValuePanel {
    int marker=-1;
    std::wstring title,x,y;
    POINT anchor{};
    RECT bounds{},close{};
};
// Uses the marker's stored coordinates; never reads or recalculates source data.
bool marker_value_panel(HDC dc,MarkerValuePanel& panel);
void draw_marker_values(HDC dc);
bool marker_values_hit_test(int x,int y);
bool handle_marker_values_click(HWND hwnd,int x,int y);
void update_marker_values_hover(HWND hwnd,int x,int y);
}
