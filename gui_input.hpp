#pragma once
#include "gui_platform.hpp"

namespace gui {

void add_guide_line(bool vertical, double value);

bool px_to_data(int px, int py, double& dx, double& dy);

bool snap_to_nearest_target(double& dx, double& dy, int* out_channel = nullptr);

void snap_to_nearest(double& dx, double& dy);

int hit_test_marker(int px, int py);
bool point_group_contains_point(int group_index, double x, double y);
bool marker_exists_at_current_position(double x, double y);
bool guide_exists_at_current_position(bool vertical, double value);

void clear_annotation_selection();
bool delete_selected_annotation();
bool begin_annotation_drag(HWND hwnd, int px, int py);
void update_annotation_drag(int px, int py);
void finish_annotation_drag(bool commit);

LRESULT handle_input_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

} // namespace gui
