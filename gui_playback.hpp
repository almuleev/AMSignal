#pragma once
#include "gui_platform.hpp"

namespace gui {

const wchar_t* speed_menu_text();

void set_play_speed(double speed);

void stop_play();
void reset_playback();
void seek_playhead(double time);
bool hit_test_playhead(int x,int y);

void start_play();

void toggle_play();

LRESULT handle_playback_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

} // namespace gui
