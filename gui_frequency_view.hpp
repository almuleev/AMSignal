#pragma once
#include "gui_platform.hpp"
namespace gui {
inline constexpr int kFrequencyViewHeight=132;
const wchar_t* frequency_view_title_text();
const wchar_t* frequency_view_apply_text();
void create_frequency_view_controls(HWND parent,HINSTANCE instance);
void sync_frequency_view_controls();
void layout_frequency_view_controls(int x,int y,int width,int viewport_top,int viewport_bottom,bool visible);
bool handle_frequency_view_command(HWND owner,WPARAM wp);
bool set_visible_frequency_range(double low,double high);
}
