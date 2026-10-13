#pragma once
#include "gui_platform.hpp"
#include <functional>
#include <cstdint>

namespace gui {
// Original extremum coordinates, transformed only for display. connect=false
// forbids a line from the preceding point, including mixed missing/gap nodes.
struct OverviewCurvePoint { double time=0,value=0;bool connect=false;std::uint64_t sample=0; };
void visit_overview_curve(std::size_t logical_channel,double gap_threshold,
                          const std::function<void(OverviewCurvePoint)>& consume);
void draw_overview_time(HDC dc,const RECT& plot);
} // namespace gui
