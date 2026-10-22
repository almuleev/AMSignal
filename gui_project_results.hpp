#pragma once
#include "gui_platform.hpp"

namespace gui {
// Optional bounded acceleration, never required to open the original samples.
void write_project_results(std::ostream& out, const char* line_end);
void restore_project_results(const std::vector<std::string>& comments);
} // namespace gui
