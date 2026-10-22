#pragma once
#include "gui_platform.hpp"

namespace gui {
// Project-only workspace. Restore after data, processing and full source bounds.
void write_project_workspace(std::ostream& out, const char* line_end);
void restore_project_workspace(const std::vector<std::string>& comments);
} // namespace gui
