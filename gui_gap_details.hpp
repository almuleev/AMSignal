#pragma once
#include "gui_platform.hpp"

namespace gui {

int hit_test_gap_marker(int x, int y);

void hide_gap_details_card();

// Application-wide display preference; disabling also clears every document's cards.
void set_gap_markers_visible(bool visible);

void show_gap_details_card(double duration, long long estimated_missing_samples);

void draw_gap_details_card(HDC dc, const RECT& plot);

} // namespace gui
